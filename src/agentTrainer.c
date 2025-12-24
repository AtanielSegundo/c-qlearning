#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#define AGENT_IMPLEMENTATION
#include "agent.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"

#include "argparse.h"

typedef struct
{
	reward_t* rewards_acumm;
    size_t*   cummulative_goals;   // Positive Integers
    double*   succes_rate;
    double*   training_loss;
    size_t*   episode_step_count;  // Positive Integers
    bool*     goal_reached;
	size_t    num_of_episodes;
} TrainMetrics;

typedef struct
{
    // NEEDED PARAMETERS
    char*  maze_file;
    // OPTIONAL PARAMETERS
    char*  qtable_save_path;
    char*  metrics_save_path; 
    float  learning_rate  ;
    float  discount_factor;
    float  epsilon_decay  ;
    size_t num_episodes   ;
    size_t max_steps      ;
    unsigned long seed;
    bool   distance_reward_shaping;
    bool   block_transpassing_walls;
} ArgParameters;

static ArgParameters ARG_PARAMS = {
    .maze_file               = NULL,
    .qtable_save_path        = NULL,
    .metrics_save_path       = NULL,
    .learning_rate           = 5e-4,
    .discount_factor         = 0.99,
    .epsilon_decay           = 37001.0,
    .num_episodes            = 200,
    .max_steps               = 856,
    .seed                    = 67,
    .distance_reward_shaping = false,
    .block_transpassing_walls = true 
};

inline float manhatan_distance(state_t s1, state_t s2) {
	return abs(s1.x - s2.x) + abs(s1.y - s2.y);
}

float huber_loss(float x)
{
    const float delta = 1.0f;
    float ax = fabsf(x);
    if (ax <= delta)
        return 0.5f * x * x;
    else
        return delta * (ax - 0.5f * delta);
}


void debug_arg_parameters();
void parse_cmd_arguments(int argc ,char** argv);
TrainMetrics* alloc_train_metrics(size_t num_episodes);
void save_metrics_csv(const char* path, const TrainMetrics* m);

const int ROLLING_WINDOW_SIZE = 20;
const int LOG_EVERY_EPISODES = 10;

