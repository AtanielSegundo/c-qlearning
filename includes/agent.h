#ifndef AGENT_H

#include "stdint.h"
#include "mazeIR.h"

#define AGENT_H

typedef MazeInternalRepr MazeEnv;
typedef struct {int8_t dx; int8_t dy;} action_delta_t;
typedef struct {size_t x; size_t y;} state_t;
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

static reward_t GridTypeToReward[] = {
	[GRID_OPEN]        = 0.0f,
	[GRID_WALL]        = -1.0f,
	[GRID_AGENT_GOAL]  = 1.0f,
	[GRID_AGENT_START] = 0.0f
};

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
void agentQtableUpdate(Agent* self,state_t next,stepResult sr);
void agentEpsilonDecay(Agent* self,decay_fn fn);
int agentSaveQtable(Agent* agent,char* save_path);
int agentReadQtable(Agent* agent, const char* load_path);

stepResult stepIntoState(MazeEnv* e,state_t s);
void agentSetSeed(Agent* self,unsigned int seed);

q_val_t getQtableValue(Agent* self,state_t s,Action a);
void setQtableValue(Agent* self,state_t s,Action a, q_val_t q);
ValAction qtableMaxValAction(Agent* a,state_t s);

#endif

#ifdef AGENT_IMPLEMENTATION

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

void agentSetSeed(Agent* self,unsigned int seed){
	self->seed = seed;
	srand((unsigned int)self->seed);
};

Agent* newAgent(MazeEnv *env,float lr,float dr,double eps_decay, unsigned long seed){
	Agent* ag = (Agent*)calloc(1,sizeof(Agent));
	ag->learning_rate = lr;
	ag->discount_rate = dr;
	ag->epsilon_decay = eps_decay;
	ag->epsilon = 1.0f;
	
	agentSetSeed(ag,(unsigned int)seed);
	
	ag->policy_action = ACTION_NONE;
	cellId _temp_id = getFirstMatchingCell(env,GRID_AGENT_START);
	ag->current_s.x = _temp_id.col;
	ag->current_s.y = _temp_id.row;
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

int agentSaveQtable(Agent* agent,char* save_path){
	if(!save_path){
		printf("[ERROR] Cant find the save path selected\n");
		return -1;
	}
	FILE* f = fopen(save_path,"wb");
	// <name>.qtable format = |u64: len_state_x| u64: len_state_y | u64: len_state_actions | float: vals[len_state_x*len_state_y*len_state_actions]
	fwrite(&agent->q_table.len_state_x,sizeof(size_t),1,f);
	fwrite(&agent->q_table.len_state_y,sizeof(size_t),1,f);
	fwrite(&agent->q_table.len_state_actions,sizeof(size_t),1,f);

	size_t q_table_len = agent->q_table.len_state_x * agent->q_table.len_state_y * agent->q_table.len_state_actions;
	fwrite(agent->q_table.vals,sizeof(q_val_t),q_table_len,f);

	return 0;
};

int agentReadQtable(Agent* agent, const char* load_path){
    if(!agent || !load_path){
        fprintf(stderr,"[ERROR] agentReadQtable: argumento NULL\n");
        return -1;
    }

    FILE *f = fopen(load_path, "rb");
    if(!f){
        fprintf(stderr,"[ERROR] failed to open '%s' for reading: %s\n", load_path, strerror(errno));
        return -1;
    }

    size_t nx = 0, ny = 0, na = 0;
    if (fread(&nx, sizeof(size_t), 1, f) != 1 ||
        fread(&ny, sizeof(size_t), 1, f) != 1 ||
        fread(&na, sizeof(size_t), 1, f) != 1) {
        fprintf(stderr, "[ERROR] corrupt qtable header in '%s'\n", load_path);
        fclose(f);
        return -1;
    }

    /* sanity check */
    if (nx == 0 || ny == 0 || na == 0) {
        fprintf(stderr, "[ERROR] invalid dimensions in qtable file '%s'\n", load_path);
        fclose(f);
        return -1;
    }

    size_t q_table_len = nx * ny * na;
    q_val_t *vals = (q_val_t*)malloc(sizeof(q_val_t) * q_table_len);
    if(!vals){
        fprintf(stderr, "[ERROR] alloc fail reading qtable (%zu entries)\n", q_table_len);
        fclose(f);
        return -1;
    }

    size_t read = fread(vals, sizeof(q_val_t), q_table_len, f);
    if(read != q_table_len){
        fprintf(stderr, "[ERROR] unexpected EOF or read error in '%s' (expected %zu, got %zu)\n", load_path, q_table_len, read);
        free(vals);
        fclose(f);
        return -1;
    }

    fclose(f);

    /* replace existing q_table (free previous memory if any) */
    if(agent->q_table.vals){
        free(agent->q_table.vals);
        agent->q_table.vals = NULL;
    }
    agent->q_table.len_state_x = nx;
    agent->q_table.len_state_y = ny;
    agent->q_table.len_state_actions = na;
    agent->q_table.vals = vals;

    return 0;
}

#endif