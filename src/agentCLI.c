#include <stdio.h>
#include <conio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <signal.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

#define AGENT_IMPLEMENTATION
#include "agent.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"

#define AGENT_CLI_STATE_FILE (".agent_cli_state")
#define NEXT_BEST_RATE_INCREMENT (0.1f)
#define SUCCESS_WINDOW 100
#define SUCCESS_THRESHOLD 0.8f

// #define USE_DISTANCE_TO_GOAL_REWARD 1

#ifdef _WIN32
    const char* maze_filter =
    "Maze Files (*.npy;*.maze)\0*.npy;*.maze\0"
    "All Files (*.*)\0*.*\0"
    "\0";
	const char* qtable_filter = 
	"Qtable Files (*.qtable)\0*.qtable\0"
	"All Files (*.*)\0*.*\0"
	"\0";
#endif

#ifdef _WIN32
char* windows_open_file_dialog(const char* filter, const char* initialDir, bool save_mode) {
    OPENFILENAMEA ofn;
    char *fileName = (char*)malloc(4096);
    if (!fileName) return NULL;
    fileName[0] = '\0';

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = 4096;
    ofn.lpstrInitialDir = initialDir;
    ofn.Flags = save_mode ? OFN_NOCHANGEDIR : OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn)) {
        return fileName;
    } else {
        free(fileName);
        return NULL;
    }
}
#endif

// Fallback: pede path via stdin (uso em non-Windows)
char* prompt_path_via_stdin(void){
    char buf[4096];
    printf("Digite caminho do arquivo .npy: ");
    if(!fgets(buf,sizeof(buf),stdin)) return NULL;
    size_t l = strlen(buf);
    if(l && (buf[l-1]=='\n' || buf[l-1]=='\r')) buf[l-1]='\0';
    return strdup(buf);
}

static void read_float(const char* prompt, float* out, float def) {
    char buf[128];    
	printf("%s (default %.2e): ", prompt, def);
    if (!fgets(buf, sizeof(buf), stdin)) {
        *out = def;
        return;
    }

    if (buf[0] == '\n') {
        *out = def;
        return;
    }

    char* endptr;
    float val = strtof(buf, &endptr);
    if (endptr == buf) {
        *out = def;
    } else {
        *out = val;
    }
}

static void read_int(const char* prompt, int* out, int def) {
    char buf[128];
    
	printf("%s (default %u): ", prompt, def);
	
    if (!fgets(buf, sizeof(buf), stdin)) {
        *out = def;
        return;
    }

    if (buf[0] == '\n') {
        *out = def;
        return;
    }

    char* endptr;
    int val = strtol(buf, &endptr,10);
    if (endptr == buf) {
        *out = def;
    } else {
        *out = val;
    }
}

int save_cli_state(const char* filename, const char* map_path,
                   float lr, float dr, float eps_decay,
                   int sucess_window_size, float sucess_treshold,
                   DecayType decay_type,
                   bool useDistanceRewardShaping) // <-- novo param
{
    if (!filename) return -1;
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "[WARN] failed to open %s for writing: %s\n", filename, strerror(errno));
        return -1;
    }

    fprintf(f, "map_path=%s\n", map_path ? map_path : "");
    fprintf(f, "lr=%g\n", (double)lr);
    fprintf(f, "dr=%g\n", (double)dr);
    fprintf(f, "eps_decay=%g\n", (double)eps_decay);
    fprintf(f, "sucess_window_size=%d\n", sucess_window_size);
    fprintf(f, "sucess_treshold=%g\n", (double)sucess_treshold);
    fprintf(f, "decay_type=%d\n", (int)decay_type);
    fprintf(f, "useDistanceRewardShaping=%d\n", useDistanceRewardShaping ? 1 : 0); // <-- novo

    fflush(f);
    fclose(f);
    return 0;
}