int main(int argc ,char** argv)
{
    parse_cmd_arguments(argc,argv);
    debug_arg_parameters();

    MazeEnv ir = {0};
    if (ARG_PARAMS.maze_file) {
        if (readMazeNumpy(ARG_PARAMS.maze_file,&ir) == -1 &&
            readMazeRaw(ARG_PARAMS.maze_file,&ir)   == -1) {
            printf("[ERROR] Invalid Maze File: %s\n", ARG_PARAMS.maze_file);
            exit(-1);
        }
    }

    TrainMetrics* metrics = alloc_train_metrics(ARG_PARAMS.num_episodes);
    Agent* agent = newAgent(&ir,
                            1.0f - ARG_PARAMS.learning_rate,
                            ARG_PARAMS.discount_factor,
                            ARG_PARAMS.epsilon_decay,
                            ARG_PARAMS.seed);

    state_t goal_state = {0,0};
    cellId c = getFirstMatchingCell(&ir, GRID_AGENT_GOAL);
    goal_state.x = c.col;
    goal_state.y = c.row;

    size_t wallsCount = countAllMatchingCells(&ir, GRID_WALL);
    size_t opensCount = countAllMatchingCells(&ir, GRID_OPEN);

    printf("[INFO] Maze loaded:\trows=%zu cols=%zu\n", ir.rows, ir.cols);
    printf("[INFO] Agent start:\tx=%u y=%u\n",
           (unsigned)agent->agent_start.x,
           (unsigned)agent->agent_start.y);

    unsigned int goals_count = 0;
    unsigned int total_training_steps = 0;

    for (int episode = 0; episode < ARG_PARAMS.num_episodes; episode++) {

        agentRestart(agent);

        size_t steps_taken_episode = 0;
        bool goal_reached = false;
        reward_t total_episode_reward = 0.0f;
        float model_loss = 0.0f;

        for (int step = 0; step < ARG_PARAMS.max_steps; step++) {

            agentPolicy(agent, &ir);

            state_t next = GetNextState(agent->current_s, agent->policy_action);
            stepResult sr = stepIntoStateUnscaled(&ir, next, wallsCount, opensCount);

            state_t trans_state;
            if (ARG_PARAMS.block_transpassing_walls)
                trans_state = sr.invalidNext ? agent->current_s : next;
            else
                trans_state = next;

            if (ARG_PARAMS.distance_reward_shaping && !sr.invalidNext) {
                float phi_s  = (float)manhatan_distance(agent->current_s, goal_state);
                float phi_sp = (float)manhatan_distance(trans_state, goal_state);
                sr.reward += ARG_PARAMS.discount_factor * (phi_s - phi_sp);
            }

            float td_error = agentQtableUpdate(agent, trans_state, sr);
            model_loss += huber_loss(td_error);

            total_episode_reward += sr.reward;
            steps_taken_episode++;

            if (sr.isGoal) {
                goal_reached = true;
                goals_count++;
                break;
            }

            if (sr.terminal) break;

            agentUpdateState(agent, trans_state);
        }

        total_training_steps += steps_taken_episode;

        /* -------- epsilon update (safe) -------- */
        const double eps_final = 0.1;
        const double eps_start = 1.0;
        double decay = ARG_PARAMS.epsilon_decay > 0.0
                       ? ARG_PARAMS.epsilon_decay
                       : 1.0;

        double eps = eps_final +
                     (eps_start - eps_final) *
                     exp(-(double)total_training_steps / decay);

        if (!isfinite(eps)) eps = eps_final;
        if (eps < eps_final) eps = eps_final;
        if (eps > 1.0) eps = 1.0;

        agent->epsilon = (float)eps;

        /* -------- success rate (rolling window) -------- */
        metrics->goal_reached[episode] = goal_reached;

        int start = episode - ROLLING_WINDOW_SIZE + 1;
        if (start < 0) start = 0;

        int window_goals = 0;
        int window_len = episode - start + 1;

        for (int i = start; i <= episode; i++)
            window_goals += metrics->goal_reached[i];

        double success_rate = 100.0 * (double)window_goals / (double)window_len;
        metrics->succes_rate[episode] = success_rate;

        /* -------- store metrics -------- */
        metrics->rewards_acumm[episode]      = total_episode_reward;
        metrics->cummulative_goals[episode]  = goals_count;
        metrics->training_loss[episode]      = model_loss;
        metrics->episode_step_count[episode] = steps_taken_episode;

        /* -------- logging -------- */
        if (episode % LOG_EVERY_EPISODES == 0 ||
            episode == ARG_PARAMS.num_episodes - 1) {

            printf(
                "[EP %4d] steps=%4zu | reward=%9.3f | loss=%9.3e | "
                "eps=%5.3f | goal=%d | SR=%6.2f%%\n",
                episode,
                steps_taken_episode,
                total_episode_reward,
                model_loss,
                agent->epsilon,
                goal_reached,
                success_rate
            );
        }
    }

    

    /* ================== GREEDY EVALUATION RUN ================== */

    printf("\n[INFO] Starting greedy rollout (epsilon = 0)\n");

    agent->epsilon = 0.0f;
    agentRestart(agent);

    /* visited map for loop detection */
    bool visited[ir.rows][ir.cols];
    memset(visited, 0, sizeof(visited));

    printf("PATH:\n");

    for (int step = 0; step < ARG_PARAMS.max_steps; step++) {

        state_t s = agent->current_s;

        printf("(%d,%d)\n", s.x, s.y);

        /* loop detection */
        if (visited[s.y][s.x]) {
            printf("[STOP] Loop detected at (%d,%d)\n", s.x, s.y);
            break;
        }
        visited[s.y][s.x] = true;

        agentPolicy(agent, &ir);

        state_t next = GetNextState(s, agent->policy_action);
        stepResult sr = stepIntoState(&ir, next, wallsCount, opensCount);

        state_t trans_state;
        if (ARG_PARAMS.block_transpassing_walls)
            trans_state = sr.invalidNext ? s : next;
        else
            trans_state = next;

        if (sr.isGoal) {
            printf("(%d,%d)\n", trans_state.x, trans_state.y);
            printf("[SUCCESS] Goal reached in %d steps\n", step + 1);
            break;
        }

        if (sr.terminal) {
            printf("[STOP] Terminal state reached\n");
            break;
        }

        agentUpdateState(agent, trans_state);

        if (step == ARG_PARAMS.max_steps - 1) {
            printf("[STOP] Max steps reached\n");
        }
    }
    
    // SAVING AGENT QTABLE
    if(ARG_PARAMS.qtable_save_path){
        agentSaveQtable(agent,ARG_PARAMS.qtable_save_path);
    }

    if(ARG_PARAMS.metrics_save_path) {
        save_metrics_csv(ARG_PARAMS.metrics_save_path, metrics);
    }


}


TrainMetrics* alloc_train_metrics(size_t num_episodes){
    TrainMetrics* train_metrics = (TrainMetrics*)calloc(1,sizeof(TrainMetrics));
    train_metrics->num_of_episodes = num_episodes;
    train_metrics->goal_reached = (bool*)calloc(num_episodes,sizeof(bool));
    train_metrics->rewards_acumm = (reward_t*)calloc(num_episodes,sizeof(reward_t));
    train_metrics->cummulative_goals =  (size_t*)calloc(num_episodes,sizeof(size_t));
    train_metrics->succes_rate =  (double*)calloc(num_episodes,sizeof(double));
    train_metrics->training_loss =  (double*)calloc(num_episodes,sizeof(double));
    train_metrics->episode_step_count =  (size_t*)calloc(num_episodes,sizeof(size_t));
    return train_metrics;
}

