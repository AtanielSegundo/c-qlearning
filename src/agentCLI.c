#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

// #define FLAG_IMPLEMENTATION
// #include "flag.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"

typedef struct {int8_t dx; int8_t dy;} action_delta_t;
typedef struct {uint8_t x; uint8_t y;}   state_t;

typedef enum {
	ACTION_LEFT,
	ACTION_RIGHT,
	ACTION_UP,
	ACTION_DOWN,
	ACTION_N_ACTIONS,
	ACTION_NONE		// USED TO SEE IF THE ACTION WAS BEING POLICY CHOSED
} Action;

static action_delta_t actionToDeltaMap[] = {
	[ACTION_LEFT]  = (action_delta_t){-1,0},
	[ACTION_RIGHT] = (action_delta_t){1,0},
	[ACTION_UP]    = (action_delta_t){0,1},
	[ACTION_DOWN]  = (action_delta_t){0,-1}
}; 

typedef float reward_t;
typedef reward_t q_val_t;
 
static reward_t GridTypeToReward[] = {
	[GRID_OPEN]        = 0.0f,
	[GRID_WALL]        = -1.0f,
	[GRID_AGENT_GOAL]  = 1.0f,
	[GRID_AGENT_START] = 0.0f
};

typedef MazeInternalRepr MazeEnv;

typedef struct {
	reward_t reward;
	bool terminal;
	bool isGoal;
} stepResult;


stepResult stepIntoState(MazeEnv* e,state_t s){
	stepResult sr = {0};
	if ((size_t)s.x >= e->cols || (size_t)s.y >= e->rows) {
		sr.terminal = true;
		sr.isGoal = false;
		sr.reward = GridTypeToReward[GRID_WALL];
	} else {
		GridCellType cell_type = (GridCellType)getCell(e,s.y,s.x);
		sr.isGoal   = (cell_type == GRID_AGENT_GOAL);
		sr.terminal = sr.isGoal;
		sr.reward 	= GridTypeToReward[cell_type];
	}	
	return sr;
};

typedef struct {
	size_t len_state_x;
	size_t len_state_y;
	size_t len_state_actions;
	q_val_t* vals;
} q_table_t;

typedef struct {
	// MUTABLE STATE 
	q_table_t q_table;
	state_t current_s;
	Action	policy_action;
	float epsilon;
	// STARTING PARAMETERS
	state_t agent_start;
	float accum_reward;
	// HYPER PARAMETERS
	float learning_rate;
	float discount_rate;
	float epsilon_decay;
	time_t seed;

} Agent;

state_t getFirstMatchingCell(MazeEnv *m,GridCellType t){
	for(size_t row=0; row < m->rows; row++)
		for(size_t col=0; col < m->cols; col++){
			if(getCell(m,col,row) == t) return (state_t){(uint8_t)col,(uint8_t)row};
		}
	return (state_t){0,0};
}

void agentSetSeed(Agent* self,unsigned int seed){
	self->seed = seed;
	srand((unsigned int)self->seed);
};

Agent* newAgent(MazeEnv *env,float lr,float dr,float eps_decay, unsigned long seed){
	Agent* ag = (Agent*)calloc(1,sizeof(Agent));
	ag->learning_rate = lr;
	ag->discount_rate = dr;
	ag->epsilon_decay = eps_decay;
	ag->epsilon = 1.0f;
	
	agentSetSeed(ag,(unsigned int)seed);
	
	ag->policy_action = ACTION_NONE;
	ag->current_s = getFirstMatchingCell(env,GRID_AGENT_START);
	ag->agent_start = ag->current_s;
	ag->accum_reward = 0.0f;
	
	ag->q_table = (q_table_t){.len_state_x=env->cols,
							  .len_state_y=env->rows,
							  .len_state_actions=ACTION_N_ACTIONS,
							  .vals = NULL
							 };
	ag->q_table.vals = (q_val_t*)calloc((env->cols*env->rows)*ACTION_N_ACTIONS,sizeof(q_val_t));
	
	return ag;
};

q_val_t getQtableValue(Agent* self,state_t s,Action a){
	size_t n_actions = self->q_table.len_state_actions;
	size_t n_x = self->q_table.len_state_x;
	size_t n_y = self->q_table.len_state_y;
	if ((size_t)s.x >= n_x || (size_t)s.y >= n_y || (size_t)a >= n_actions) return 0.0f;
	return self->q_table.vals[n_actions*(((size_t)s.y*n_x)+s.x) + a];
}