int read_cli_state(const char* filename, char** out_map_path,
                   float* out_lr, float* out_dr, float* out_eps_decay,
                   int* out_sucess_window_size, float* out_sucess_treshold,
                   DecayType* out_decay_type,
                   bool* out_useDistanceRewardShaping) // <-- novo
{
    if (!filename) return -1;
    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        size_t l = strlen(line);
        if (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = '\0';

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line;
        char *val = eq + 1;

        if (strcmp(key, "map_path") == 0) {
            if (out_map_path) {
                free(*out_map_path);
                if (val && val[0] != '\0') *out_map_path = strdup(val);
                else *out_map_path = NULL;
            }
        } else if (strcmp(key, "lr") == 0) {
            if (out_lr) *out_lr = strtof(val, NULL);
        } else if (strcmp(key, "dr") == 0) {
            if (out_dr) *out_dr = strtof(val, NULL);
        } else if (strcmp(key, "eps_decay") == 0) {
            if (out_eps_decay) *out_eps_decay = strtof(val, NULL);
        } else if (strcmp(key, "sucess_window_size") == 0) {
            if (out_sucess_window_size) *out_sucess_window_size = (int)strtol(val, NULL, 10);
        } else if (strcmp(key, "sucess_treshold") == 0) {
            if (out_sucess_treshold) *out_sucess_treshold = strtof(val, NULL);
        } else if (strcmp(key, "decay_type") == 0) {
            if (out_decay_type) {
                int dt = (int)strtol(val, NULL, 10);
                if (dt == DECAY_TYPE_LINEAR || dt == DECAY_TYPE_EXP) *out_decay_type = (DecayType)dt;
            }
        } else if (strcmp(key, "useDistanceRewardShaping") == 0) { // <-- novo
            if (out_useDistanceRewardShaping) *out_useDistanceRewardShaping = (atoi(val) != 0);
        }
    }
    fclose(f);
    return 0;
}

inline float manhatan_distance(state_t s1, state_t s2) {
	return abs(s1.x - s2.x) + abs(s1.y - s2.y);
}

Agent* run_training(MazeEnv* ir,const char* map_path, float lr, float dr, double eps_decay,int sucess_window_size, float sucess_treshold, 
				  DecayType current_decay_type, bool useDistanceRewardShaping) {

	bool stop_train = false;
	char flag_char = '\0';
	char temp_flag = '\0';
	float next_best_sucess_rate = 0.0f;
	state_t goal_state = {0,0};
    Agent* agent = newAgent(ir, lr, dr, eps_decay, (unsigned long)time(NULL));
    size_t MAX_STEPS_PER_EPISODE = ACTION_N_ACTIONS*(ir->cols*ir->rows);
    size_t current_episode = 0;
	int32_t start_to_goal_distance = 0;
	reward_t goal_reward = 0.0f;
    int* success_history = calloc(sucess_window_size,sizeof(int));
    for(int i=0;i<sucess_window_size;i++) success_history[i]=0;
    int success_index = 0;

	if(useDistanceRewardShaping) {
		cellId c = getFirstMatchingCell(ir,GRID_AGENT_GOAL);
		goal_state.x = c.col; goal_state.y = c.row;
		int32_t start_to_goal_distance = manhatan_distance(goal_state,agent->agent_start);
		goal_reward = getCellReward(ir,GRID_AGENT_GOAL);
	}

    printf("Maze loaded: rows=%zu cols=%zu\n", ir->rows, ir->cols);
    printf("Agent start: x=%u y=%u\n", (unsigned)agent->agent_start.x, (unsigned)agent->agent_start.y);

    while(current_episode < SIZE_MAX && !stop_train){
        agentRestart(agent);
        size_t steps_taken = 0;
        bool reached_goal = false;

        for(int step = 0; step < (int)MAX_STEPS_PER_EPISODE; step++){
            agentPolicy(agent,ir);
            state_t next = GetNextState(agent->current_s,agent->policy_action);
            stepResult sr = stepIntoState(ir,next);
			
			state_t trans_state = sr.invalidNext ? agent->current_s : next;

			if (useDistanceRewardShaping) {
				float phi_s  = (float)manhatan_distance(agent->current_s, goal_state);
				float phi_sp = (float)manhatan_distance(trans_state, goal_state);
				float shaping = dr * (phi_s - phi_sp); 
				sr.reward += shaping;
			}

            agentQtableUpdate(agent,trans_state,sr);
            agent->accum_reward += sr.reward;
            steps_taken++;

            if(sr.isGoal) {
                reached_goal = true;
                break;
            }
            if(sr.terminal) break;

            agentUpdateState(agent,trans_state);
        }

        success_history[success_index] = reached_goal ? 1 : 0;
        success_index = (success_index + 1) % sucess_window_size;

        int sum = 0;
        for(int i=0; i<sucess_window_size; i++) sum += success_history[i];
        float success_rate = (float)sum / sucess_window_size;

        if( success_rate >= next_best_sucess_rate || flag_char == 'v') {
            printf("Episode %zu end | steps=%zu accum_reward=%.2f | epsilon=%.2f | success_rate=%.2f\n",
                current_episode, steps_taken, (double)agent->accum_reward,agent->epsilon,success_rate);
			next_best_sucess_rate += NEXT_BEST_RATE_INCREMENT;
        }

        if(success_rate >= sucess_treshold) {
            printf("Policy converged with %.1f%% success.\n", success_rate*100);
            break;
        }
		
		if(kbhit()){
			switch (temp_flag = _getch())
			{
				case 'q': stop_train = true; break;
				case 'h': agent->epsilon = agent->epsilon * 0.5f; break;
				case 'g': agent->epsilon = agent->epsilon * 2.0f; break;
				default: break;
			}
			fflush(stdin);
		}
		
		flag_char = temp_flag == flag_char ? '\0' : temp_flag;
		temp_flag = '\0';

        agentEpsilonDecay(agent,decayTypeToFn[current_decay_type]);
        current_episode++;
    }

    printf("Training finished after %zu episodes.\n", current_episode);

    // Simula melhor trajetória (epsilon=0)
    if(!stop_train){
		printf("SIMULATING THE BEST TRAJECTORY FOUND\n");
    	agent->epsilon = 0.0f;
    	agentRestart(agent);
    	for(int step=0;step < MAX_STEPS_PER_EPISODE;step++){
			if(kbhit() && (temp_flag = _getch()) == 'q') break;
    	    agentPolicy(agent,ir);
    	    state_t before = agent->current_s;
    	    state_t next = GetNextState(agent->current_s,agent->policy_action);
    	    stepResult sr = stepIntoState(ir,next);
    	    printf("(%u) (%u,%u) -> (%u,%u) %s\n",step,before.x,before.y,next.x,next.y,
    	        sr.isGoal ? "[GOAL TARGET]" : "");
    	    if(sr.terminal) break;
    	    agentUpdateState(agent,next);
    	}
	}
	
	free(success_history);
    
	return agent;
}

