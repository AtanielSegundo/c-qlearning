#ifndef APP_CONTEXT_H
#define APP_CONTEXT_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* Note: this header expects raylib.h, mazeIR.h, mazeRender.h and agent.h
 * (with their respective _IMPLEMENTATION macros) to have already been
 * included by the translation unit, since mazeRender.h emits unguarded
 * function definitions that would multiply on a second inclusion. */

#define APP_PATH_MAX        512
#define APP_STATE_FILE      ".cqlearning_state"
#define APP_DEFAULT_EPISODES   200
#define APP_DEFAULT_MAX_STEPS  1024
#define APP_DEFAULT_LR         5e-4f
#define APP_DEFAULT_DF         0.99f
#define APP_DEFAULT_EPS_DECAY  37001.0f
#define APP_DEFAULT_SEED       67UL

typedef enum {
    APP_MODE_MENU = 0,
    APP_MODE_EDITOR,
    APP_MODE_VIEWER,
    APP_MODE_TRAINER
} AppMode;

typedef struct {
    /* shared resources */
    MazeInternalRepr ir;
    MazeRenderCtx    render;
    Agent            agent;

    /* file paths */
    char maze_path   [APP_PATH_MAX];
    char qtable_path [APP_PATH_MAX];
    char metrics_path[APP_PATH_MAX];

    /* derived sizes */
    size_t walls_count;
    size_t opens_count;

    /* load flags */
    bool maze_loaded;
    bool agent_loaded;

    /* training hyper-parameters (also used by trainer mode) */
    float  learning_rate;
    float  discount_factor;
    float  epsilon_decay;
    size_t num_episodes;
    size_t max_steps;
    unsigned long seed;
    bool   distance_reward_shaping;
    bool   block_transpassing_walls;

    /* feedback popup state */
    bool popup_active;
    char popup_msg[256];

    /* current screen */
    AppMode mode;
} AppContext;

#define APP_POPUP(ctx, msg)                                                   \
    do { (ctx)->popup_active = true;                                          \
         snprintf((ctx)->popup_msg, sizeof((ctx)->popup_msg), "%s", (msg));   \
    } while (0)

void appContextInit       (AppContext* ctx);
void appContextRefreshSize(AppContext* ctx);
bool appLoadMazeFile      (AppContext* ctx, const char* path);
bool appLoadQtableFile    (AppContext* ctx, const char* path);
bool appSaveMazeFile      (AppContext* ctx, const char* path);
bool appSaveQtableFile    (AppContext* ctx, const char* path);
void appSaveState         (const AppContext* ctx);
void appLoadState         (AppContext* ctx);

#endif /* APP_CONTEXT_H */

#ifdef APP_CONTEXT_IMPLEMENTATION

void appContextInit(AppContext* ctx) {
    memset(ctx, 0, sizeof(*ctx));
    MazeRenderCtxInit(&ctx->render);
    ctx->learning_rate            = APP_DEFAULT_LR;
    ctx->discount_factor          = APP_DEFAULT_DF;
    ctx->epsilon_decay            = APP_DEFAULT_EPS_DECAY;
    ctx->num_episodes             = APP_DEFAULT_EPISODES;
    ctx->max_steps                = APP_DEFAULT_MAX_STEPS;
    ctx->seed                     = APP_DEFAULT_SEED;
    ctx->distance_reward_shaping  = false;
    ctx->block_transpassing_walls = true;
    ctx->mode                     = APP_MODE_MENU;
}

void appContextRefreshSize(AppContext* ctx) {
    if (!ctx->maze_loaded) { ctx->walls_count = 0; ctx->opens_count = 0; return; }
    ctx->walls_count = countAllMatchingCells(&ctx->ir, GRID_WALL);
    ctx->opens_count = countAllMatchingCells(&ctx->ir, GRID_OPEN);
}

bool appLoadMazeFile(AppContext* ctx, const char* path) {
    if (!path || !*path) return false;
    bool isNumpy   = IsFileExtension(path, ".npy");
    bool isRawMaze = IsFileExtension(path, ".maze");
    int rc = -1;
    if      (isNumpy)   rc = readMazeNumpy((char*)path, &ctx->ir);
    else if (isRawMaze) rc = readMazeRaw  ((char*)path, &ctx->ir);
    if (rc != 0) return false;

    strncpy(ctx->maze_path, path, sizeof(ctx->maze_path) - 1);
    ctx->maze_path[sizeof(ctx->maze_path) - 1] = '\0';
    ctx->maze_loaded = true;
    appContextRefreshSize(ctx);
    return true;
}

bool appLoadQtableFile(AppContext* ctx, const char* path) {
    if (!path || !*path) return false;
    if (!IsFileExtension(path, ".qtable")) return false;
    if (agentReadQtable(&ctx->agent, path) != 0) return false;

    strncpy(ctx->qtable_path, path, sizeof(ctx->qtable_path) - 1);
    ctx->qtable_path[sizeof(ctx->qtable_path) - 1] = '\0';
    ctx->agent_loaded = true;
    return true;
}

bool appSaveMazeFile(AppContext* ctx, const char* path) {
    if (!ctx->maze_loaded) return false;
    return writeMazeRaw((char*)path, &ctx->ir) == 0;
}

bool appSaveQtableFile(AppContext* ctx, const char* path) {
    if (!ctx->agent_loaded) return false;
    return agentSaveQtable(&ctx->agent, (char*)path) == 0;
}

void appSaveState(const AppContext* ctx) {
    FILE* f = fopen(APP_STATE_FILE, "w");
    if (!f) return;
    fprintf(f, "maze_path=%s\n",   ctx->maze_path);
    fprintf(f, "qtable_path=%s\n", ctx->qtable_path);
    fprintf(f, "metrics_path=%s\n",ctx->metrics_path);
    fprintf(f, "mode=%d\n",        (int)ctx->mode);
    fprintf(f, "episodes=%zu\n",   ctx->num_episodes);
    fprintf(f, "max_steps=%zu\n",  ctx->max_steps);
    fprintf(f, "lr=%f\n",          ctx->learning_rate);
    fprintf(f, "df=%f\n",          ctx->discount_factor);
    fprintf(f, "eps_decay=%f\n",   ctx->epsilon_decay);
    fprintf(f, "seed=%lu\n",       ctx->seed);
    fclose(f);
}

void appLoadState(AppContext* ctx) {
    FILE* f = fopen(APP_STATE_FILE, "r");
    if (!f) return;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        int   im;
        if (sscanf(line, "maze_path=%511[^\n]",   ctx->maze_path)    == 1) continue;
        if (sscanf(line, "qtable_path=%511[^\n]", ctx->qtable_path)  == 1) continue;
        if (sscanf(line, "metrics_path=%511[^\n]",ctx->metrics_path) == 1) continue;
        if (sscanf(line, "mode=%d", &im)                             == 1) {
            if (im >= 0 && im <= APP_MODE_TRAINER) ctx->mode = (AppMode)im;
            continue;
        }
        if (sscanf(line, "episodes=%zu", &ctx->num_episodes)         == 1) continue;
        if (sscanf(line, "max_steps=%zu",&ctx->max_steps)            == 1) continue;
        if (sscanf(line, "lr=%f",        &ctx->learning_rate)        == 1) continue;
        if (sscanf(line, "df=%f",        &ctx->discount_factor)      == 1) continue;
        if (sscanf(line, "eps_decay=%f", &ctx->epsilon_decay)        == 1) continue;
        if (sscanf(line, "seed=%lu",     &ctx->seed)                 == 1) continue;
    }
    fclose(f);
}

#endif /* APP_CONTEXT_IMPLEMENTATION */