void setQtableValue(Agent* self,state_t s,Action a, q_val_t q){
	size_t n_actions = self->q_table.len_state_actions;
	size_t n_x = self->q_table.len_state_x;
	size_t n_y = self->q_table.len_state_y;
	if ((size_t)s.x >= n_x || (size_t)s.y >= n_y || (size_t)a >= n_actions) return;
	self->q_table.vals[n_actions*(((size_t)s.y*n_x)+s.x) + a] = q;
}
typedef struct {q_val_t v; Action a} ValAction;
ValAction qtableMaxValAction(Agent* a,state_t s){
	float max_qval_t  = -INFINITY;
	Action max_action = ACTION_NONE; 
	for(int i = 0; i < ACTION_N_ACTIONS; i++){
		q_val_t q = getQtableValue(a,s,i);
		if(q > max_qval_t) {
			max_qval_t = q;
			max_action = (Action)i;
		}
	}
	return (ValAction){.v=max_qval_t,.a=max_action};
};

void agentRestart(Agent* self){
	self->current_s = self->agent_start;
	self->policy_action = ACTION_NONE;
	self->accum_reward = 0.0f;
};

void agentPolicy(MazeEnv* env, Agent* self){
	float r = (float)rand()/RAND_MAX;
	if(r > self->epsilon){
		self->policy_action = qtableMaxValAction(self,self->current_s).a;
	} else{
		self->policy_action = (Action)(rand() % ACTION_N_ACTIONS);
	}
};

state_t GetNextState(state_t s, Action a){
	action_delta_t da = actionToDeltaMap[a];
	int nx = (int)s.x + (int)da.dx;
	int ny = (int)s.y + (int)da.dy;
	return (state_t){(uint8_t)nx,(uint8_t)ny};
};

void agentUpdateState(Agent* a, state_t new_state){
	a->current_s = new_state;
	a->policy_action = ACTION_NONE;
};

void agentQtableUpdate(Agent* self,state_t next,stepResult sr){
	q_val_t old_q_trace = (1-self->learning_rate)*getQtableValue(self,self->current_s,self->policy_action);
	q_val_t TD = 0.0;
	if(sr.terminal == true){
		TD = self->learning_rate*sr.reward;
	} else{
		q_val_t max_next_q_val = qtableMaxValAction(self,next).v;
		TD = self->learning_rate*(sr.reward+self->discount_rate*max_next_q_val);	
	}
	q_val_t new_q = old_q_trace + TD;
	setQtableValue(self,self->current_s,self->policy_action,new_q);
}

void agentEpsilonDecay(Agent* self){
	self->epsilon -= self->epsilon_decay;  
	if(self->epsilon < 0.0f) self->epsilon = 0.0f;
};