void handle_interrrupt(int signal){
	printf("[ERROR] INTERRUPT EVENT EXIT\n");
	exit(1);
};

void prompt_training_params(double sugested_epsilon_decay_exp,double sugested_epsilon_decay_linear,
                            float* out_lr, float* out_dr, float* out_eps_decay,
                            int* sucess_window_size, float* sucess_treshold,
                            DecayType* out_decay_type,
                            bool* out_useDistanceRewardShaping) // <-- novo
{
    printf("Train Parameters (enter => current value):\n");
    read_float(" Learning rate", out_lr, *out_lr);
    read_float(" Discount factor (gamma)", out_dr, *out_dr);

    int dt_default = (int)(*out_decay_type);
    printf(" Decay type options:\n");
    printf(" 0 = %s\n", decayTypeToStr[DECAY_TYPE_LINEAR]);
    printf(" 1 = %s\n", decayTypeToStr[DECAY_TYPE_EXP]);
    int dt_choice = dt_default;
    read_int(" Decay type (enter 0 or 1)", &dt_choice, dt_default);
    if (dt_choice == DECAY_TYPE_LINEAR || dt_choice == DECAY_TYPE_EXP) {
        *out_decay_type = (DecayType)dt_choice;
        printf("Selected: %s\n",decayTypeToStr[*out_decay_type]);
    } else {
        printf(" Invalid decay type selected; keeping current (%s)\n", decayTypeToStr[*out_decay_type]);
    }

    read_float(" Epsilon decay", out_eps_decay, *out_decay_type == DECAY_TYPE_LINEAR ? sugested_epsilon_decay_linear : sugested_epsilon_decay_exp);
    
    read_int(" Sucess Window Size",sucess_window_size,*sucess_window_size);
    read_float(" Sucess treshold",sucess_treshold,*sucess_treshold);

    int shaping_int = *out_useDistanceRewardShaping ? 1 : 0;
    read_int(" Use distance reward shaping (0/1)", &shaping_int, shaping_int); // <-- novo
    *out_useDistanceRewardShaping = (shaping_int != 0);
}

