#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>


// #define FLAG_IMPLEMENTATION
// #include "flag.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"

// GridCellType define tipos de celulas da grade

// tipo que representa o efeito da ação no estado atual

/*
	O proximo estado (s´) é uma função do estado atual (s) com a ação tomada
	(a).
	No caso de uma Maze, pode se definir:

	s´ = s + action_delta(a), onde s = (x,y) e action_delta(a) = (dx,dy)
*/

typedef struct {uint8_t x; uint8_t y}   state_t;
typedef struct {uint8_t dx; uint8_t dy} action_delta_t;

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

// typedef struct {
// 	MazeInternalRepr ir;
// } MazeEnv;

typedef struct {
	state_t next_s;
	reward_t reward;
	bool terminal
} stepResult;

typedef struct {
	// MUTABLE STATE 
	q_val_t* q_table;
	state_t current_s;
	Action	policy_action;
	float epsilon;
	// HYPER PARAMETERS
	float learning_rate;
	float discount_rate;
	float epsilon_decay;
	time_t seed;

} Agent;

state_t getFirstMatchingCell(MazeEnv *m,GridCellType t){
	for(size_t i=0;i < m->rows; i++)
		for(size_t j=0; j < m->cols; j++){
			if(getCell(m,i,j) == t) return (state_t){i,j};
		}
}

void agentSetSeed(Agent* self,unsigned int seed){
	self->seed = seed;
	srand(self->seed);
};

Agent* initAgent(MazeEnv *env,float lr,float dr,float eps_decay, unsigned long seed){
	Agent* ag = (Agent*)calloc(1,sizeof(Agent));
	ag->learning_rate = lr;
	ag->discount_rate = dr;
	ag->epsilon_decay = eps_decay;
	ag->epsilon = 1.0f;
	
	agentSetSeed(ag,seed);
	
	ag->policy_action = ACTION_NONE;
	ag->current_s = getFirstMatchingCell(env,GRID_AGENT_START);
	ag->policy_action = ACTION_NONE;
	ag->q_table = (q_val_t*)calloc((env->cols*env->rows)*ACTION_N_ACTIONS,sizeof(q_val_t));
	
	return ag;
};

/*
	EXEMPLO:

	CONSIDERANDO N_ACOES = 2
	CONSIDERANDO ROWS,COLS = 3,4
	
	N_ESTADOS = 12
	N_ENTRADAS_Q = N_ESTADOS*N_ACOES = 24

	c_s1 = (0,0)
	c_s2 = (0,1)

	Q TABLE ={
		0.12,0.13,0.14,0.15
	}

*/

Action qtableMaxValAction(MazeEnv* env,Agent* a,state_t s){
	// (i*m->cols)+j
	float max_qval_t  = -INFINITY;
	Action max_action = ACTION_NONE; 
	for(int i = 0; i < ACTION_N_ACTIONS; i++){
		q_val_t q = a->q_table[ACTION_N_ACTIONS*((s.y*env->cols)+s.x) + i];
		if(q > max_qval_t) {
			max_qval_t = q;
			max_action = (Action)i;
		}
	}
};

// TODO: ACEITAR MAIS OPÇÕES DE POLITICAS 
void agentPolicy(MazeEnv* env, Agent* self){
	float r = (float)rand()/RAND_MAX;
	if(r > self->epsilon){
		self->policy_action = qtableMaxValAction(env,self,self->current_s);
	} else{
		float r_action = (ACTION_N_ACTIONS*(float)rand()/RAND_MAX);
		self->policy_action = (Action)ceil(r_action);
	}
};

int main(int argc, char** argv){
	

	printf("Hello World\n");
	return 0;
};