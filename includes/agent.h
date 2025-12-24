#ifndef AGENT_H

#include "stdint.h"
#include "mazeIR.h"
#include <float.h>

#define AGENT_H

typedef MazeInternalRepr MazeEnv;
typedef struct {int32_t dx; int32_t dy;} action_delta_t;
typedef struct {int32_t x; int32_t y;} state_t;
typedef float reward_t;
typedef reward_t q_val_t;
typedef enum {
	ACTION_LEFT,
	ACTION_RIGHT,
	ACTION_UP,
	ACTION_DOWN,
	ACTION_N_ACTIONS,
	ACTION_NONE		// USED TO SEE IF THE ACTION WAS BEING POLICY CHOSED
} Action;

typedef struct {q_val_t v; Action a;} ValAction;

typedef struct {
	reward_t reward;
	bool invalidNext;	// A transicao para o estado é invalida, ou seja o estado permanece o mesmo
	bool terminal;
	bool isGoal;
} stepResult;

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
	double epsilon_decay;
	time_t seed;
} Agent;

static action_delta_t actionToDeltaMap[] = {
	[ACTION_LEFT]  = (action_delta_t){-1,0},
	[ACTION_RIGHT] = (action_delta_t){1,0},
	[ACTION_UP]    = (action_delta_t){0,1},
	[ACTION_DOWN]  = (action_delta_t){0,-1}
}; 

static reward_t gridTypeToReward[] = {
	[GRID_OPEN]        = -.01f,
	[GRID_WALL]        = -.5f,
	[GRID_AGENT_GOAL]  = 1.0f,
	[GRID_AGENT_START] = -.1f
};

// static reward_t smallGridTypeToReward[] = {
// 	[GRID_OPEN]        = 0.0f,
// 	[GRID_WALL]        = -1.0f,
// 	[GRID_AGENT_GOAL]  = 1.0f,
// 	[GRID_AGENT_START] = -0.1f
// };

// static reward_t bigGridTypeToReward[] = {
// 	[GRID_OPEN]        = 0.0,
// 	[GRID_WALL]        = -10.0f,
// 	[GRID_AGENT_GOAL]  = 10.0f,
// 	[GRID_AGENT_START] = -0.1f
// };

typedef float(*decay_fn)(float,float);

typedef enum {
	DECAY_TYPE_LINEAR,
	DECAY_TYPE_EXP
} DecayType;


// FUNCTIONS PROTOTYPES
state_t GetNextState(state_t s, Action a);

Agent* newAgent(MazeEnv *env,float lr,float dr,double eps_decay, unsigned long seed);
void agentSetSeed(Agent* self,unsigned int seed);
void agentRestart(Agent* self);
void agentPolicy(Agent* self,MazeEnv* env);
void agentUpdateState(Agent* a, state_t new_state);
float agentQtableUpdate(Agent* self,state_t next,stepResult sr);
void agentEpsilonDecay(Agent* self,decay_fn fn);
int agentSaveQtable(Agent* agent,char* save_path);
int agentReadQtable(Agent* agent, const char* load_path);
reward_t getCellReward(MazeEnv* env,GridCellType t, size_t wallsCount, size_t opensCount);

stepResult stepIntoState(MazeEnv* e,state_t s,size_t wallsCount, size_t opensCount);
q_val_t getQtableValue(Agent* self,state_t s,Action a);
void setQtableValue(Agent* self,state_t s,Action a, q_val_t q);
ValAction qtableMaxValAction(Agent* a,state_t s);

#endif

#ifdef AGENT_IMPLEMENTATION

reward_t getCellReward(MazeEnv* env,GridCellType t, size_t wallsCount, size_t opensCount){
	reward_t normalized_r = gridTypeToReward[t];
	reward_t scaled_r = 0.0f;
	switch (t)
	{
		case GRID_AGENT_GOAL: scaled_r = opensCount*normalized_r; break;
		case GRID_WALL: scaled_r = wallsCount*normalized_r; break;
		default: scaled_r = normalized_r; break;
	}

	return scaled_r;
};

reward_t getCellRewardUnscaled(MazeEnv* env,GridCellType t, size_t wallsCount, size_t opensCount){
	reward_t normalized_r = gridTypeToReward[t];
	return normalized_r;
};