int main(int argc, char** argv){
	signal(SIGINT,handle_interrrupt);
    char *map_path = NULL;
    float lr = 0.6f, dr = 0.9f, eps_decay = 0.99;
	MazeEnv ir = {0};
	Agent* current_agent = NULL;
	int sucess_window_size = SUCCESS_WINDOW;
	float sucess_treshold = SUCCESS_THRESHOLD;
	DecayType current_decay_type = DECAY_TYPE_EXP;
	bool useDistanceRewardShaping = false;

	read_cli_state(AGENT_CLI_STATE_FILE, &map_path, &lr, &dr, &eps_decay, &sucess_window_size, &sucess_treshold,&current_decay_type,&useDistanceRewardShaping);
	if(map_path){
		if(readMazeNumpy(map_path,&ir) == -1 && readMazeRaw(map_path,&ir) == -1){
			free(map_path);
			printf("Invalid Maze Selected\n");	
		}
	}

    while(1){
		printf("\nMaze: %s\n",map_path);

		printf("learning rate = %.2f, discount factor = %.2f, epsilon decay = %.2e\n",lr, dr, eps_decay);
			
		printf("decay type = %s\n",decayTypeToStr[current_decay_type]);
		printf("sucess window size = %u, sucess treshold = %.2f\n",sucess_window_size,sucess_treshold);
        
		printf("\n=== MENU ===\n\n");
        
		printf("1) Select Map\n");
		printf("2) Define parameters\n");
        printf("3) Start training\n");
        printf("4) Save trained model\n");
        printf("5) Exit\n");
        
		printf("\nOpt: ");
        
		int choice = 0;
        if(scanf("%d",&choice) != 1){
            while(getchar()!='\n');
            continue;
        }
        while(getchar()!='\n');

        if(choice == 3){
			if(map_path){
				if(current_agent){
					free(current_agent->q_table.vals);
        			free(current_agent);
					current_agent = NULL;
				}
				current_agent = run_training(&ir,map_path, lr, dr, eps_decay,sucess_window_size, sucess_treshold,current_decay_type,useDistanceRewardShaping);
			} else {
				printf("Select an valid map path\n");
			}
        } else if(choice == 4){
			if(current_agent){
				char* save_path = windows_open_file_dialog(qtable_filter,NULL,true);
				if(agentSaveQtable(current_agent,save_path) == 0){
					printf("[INFO] Agent Q table Saved\n");
				}
			} else {
				printf("[ERROR] No trained agent to be saved\n");
			}
        } else if(choice == 1){
		
			#ifdef _WIN32
				free(map_path);
				map_path = windows_open_file_dialog(maze_filter, NULL,false);

			    if(readMazeNumpy(map_path,&ir) == -1) 
				if(readMazeRaw(map_path,&ir) == -1) free(map_path);

			#else
				free(map_path);
				map_path = prompt_path_via_stdin();
			#endif
			
			if(map_path){
            	save_cli_state(AGENT_CLI_STATE_FILE, map_path, lr, dr, eps_decay, sucess_window_size, sucess_treshold,current_decay_type,useDistanceRewardShaping);
			} else {
				printf("\n[ERROR] No Maze Given\n");
			}

        } else if (choice == 2){
			double suggested_decay_exp,suggested_decay_linear;
			suggested_decay_exp = eps_decay;
			suggested_decay_linear = eps_decay;
			if(map_path){
				size_t adjust = countAllMatchingCells(&ir,GRID_WALL) + countAllMatchingCells(&ir,GRID_OPEN);
				adjust = adjust/2;
				suggested_decay_linear = 1.0f/((float)ACTION_N_ACTIONS*(ir.cols*ir.rows)*(adjust));
			}
			// TODO: ENHANCE THE SUGESTED SUGESTED DECAY FOR EXPONENCIAL DECAY
			// suggested_decay_exp = 0.9999f;
			prompt_training_params(suggested_decay_exp,suggested_decay_linear,&lr,&dr,&eps_decay,&sucess_window_size,&sucess_treshold,&current_decay_type,&useDistanceRewardShaping);
			save_cli_state(AGENT_CLI_STATE_FILE, map_path, lr, dr, eps_decay, sucess_window_size, sucess_treshold,current_decay_type,useDistanceRewardShaping);
		}

		else if(choice == 5){
            break;
        } 
		else {
            printf("Escolha invalida.\n");
        }
    }

    free(map_path);
    return 0;
}