#ifdef _WIN32
char* windows_open_file_dialog(const char* filter, const char* initialDir) {
    OPENFILENAMEA ofn;
    char *fileName = (char*)malloc(4096);
    if (!fileName) return NULL;
    fileName[0] = '\0';

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter; // exemplo: "Maze Files\0*.npy\0*.maze\0All\0*.*\0"
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = 4096;
    ofn.lpstrInitialDir = initialDir;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

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

void prompt_training_params(float* out_lr, float* out_dr, float* out_eps_decay){
    printf("Parametros de treinamento (enter para usar default entre parenteses):\n");
    printf(" Learning rate (default 0.6): ");
    if(scanf("%f", out_lr) != 1) { *out_lr = 0.6f; while(getchar()!='\n'); }
    printf(" Discount factor (gamma) (default 0.9): ");
    if(scanf("%f", out_dr) != 1) { *out_dr = 0.9f; while(getchar()!='\n'); }
    printf(" Epsilon decay (default 1e-4): ");
    if(scanf("%f", out_eps_decay) != 1) { *out_eps_decay = 1e-4f; while(getchar()!='\n'); }
    // flush newline
    while(getchar()!='\n');
}

/* ---------- training runner (extrai main loop para chamar a partir do menu) ---------- */

#define PRINT_EPISODE_EVERY 100
#define SUCCESS_WINDOW 100
#define SUCCESS_THRESHOLD 0.8f

void run_training(const char* map_path, float lr, float dr, float eps_decay) {
    MazeEnv ir = {0};
    if(readMazeNumpy(map_path,&ir) == -1) 
	if(readMazeRaw(map_path,&ir) == -1) return;

    Agent* agent = newAgent(&ir, lr, dr, eps_decay, (unsigned long)time(NULL));
    size_t MAX_STEPS_PER_EPISODE = ACTION_N_ACTIONS*(ir.cols*ir.rows);
    size_t current_episode = 0;

    int success_history[SUCCESS_WINDOW];
    for(int i=0;i<SUCCESS_WINDOW;i++) success_history[i]=0;
    int success_index = 0;

    printf("Maze loaded: rows=%zu cols=%zu\n", ir.rows, ir.cols);
    printf("Agent start: x=%u y=%u\n", (unsigned)agent->agent_start.x, (unsigned)agent->agent_start.y);

    while(current_episode < SIZE_MAX){
        agentRestart(agent);
        size_t steps_taken = 0;
        bool reached_goal = false;

        for(int step = 0; step < (int)MAX_STEPS_PER_EPISODE; step++){
            agentPolicy(&ir,agent);
            state_t next = GetNextState(agent->current_s,agent->policy_action);
            stepResult sr = stepIntoState(&ir,next);

            agentQtableUpdate(agent,next,sr);
            agent->accum_reward += sr.reward;
            steps_taken++;

            if(sr.isGoal) {
                reached_goal = true;
                break;
            }
            if(sr.terminal) break;

            agentUpdateState(agent,next);
        }

        success_history[success_index] = reached_goal ? 1 : 0;
        success_index = (success_index + 1) % SUCCESS_WINDOW;

        int sum = 0;
        for(int i=0; i<SUCCESS_WINDOW; i++) sum += success_history[i];
        float success_rate = (float)sum / SUCCESS_WINDOW;

        if(current_episode % PRINT_EPISODE_EVERY == 0 || reached_goal) {
            printf("Episode %zu end | steps=%zu accumulated_reward=%.2f | success_rate=%.2f\n",
                current_episode, steps_taken, (double)agent->accum_reward, success_rate);
        }

        if(success_rate >= SUCCESS_THRESHOLD) {
            printf("Policy converged with %.1f%% success.\n", success_rate*100);
            break;
        }

        agentEpsilonDecay(agent);
        current_episode++;
    }

    printf("Training finished after %zu episodes.\n", current_episode);

    // Simula melhor trajetória (epsilon=0)
    printf("SIMULATING THE BEST TRAJECTORY FOUND\n");
    agent->epsilon = 0.0f;
    agentRestart(agent);
    for(int step=0;step < MAX_STEPS_PER_EPISODE;step++){
        agentPolicy(&ir,agent);
        state_t before = agent->current_s;
        state_t next = GetNextState(agent->current_s,agent->policy_action);
        stepResult sr = stepIntoState(&ir,next);
        printf("(%u) (%u,%u) -> (%u,%u) %s\n",step,before.x,before.y,next.x,next.y,
            sr.isGoal ? "[GOAL TARGET]" : "");
        if(sr.terminal) break;
        agentUpdateState(agent,next);
    }

    // cleanup
    if(agent){
        free(agent->q_table.vals);
        free(agent);
    }
}

/* ---------- main CLI loop ---------- */

#ifdef _WIN32
    const char* filter =
    "Maze Files (*.npy;*.maze)\0*.npy;*.maze\0"
    "All Files (*.*)\0*.*\0"
    "\0";
#endif

int main(int argc, char** argv){
    char *map_path = NULL;
    float lr = 0.6f, dr = 0.9f, eps_decay = 1e-4f;
	size_t sucess_window_size = SUCCESS_WINDOW;
	size_t sucess_treshold = SUCCESS_THRESHOLD;
	
    // menu
    while(1){
		printf("\nMaze: %s\n",map_path);
		printf("learning rate = %.2f, discount factor = %.2f, epsilon decay = %.2e\n",lr, dr, eps_decay);
        
		printf("\n=== MENU ===\n\n");
        
		printf("1) Select Map\n");
		printf("2) Define parameters\n");
        printf("3) Start training\n");
        printf("4) Vizualize trained model\n");
        printf("5) Exit\n");
        
		printf("\n Opt: ");
        
		int choice = 0;
        if(scanf("%d",&choice) != 1){
            while(getchar()!='\n');
            continue;
        }
        while(getchar()!='\n');

        if(choice == 3){
            run_training(map_path, lr, dr, eps_decay);
        } else if(choice == 4){
            printf("[OPCAO] Visualizar modelo treinado selecionada. (Placeholder — implemente você aqui.)\n");
            
        } else if(choice == 1){
		
			#ifdef _WIN32
				free(map_path);
				map_path = windows_open_file_dialog(filter, NULL);
			#else
				free(map_path);
				map_path = prompt_path_via_stdin();
			#endif
			
			if(!map_path){
				printf("\nNenhum mapa fornecido\n");
			}
        } else if (choice == 2){
			prompt_training_params(&lr,&dr,&eps_decay);
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