stepResult stepIntoState(MazeEnv* e,state_t s,size_t wallsCount, size_t opensCount){
	stepResult sr = {0};
	if (s.x >= e->cols || s.y >= e->rows || s.x < 0 || s.y < 0) {
		sr.isGoal = false;
		// sr.terminal default is false
		sr.reward = getCellReward(e,GRID_WALL,wallsCount,opensCount);
        sr.invalidNext = true;
	} else {
		GridCellType cell_type = (GridCellType)getCell(e,s.y,s.x);
		sr.isGoal   = (cell_type == GRID_AGENT_GOAL);
		sr.terminal = sr.isGoal;
		sr.reward 	= getCellReward(e,cell_type,wallsCount,opensCount);
		sr.invalidNext = (cell_type == GRID_WALL);
	}	
	return sr;
};

stepResult stepIntoStateUnscaled(MazeEnv* e,state_t s,size_t wallsCount, size_t opensCount){
	stepResult sr = {0};
	if (s.x >= e->cols || s.y >= e->rows || s.x < 0 || s.y < 0) {
		sr.isGoal = false;
		// sr.terminal default is false
		sr.reward = getCellRewardUnscaled(e,GRID_WALL,wallsCount,opensCount);
        sr.invalidNext = true;
	} else {
		GridCellType cell_type = (GridCellType)getCell(e,s.y,s.x);
		sr.isGoal   = (cell_type == GRID_AGENT_GOAL);
		sr.terminal = sr.isGoal;
		sr.reward 	= getCellRewardUnscaled(e,cell_type,wallsCount,opensCount);
		sr.invalidNext = (cell_type == GRID_WALL);
	}	
	return sr;
};


void agentSetSeed(Agent* self,unsigned int seed){
	self->seed = seed;
	srand((unsigned int)self->seed);
};

void agentInit(Agent* agent,MazeEnv* env,float lr,float dr,double eps_decay, unsigned long seed){
	agent->learning_rate = lr;
	agent->discount_rate = dr;
	agent->epsilon_decay = eps_decay;
	agent->epsilon = 1.0f;
	agentSetSeed(agent,(unsigned int)seed);
	agent->policy_action = ACTION_NONE;
	cellId _temp_id = getFirstMatchingCell(env,GRID_AGENT_START);
	agent->current_s.x = _temp_id.col;
	agent->current_s.y = _temp_id.row;
	agent->agent_start = agent->current_s;
	agent->accum_reward = 0.0f;
};

Agent* newAgent(MazeEnv *env,float lr,float dr,double eps_decay, unsigned long seed){
	Agent* ag = (Agent*)calloc(1,sizeof(Agent));
	agentInit(ag,env,lr,dr, eps_decay, seed);
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
	if (s.x < 0 || s.y < 0 || s.x >= n_x || s.y >= n_y || a >= n_actions) return 0.0f;
	return self->q_table.vals[n_actions*(((size_t)s.y*n_x)+s.x) + a];
}

void setQtableValue(Agent* self,state_t s,Action a, q_val_t q){
	size_t n_actions = self->q_table.len_state_actions;
	size_t n_x = self->q_table.len_state_x;
	size_t n_y = self->q_table.len_state_y;
	if (s.x < 0 || s.y < 0 || s.x >= n_x || s.y >= n_y || a >= n_actions) return;
	self->q_table.vals[n_actions*(((size_t)s.y*n_x)+s.x) + a] = q;
}