void parse_cmd_arguments(int argc ,char** argv){
    argument_parser_t parser;
    argparse_init(&parser, argc, argv, "Q-learning Maze Solcing Agent Trainer", NULL);

    argparse_arg_t arg_maze = ARGPARSE_POSITIONAL(
        STRING, "maze", &ARG_PARAMS.maze_file, "Path to maze to solve"
    );
    argparse_arg_t arg_lr           = ARGPARSE_OPTION(
        FLOAT, 'a', "--lr", &ARG_PARAMS.learning_rate, "Agent learning rate"
    );
    argparse_arg_t arg_df           = ARGPARSE_OPTION(
        FLOAT, 'g', "--df", &ARG_PARAMS.discount_factor, "Agent TD discount factor"
    );
    argparse_arg_t arg_eps_decay    = ARGPARSE_OPTION(
        FLOAT, 'd', "--decay", &ARG_PARAMS.epsilon_decay, "Agent epsilon decay"
    );
    argparse_arg_t arg_num_episodes = ARGPARSE_OPTION(
        INT, 'e', "--episodes", &ARG_PARAMS.num_episodes, "Agent training number of episodes"
    );
    argparse_arg_t arg_max_steps    = ARGPARSE_OPTION(
        INT, 's', "--max_steps", &ARG_PARAMS.max_steps, "Agent training number of max steps per episode"
    );
    argparse_arg_t arg_reward_shaping  = ARGPARSE_FLAG_TRUE(
        NO_FLAG, "--use_distance_shaping", &ARG_PARAMS.distance_reward_shaping, "Agent training uses distance to goal reward shaping"
    );
    argparse_arg_t arg_block_transpassing = ARGPARSE_FLAG_FALSE(
        NO_FLAG, "--enable_transpasing", &ARG_PARAMS.block_transpassing_walls, "Agent training enables walls transpassing"
    );
    argparse_arg_t arg_seed         = ARGPARSE_OPTION(
        INT, NO_FLAG, "--seed", &ARG_PARAMS.seed, "Agent training seed for random"
    );
    argparse_arg_t arg_qtable_path  = ARGPARSE_OPTION(
        STRING, NO_FLAG, "--qtable_path", &ARG_PARAMS.qtable_save_path, "Path to save agent qtable"
    );
    argparse_arg_t arg_metrics_path = ARGPARSE_OPTION(
        STRING, NO_FLAG, "--metrics_path", &ARG_PARAMS.metrics_save_path, "Path to save agent metrics"
    );
    
    argparse_add_argument(&parser, &arg_maze);
    argparse_add_argument(&parser, &arg_lr);
    argparse_add_argument(&parser, &arg_df);
    argparse_add_argument(&parser, &arg_eps_decay);
    argparse_add_argument(&parser, &arg_num_episodes);
    argparse_add_argument(&parser, &arg_max_steps);
    argparse_add_argument(&parser, &arg_reward_shaping);
    argparse_add_argument(&parser, &arg_block_transpassing);
    argparse_add_argument(&parser, &arg_seed);
    argparse_add_argument(&parser, &arg_qtable_path);
    argparse_add_argument(&parser, &arg_metrics_path);
    
    auto error = argparse_parse_args(&parser);

    argparse_check_error_and_exit(error);
}

void debug_arg_parameters(){
    printf("ARG PARAMETERS:\n");
    printf("\tmaze_file       = %s\n"  ,ARG_PARAMS.maze_file == NULL ? "(null)" : ARG_PARAMS.maze_file);
    printf("\tlearning_rate   = %.3f\n",ARG_PARAMS.learning_rate);
    printf("\tdiscount_factor = %.3f\n",ARG_PARAMS.discount_factor);
    printf("\tepsilon_decay   = %.3f\n",ARG_PARAMS.epsilon_decay);
    printf("\tnum_episodes    = %d\n"  ,ARG_PARAMS.num_episodes);
    printf("\tmax_steps       = %d\n"  ,ARG_PARAMS.max_steps);
}

void save_metrics_csv(const char* path, const TrainMetrics* m)
{
    if (!path || !m) return;

    FILE* f = fopen(path, "w");
    if (!f) {
        printf("[ERROR] Could not open metrics file: %s\n", path);
        return;
    }

    /* CSV header */
    fprintf(f,
        "episode,"
        "reward,"
        "cumulative_goals,"
        "success_rate,"
        "training_loss,"
        "steps\n"
    );

    for (size_t i = 0; i < m->num_of_episodes; i++) {
        fprintf(
            f,
            "%zu,%.6f,%zu,%.4f,%.6e,%zu,%d\n",
            i,
            (double)m->rewards_acumm[i],
            m->cummulative_goals[i],
            m->succes_rate[i],
            m->training_loss[i],
            m->episode_step_count[i]
        );
    }

    fclose(f);
    printf("[INFO] Metrics saved to %s\n", path);
}