ValAction qtableMaxValAction(Agent* a,state_t s){
	float max_qval_t  = -FLT_MAX;
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

void agentPolicy(Agent* self,MazeEnv* env){
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
	return (state_t){(int32_t)nx,(int32_t)ny};
};

void agentUpdateState(Agent* a, state_t new_state){
	a->policy_action = ACTION_NONE;
	if (new_state.x < 0 || new_state.y < 0 || new_state.x >= a->q_table.len_state_x || new_state.y >= a->q_table.len_state_y){
		
	} else{
		a->current_s = new_state;
	}
};

float agentQtableUpdate(Agent* self,state_t next,stepResult sr){
	q_val_t old_q_val = getQtableValue(self,self->current_s,self->policy_action);
    q_val_t old_q_trace = (1-self->learning_rate)*old_q_val;
	q_val_t TD = 0.0;
	if(sr.terminal == true){
		TD = self->learning_rate*sr.reward;
	} else{
		q_val_t max_next_q_val = qtableMaxValAction(self,next).v;
		TD = self->learning_rate*(sr.reward+self->discount_rate*max_next_q_val);	
	}
	q_val_t new_q = old_q_trace + TD;
	setQtableValue(self,self->current_s,self->policy_action,new_q);

    // Return TD error
    return TD - self->learning_rate*old_q_val; 
}

float exp_epislon_decay(float epsilon,float decay){return epsilon*decay;}
float linear_epislon_decay(float epsilon,float decay){return epsilon - decay;}

static decay_fn decayTypeToFn[] = {
	[DECAY_TYPE_LINEAR] = linear_epislon_decay,
	[DECAY_TYPE_EXP] = exp_epislon_decay
};

static char* decayTypeToStr[] ={
	[DECAY_TYPE_LINEAR] = "LINEAR DECAY",
	[DECAY_TYPE_EXP] = "EXPONENTIAL DECAY"
};

void agentEpsilonDecay(Agent* self,decay_fn fn){
	self->epsilon = fn(self->epsilon,self->epsilon_decay);
	if(self->epsilon < 0.0f) self->epsilon = 0.0f;
	if(self->epsilon > 1.0f) self->epsilon = 1.0f;
};


int agentSaveQtable(Agent* agent, char* save_path){
    if(!agent || !save_path) return -1;
    FILE* f = fopen(save_path,"wb");
    if(!f) { perror("fopen"); return -1; }

    uint64_t nx = (uint64_t)agent->q_table.len_state_x;
    uint64_t ny = (uint64_t)agent->q_table.len_state_y;
    uint64_t na = (uint64_t)agent->q_table.len_state_actions;

    if(fwrite(&nx, sizeof(uint64_t), 1, f) != 1) { fclose(f); return -1; }
    if(fwrite(&ny, sizeof(uint64_t), 1, f) != 1) { fclose(f); return -1; }
    if(fwrite(&na, sizeof(uint64_t), 1, f) != 1) { fclose(f); return -1; }

    size_t q_table_len = (size_t)nx * (size_t)ny * (size_t)na;
    if(q_table_len == 0){ fclose(f); return -1; }

    size_t wrote = fwrite(agent->q_table.vals, sizeof(q_val_t), q_table_len, f);
    if(wrote != q_table_len){
        fprintf(stderr, "[ERROR] wrote %zu of %zu qvals\n", wrote, q_table_len);
        fclose(f);
        return -1;
    }

    fflush(f);
    fclose(f);
    return 0;
}

int agentReadQtable(Agent* agent, const char* load_path){
    if(!agent || !load_path) return -1;
    FILE *f = fopen(load_path, "rb");
    if(!f){ perror("fopen"); return -1; }

    uint64_t nx=0, ny=0, na=0;
    if(fread(&nx, sizeof(uint64_t), 1, f) != 1 ||
       fread(&ny, sizeof(uint64_t), 1, f) != 1 ||
       fread(&na, sizeof(uint64_t), 1, f) != 1) {
        fprintf(stderr, "[ERROR] corrupt header\n"); fclose(f); return -1;
    }

    if(nx == 0 || ny == 0 || na == 0){ fclose(f); return -1; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    long expected = (long)(3*sizeof(uint64_t) + (uint64_t)nx*ny*na*sizeof(q_val_t));
    if(fsize != expected){
        fprintf(stderr, "[WARN] file size mismatch: %ld != %ld (expected)\n", fsize, expected);
    }
    fseek(f, 3*sizeof(uint64_t), SEEK_SET);

    size_t qlen = (size_t)nx * (size_t)ny * (size_t)na;
    q_val_t *vals = malloc(sizeof(q_val_t) * qlen);
    if(!vals){ fclose(f); return -1; }

    size_t read = fread(vals, sizeof(q_val_t), qlen, f);
    if(read != qlen){ fprintf(stderr,"[ERROR] read %zu of %zu qvals\n", read, qlen); free(vals); fclose(f); return -1; }

    fclose(f);

    if(agent->q_table.vals) free(agent->q_table.vals);
    agent->q_table.vals = vals;
    agent->q_table.len_state_x = (size_t)nx;
    agent->q_table.len_state_y = (size_t)ny;
    agent->q_table.len_state_actions = (size_t)na;
    return 0;
}

#endif