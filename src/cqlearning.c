/* ========================================================================
 *  Modes (selectable from main menu):
 *      MENU      : entry screen
 *      EDITOR    : create / load / edit / save maze grids
 *      TRAINER   : train an agent with real-time graphs + CSV export
 *      VIEWER    : run a trained Q-table on a maze
 * ======================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <time.h>

#include "raylib.h"
#include "raygui.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"
#undef MAZE_IR_IMPLEMENTATION

#define MAZE_RENDER_IMPLEMENTATION
#include "mazeRender.h"

#define AGENT_IMPLEMENTATION
#include "agent.h"

#define UI_IMPLEMENTATION
#include "UI.h"

#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#include "mazeGeneration.h"

#define APP_CONTEXT_IMPLEMENTATION
#include "appContext.h"

#define WINDOW_W 1280U
#define WINDOW_H  720U
#define ROLLING_WIN 20

/* ------------------------------------------------------------------ */
/*  Dynamic array for agent step tracking (T key)                     */
/* ------------------------------------------------------------------ */
#define da_append(xs, x)                                                             \
    do {                                                                             \
        if ((xs)->count >= (xs)->capacity) {                                         \
            if ((xs)->capacity == 0) (xs)->capacity = 256;                           \
            else (xs)->capacity *= 2;                                                \
            (xs)->items = realloc((xs)->items, (xs)->capacity*sizeof(*(xs)->items)); \
        }                                                                            \
        (xs)->items[(xs)->count++] = (x);                                            \
    } while (0)

typedef struct {
    state_t s;
    Action  a;
} agentStep;

typedef struct {
    agentStep* items;
    size_t     count;
    size_t     capacity;
} agentSteps;

/* ------------------------------------------------------------------ */
/*  Module-scope persistent UI state                                  */
/* ------------------------------------------------------------------ */
static GuiWindowFileDialogState g_fdlg;
static GenerateFormState        g_genForm;

typedef enum {
    DLG_NONE = 0,
    DLG_LOAD_MAZE,
    DLG_SAVE_MAZE,
    DLG_LOAD_QTABLE,
    DLG_SAVE_QTABLE,
    DLG_SAVE_METRICS
} DialogTarget;

static DialogTarget g_dlg_target = DLG_NONE;

/* Trainer ------------------------------------------------------------ */
typedef struct {
    bool   running;
    bool   paused;
    bool   done;
    bool   allocated;

    size_t cur_episode;
    size_t goals_count;
    unsigned int total_training_steps;

    float*  rewards;
    double* success_rate;
    double* loss;
    size_t* steps;
    size_t* cum_goals;
    bool*   goal_reached;

    int episodes_per_frame;
} TrainState;

static TrainState g_train;

/* Viewer ------------------------------------------------------------- */
typedef struct {
    bool    running;
    bool    paused_on_goal;
    double  last_update;
    double  update_interval;
    size_t  steps_taken;
    bool    is_goal;
    q_val_t accum_q;

    /* T — step tracking */
    bool       track_steps;
    agentSteps agent_steps;

    /* Z — lock camera to agent */
    bool   lock_camera;
    int    prev_mouse_offset_x;
    int    prev_mouse_offset_y;
    state_t prev_agent_pos;

    /* smooth camera transition */
    bool   cam_transition_active;
    double cam_transition_start;
    double cam_transition_duration;
    int    cam_from_x, cam_from_y;
    int    cam_target_x, cam_target_y;
} ViewerState;

static ViewerState g_view;

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */
static void drawPopup(AppContext* ctx) {
    if (!ctx->popup_active) return;
    setDefaultStyle();
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.45f));
    int w = 640, h = 140;
    int x = (GetScreenWidth() - w) / 2, y = (GetScreenHeight() - h) / 2;
    DrawRectangle(x, y, w, h, LIGHTGRAY);
    DrawRectangleLines(x, y, w, h, GRAY);
    DrawTextEx(GetFontDefault(), ctx->popup_msg, (Vector2){x + 12, y + 16}, 18, 1, BLACK);
    if (GuiButton((Rectangle){x + (w - 100) / 2, y + h - 44, 100, 32}, "OK")) {
        ctx->popup_active = false;
        ctx->popup_msg[0] = '\0';
    }
    setCustomStyle();
}

static void smoothToF_from_f(const float*  src, float* dst, size_t n, int win) {
    if (!src || !dst || n == 0) return;
    if (win < 1) win = 1;
    for (size_t i = 0; i < n; i++) {
        float sum = 0.0f;
        for (int j = 0; j < win; j++) {
            int idx = (int)i - j;
            sum += (idx >= 0) ? src[idx] : 0.0f;
        }
        dst[i] = sum / (float)win;
    }
}
static void smoothToF_from_d(const double* src, float* dst, size_t n, int win) {
    if (!src || !dst || n == 0) return;
    if (win < 1) win = 1;
    for (size_t i = 0; i < n; i++) {
        double sum = 0.0;
        for (int j = 0; j < win; j++) {
            int idx = (int)i - j;
            sum += (idx >= 0) ? src[idx] : 0.0;
        }
        dst[i] = (float)(sum / (double)win);
    }
}
static void smoothToF_from_z(const size_t* src, float* dst, size_t n, int win) {
    if (!src || !dst || n == 0) return;
    if (win < 1) win = 1;
    for (size_t i = 0; i < n; i++) {
        double sum = 0.0;
        for (int j = 0; j < win; j++) {
            int idx = (int)i - j;
            sum += (idx >= 0) ? (double)src[idx] : 0.0;
        }
        dst[i] = (float)(sum / (double)win);
    }
}

static void drawLineGraphF(Rectangle r, const float* vals, size_t n,
                           const char* title, Color col, size_t x_offset)
{
    DrawRectangleLines((int)r.x, (int)r.y, (int)r.width, (int)r.height, GRAY);
    DrawText(title, (int)r.x + 6, (int)r.y + 4, 14, RAYWHITE);
    if (n < 2) return;

    float minv = vals[0], maxv = vals[0];
    for (size_t i = 1; i < n; i++) {
        if (vals[i] < minv) minv = vals[i];
        if (vals[i] > maxv) maxv = vals[i];
    }
    if (maxv - minv < 1e-6f) maxv = minv + 1.0f;

    float pad_top = 22.0f, pad_bot = 8.0f;
    float hh = r.height - pad_top - pad_bot;
    float ww = r.width - 12.0f;

    Vector2 prev = {0};
    for (size_t i = 0; i < n; i++) {
        float t = (n == 1) ? 0.0f : (float)i / (float)(n - 1);
        float v = (vals[i] - minv) / (maxv - minv);
        Vector2 p = { r.x + 6 + t * ww, r.y + pad_top + (1.0f - v) * hh };
        if (i > 0) DrawLineEx(prev, p, 1.5f, col);
        prev = p;
    }

    DrawText(TextFormat("%.2f", maxv), (int)(r.x + r.width - 70), (int)(r.y + 4), 12, GRAY);
    DrawText(TextFormat("%.2f", minv), (int)(r.x + r.width - 70), (int)(r.y + r.height - 16), 12, GRAY);

    /* hover tooltip — nearest sample under mouse x */
    Vector2 mp = GetMousePosition();
    if (CheckCollisionPointRec(mp, r)) {
        float t = (mp.x - r.x - 6.0f) / ww;
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        size_t i = (size_t)(t * (float)(n - 1) + 0.5f);
        if (i >= n) i = n - 1;

        float t_i = (float)i / (float)(n - 1);
        float v   = (vals[i] - minv) / (maxv - minv);
        Vector2 sp = { r.x + 6 + t_i * ww, r.y + pad_top + (1.0f - v) * hh };

        DrawLineEx((Vector2){ sp.x, r.y + pad_top },
                   (Vector2){ sp.x, r.y + r.height - pad_bot },
                   1.0f, Fade(RAYWHITE, 0.35f));
        DrawCircleV(sp, 4.0f, RAYWHITE);
        DrawCircleV(sp, 2.5f, col);

        const int tip_font   = 22;
        const int tip_pad_x  = 10;
        const int tip_pad_y  = 6;
        char buf[64];
        snprintf(buf, sizeof(buf), "x=%zu  y=%.4g", i + x_offset, (double)vals[i]);
        int tw = MeasureText(buf, tip_font);
        int box_w = tw + tip_pad_x * 2;
        int box_h = tip_font + tip_pad_y * 2;
        int tx = (int)mp.x + 18;
        int ty = (int)mp.y - box_h - 6;
        if (tx + box_w > GetScreenWidth()) tx = (int)mp.x - box_w - 18;
        if (ty < 0) ty = (int)mp.y + 24;
        DrawRectangle(tx, ty, box_w, box_h, Fade(BLACK, 0.85f));
        DrawRectangleLines(tx, ty, box_w, box_h, GRAY);
        DrawText(buf, tx + tip_pad_x, ty + tip_pad_y, tip_font, RAYWHITE);
    }
}

static void drawLineGraphD(Rectangle r, const double* vals, size_t n,
                           const char* title, Color col)
{
    if (n == 0) { drawLineGraphF(r, NULL, 0, title, col, 0); return; }
    static float buf[8192];
    size_t take = n > 8192 ? 8192 : n;
    size_t skip = n - take;
    for (size_t i = 0; i < take; i++) buf[i] = (float)vals[skip + i];
    drawLineGraphF(r, buf, take, title, col, skip);
}

static void drawLineGraphZ(Rectangle r, const size_t* vals, size_t n,
                           const char* title, Color col)
{
    if (n == 0) { drawLineGraphF(r, NULL, 0, title, col, 0); return; }
    static float buf[8192];
    size_t take = n > 8192 ? 8192 : n;
    size_t skip = n - take;
    for (size_t i = 0; i < take; i++) buf[i] = (float)vals[skip + i];
    drawLineGraphF(r, buf, take, title, col, skip);
}

static float huber(float x) {
    const float d = 1.0f;
    float a = fabsf(x);
    return (a <= d) ? 0.5f * x * x : d * (a - 0.5f * d);
}

static int manhattan(state_t a, state_t b) {
    return abs(a.x - b.x) + abs(a.y - b.y);
}

static void openFileDialog(DialogTarget t, bool save_mode, const char* hint_path) {
    g_dlg_target = t;
    g_fdlg.windowActive = true;
    g_fdlg.saveFileMode = save_mode;
    if (save_mode && hint_path && *hint_path) {
        strcpy(g_fdlg.dirPathText,  GetDirectoryPath(hint_path));
        strcpy(g_fdlg.fileNameText, GetFileName(hint_path));
    }
}

/* ------------------------------------------------------------------ */
/*  Camera helper for Z (lock-to-agent)                               */
/* ------------------------------------------------------------------ */
static void compute_camera_target_for_agent(MazeRenderCtx *render, MazeInternalRepr *ir,
                                            Agent *agent, int *out_tx, int *out_ty)
{
    *out_tx = render->mouse_offset_x;
    *out_ty = render->mouse_offset_y;
    if (ir->rows > 0 && ir->cols > 0) {
        state_t s = agent->current_s;
        *out_tx = (int)((GetScreenWidth()  / 2) - (render->offset_x + s.x * render->cr_lenght + render->cr_lenght / 2));
        *out_ty = (int)((GetScreenHeight() / 2) - (render->offset_y + s.y * render->cr_lenght + render->cr_lenght / 2));
    }
}

/* ------------------------------------------------------------------ */
/*  Arrow rendering for T (track steps)                               */
/* ------------------------------------------------------------------ */
static void renderTrackedArrows(const agentSteps* steps, const MazeRenderCtx* render) {
    if (!steps || steps->count == 0) return;

    const float arrow_pad_percent        = 0.1f;
    const float arrow_body_width_percent = 0.10f;
    const float arrow_head_width_percent = 0.35f;
    const float arrow_body_height_percent = 0.75f;

    float arrow_total_height = render->cr_lenght * (1.0f - 2 * arrow_pad_percent);
    float arrow_body_height  = arrow_total_height * arrow_body_height_percent;
    float arrow_body_width   = render->cr_lenght * arrow_body_width_percent;
    float arrow_head_width   = render->cr_lenght * arrow_head_width_percent;

    const Color arrow_color = BLACK;

    for (size_t i = 0; i < steps->count; i++) {
        state_t _s = steps->items[i].s;
        Action act = steps->items[i].a;

        Vector2 s_to_pixel = mazeCellToPixel(render, _s.y, _s.x);

        if (act >= ACTION_N_ACTIONS) continue;
        if (s_to_pixel.x < 0.0f || s_to_pixel.y < 0.0f) continue;

        Vector2 center = {
            s_to_pixel.x + (float)render->cr_lenght * 0.5f,
            s_to_pixel.y + (float)render->cr_lenght * 0.5f
        };

        float ux = (float)actionToDeltaMap[act].dx;
        float uy = (float)actionToDeltaMap[act].dy;
        if (ux == 0.0f && uy == 0.0f) continue;

        float px = -uy;
        float py =  ux;

        float half_body_w = arrow_body_width * 0.5f;
        float half_head_w = arrow_head_width * 0.5f;

        Vector2 body_start = {
            center.x - ux * (arrow_total_height * 0.5f),
            center.y - uy * (arrow_total_height * 0.5f)
        };
        Vector2 body_end = {
            body_start.x + ux * arrow_body_height,
            body_start.y + uy * arrow_body_height
        };

        Vector2 neck = body_end;
        Vector2 head_tip = {
            neck.x + ux * (arrow_total_height * 0.25f),
            neck.y + uy * (arrow_total_height * 0.25f)
        };
        Vector2 head_left  = { neck.x + px * half_head_w, neck.y + py * half_head_w };
        Vector2 head_right = { neck.x - px * half_head_w, neck.y - py * half_head_w };

        Vector2 p1 = { body_start.x + px * half_body_w, body_start.y + py * half_body_w };
        Vector2 p2 = { body_end.x   + px * half_body_w, body_end.y   + py * half_body_w };
        Vector2 p3 = { body_end.x   - px * half_body_w, body_end.y   - py * half_body_w };
        Vector2 p4 = { body_start.x - px * half_body_w, body_start.y - py * half_body_w };

        DrawTriangle(p1, p2, p3, arrow_color);
        DrawTriangle(p1, p3, p4, arrow_color);
        DrawTriangle(head_left, head_tip, head_right, arrow_color);
    }
}

/* ------------------------------------------------------------------ */
/*  Trainer mode                                                       */
/* ------------------------------------------------------------------ */
static void trainFreeMetrics(TrainState* t) {
    if (!t->allocated) return;
    int epf = t->episodes_per_frame; 
    free(t->rewards);
    free(t->success_rate);
    free(t->loss);
    free(t->steps);
    free(t->cum_goals);
    free(t->goal_reached);
    memset(t, 0, sizeof(*t));
    t->episodes_per_frame = epf > 0 ? epf : 5;
}

static bool trainAllocMetrics(TrainState* t, size_t n) {
    trainFreeMetrics(t);
    t->rewards      = (float*) calloc(n, sizeof(float));
    t->success_rate = (double*)calloc(n, sizeof(double));
    t->loss         = (double*)calloc(n, sizeof(double));
    t->steps        = (size_t*)calloc(n, sizeof(size_t));
    t->cum_goals    = (size_t*)calloc(n, sizeof(size_t));
    t->goal_reached = (bool*)  calloc(n, sizeof(bool));
    if (!t->rewards || !t->success_rate || !t->loss ||
        !t->steps   || !t->cum_goals    || !t->goal_reached) {
        trainFreeMetrics(t);
        return false;
    }
    t->allocated = true;
    return true;
}

static bool trainStart(AppContext* ctx) {
    if (!ctx->maze_loaded || ctx->ir.rows == 0 || ctx->ir.cols == 0) {
        APP_POPUP(ctx, "Load a maze first");
        return false;
    }
    if (!trainAllocMetrics(&g_train, ctx->num_episodes)) {
        APP_POPUP(ctx, "Failed to allocate training metrics");
        return false;
    }

    /* fresh agent backed by ctx */
    if (ctx->agent.q_table.vals) free(ctx->agent.q_table.vals);
    memset(&ctx->agent, 0, sizeof(ctx->agent));

    agentInit(&ctx->agent, &ctx->ir,
              1.0f - ctx->learning_rate,
              ctx->discount_factor,
              ctx->epsilon_decay,
              ctx->seed);
    ctx->agent.q_table.len_state_x       = ctx->ir.cols;
    ctx->agent.q_table.len_state_y       = ctx->ir.rows;
    ctx->agent.q_table.len_state_actions = ACTION_N_ACTIONS;
    ctx->agent.q_table.vals = (q_val_t*)calloc(ctx->ir.cols * ctx->ir.rows * ACTION_N_ACTIONS,
                                               sizeof(q_val_t));
    ctx->agent_loaded = true;

    appContextRefreshSize(ctx);

    g_train.running     = true;
    g_train.paused      = false;
    g_train.done        = false;
    g_train.cur_episode = 0;
    g_train.goals_count = 0;
    g_train.total_training_steps = 0;
    return true;
}

static void trainOneEpisode(AppContext* ctx) {
    TrainState* t = &g_train;
    Agent* ag     = &ctx->agent;
    MazeEnv* env  = &ctx->ir;

    state_t goal = {0,0};
    cellId gc = getFirstMatchingCell(env, GRID_AGENT_GOAL);
    goal.x = (int32_t)gc.col;
    goal.y = (int32_t)gc.row;

    agentRestart(ag);

    size_t   steps_done   = 0;
    bool     goal_reached = false;
    reward_t total_reward = 0.0f;
    float    model_loss   = 0.0f;

    for (size_t s = 0; s < ctx->max_steps; s++) {
        agentPolicy(ag, env);
        state_t next = GetNextState(ag->current_s, ag->policy_action);
        stepResult sr = stepIntoStateUnscaled(env, next, ctx->walls_count, ctx->opens_count);

        state_t trans = ctx->block_transpassing_walls
                        ? (sr.invalidNext ? ag->current_s : next)
                        : next;

        if (ctx->distance_reward_shaping && !sr.invalidNext) {
            float phi_s  = (float)manhattan(ag->current_s, goal);
            float phi_sp = (float)manhattan(trans, goal);
            sr.reward += ctx->discount_factor * (phi_s - phi_sp);
        }

        float td = agentQtableUpdate(ag, trans, sr);
        model_loss += huber(td);

        total_reward += sr.reward;
        steps_done++;

        if (sr.isGoal) { goal_reached = true; t->goals_count++; break; }
        if (sr.terminal) break;

        agentUpdateState(ag, trans);
    }

    t->total_training_steps += (unsigned int)steps_done;

    /* epsilon */
    const double eps_final = 0.1, eps_start = 1.0;
    double decay = ctx->epsilon_decay > 0.0 ? ctx->epsilon_decay : 1.0;
    double eps = eps_final + (eps_start - eps_final) *
                 exp(-(double)t->total_training_steps / decay);
    if (!isfinite(eps)) eps = eps_final;
    if (eps < eps_final) eps = eps_final;
    if (eps > 1.0)       eps = 1.0;
    ag->epsilon = (float)eps;

    /* metrics */
    size_t ep = t->cur_episode;
    t->goal_reached[ep] = goal_reached;

    int start = (int)ep - ROLLING_WIN + 1;
    if (start < 0) start = 0;
    int win_goals = 0;
    int win_len   = (int)ep - start + 1;
    for (int i = start; i <= (int)ep; i++) win_goals += t->goal_reached[i] ? 1 : 0;
    t->success_rate[ep] = 100.0 * (double)win_goals / (double)win_len;

    t->rewards[ep]   = (float)total_reward;
    t->cum_goals[ep] = t->goals_count;
    t->loss[ep]      = (double)model_loss;
    t->steps[ep]     = steps_done;

    t->cur_episode++;
    if (t->cur_episode >= ctx->num_episodes) {
        t->running = false;
        t->done    = true;
        ag->epsilon = 0.0f;   /* greedy after training */
    }
}

static bool saveMetricsCSV(AppContext* ctx, const char* path) {
    if (!g_train.allocated) return false;
    FILE* f = fopen(path, "w");
    if (!f) return false;
    fprintf(f, "episode,reward,cumulative_goals,success_rate,training_loss,steps,goal_reached\n");
    for (size_t i = 0; i < g_train.cur_episode; i++) {
        fprintf(f, "%zu,%.6f,%zu,%.4f,%.6e,%zu,%d\n",
                i,
                (double)g_train.rewards[i],
                g_train.cum_goals[i],
                g_train.success_rate[i],
                g_train.loss[i],
                g_train.steps[i],
                g_train.goal_reached[i] ? 1 : 0);
    }
    fclose(f);
    return true;
}

/* ------------------------------------------------------------------ */
/*  MENU                                                               */
/* ------------------------------------------------------------------ */
static void drawMenu(AppContext* ctx) {
    ClearBackground((Color){18, 22, 30, 255});

    int sw = GetScreenWidth(), sh = GetScreenHeight();

    const char* title = "C-Qlearning";
    int titleSize = sh / 11; if (titleSize < 28) titleSize = 28;
    int tw = MeasureText(title, titleSize);
    int title_y = sh / 7;
    DrawText(title, (sw - tw) / 2, title_y, titleSize, RAYWHITE);

    // const char* subtitle = "maze Q-learning toolkit";
    // DrawText(subtitle, (sw - MeasureText(subtitle, 22)) / 2,
    //          title_y + titleSize + 12, 22, GRAY);

    int btnW = sw / 4; if (btnW < 280) btnW = 280; if (btnW > 460) btnW = 460;
    int btnH = sh / 12; if (btnH < 44) btnH = 44; if (btnH > 64) btnH = 64;
    int gap = 18;
    int x = (sw - btnW) / 2;
    int y = sh / 4 + 30;

    if (GuiButton((Rectangle){x, y, btnW, btnH},
                  GuiIconText(ICON_FILE_NEW, "Maze Editor"))) {
        ctx->mode = APP_MODE_EDITOR;
    }
    y += btnH + gap;

    if (GuiButton((Rectangle){x, y, btnW, btnH},
                  GuiIconText(ICON_GEAR, "Train Agent"))) {
        ctx->mode = APP_MODE_TRAINER;
    }
    y += btnH + gap;

    if (GuiButton((Rectangle){x, y, btnW, btnH},
                  GuiIconText(ICON_PLAYER_PLAY, "Agent Viewer"))) {
        ctx->mode = APP_MODE_VIEWER;
    }
    y += btnH + gap;

    if (GuiButton((Rectangle){x, y, btnW, btnH},
                  GuiIconText(ICON_EXIT, "Quit"))) {
        ctx->mode = -1; /* sentinel: caller checks WindowShouldClose-like */
    }

    int info_y = y + btnH + 40;
    DrawText(TextFormat("Maze:   %s", ctx->maze_loaded ? GetFileName(ctx->maze_path)   : "(none)"),
             x, info_y, 18, ctx->maze_loaded   ? GREEN : GRAY); info_y += 22;
    DrawText(TextFormat("Qtable: %s", ctx->agent_loaded ? GetFileName(ctx->qtable_path) : "(none)"),
             x, info_y, 18, ctx->agent_loaded ? GREEN : GRAY);
}

/* ------------------------------------------------------------------ */
/*  EDITOR                                                             */
/* ------------------------------------------------------------------ */
static int drawEditorTopBar(AppContext* ctx, bool* lockEdit) {
    int sw = GetScreenWidth();
    const int bh = 30, gap = 4;
    int x = 0, y = 0;
    #define EADV(w) do { x += (w) + gap; if (x + 160 > sw) { x = 0; y += bh + 2; } } while (0)
    const float lscale = 1.67;
    if (GuiButton((Rectangle){x, y, lscale*100, bh}, GuiIconText(ICON_ARROW_LEFT_FILL, "Menu"))) {
        ctx->mode = APP_MODE_MENU;
    } EADV(lscale*100);
    if (GuiButton((Rectangle){x, y, lscale*180, bh}, GuiIconText(ICON_FILE_OPEN, "Open Maze"))) {
        openFileDialog(DLG_LOAD_MAZE, false, NULL); *lockEdit = true;
    } EADV(lscale*180);
    if (GuiButton((Rectangle){x, y, lscale*200, bh}, GuiIconText(ICON_FILE_NEW, "Generate Maze"))) {
        g_genForm.windowActive = true; *lockEdit = true;
    } EADV(lscale*200);
    if (GuiButton((Rectangle){x, y, lscale*180, bh}, GuiIconText(ICON_FILE_SAVE, "Save Maze"))) {
        openFileDialog(DLG_SAVE_MAZE, true, ctx->maze_path); *lockEdit = true;
    } EADV(lscale*180);
    #undef EADV
    return y + bh + UI_BTN_SPACING;
}

static void runEditor(AppContext* ctx) {
    static bool lockEdit = false;

    /* update */
    if (g_genForm.createPressed) {
        g_genForm.createPressed = false;
        int rows = atoi(g_genForm.rowsText);
        int cols = atoi(g_genForm.colsText);
        if (rows > 0 && cols > 0) {
            if (ctx->maze_loaded) freeMaze(&ctx->ir);
            ctx->ir = generateMaze(rows, cols);
            ctx->maze_loaded = true;
            strncpy(ctx->maze_path, g_genForm.mazeName, sizeof(ctx->maze_path) - 1);
            appContextRefreshSize(ctx);
            g_genForm.windowActive = false;
            lockEdit = false;
            appSaveState(ctx);
        } else {
            APP_POPUP(ctx, "Invalid generate parameters");
        }
    } else if (g_genForm.cancelPressed) {
        g_genForm.cancelPressed = false;
        g_genForm.windowActive  = false;
        lockEdit = false;
    }

    if (updateMazeClickedCell(&ctx->render, &ctx->ir)) {
        appContextRefreshSize(ctx);
    }

    /* render */
    renderMaze(&ctx->render, &ctx->ir, lockEdit || g_fdlg.windowActive);

    if (g_fdlg.windowActive) GuiLock();
        int row_bottom = drawEditorTopBar(ctx, &lockEdit);

        int ix = UI_LEFT_X, iy = row_bottom;
        DrawText(TextFormat("Map:  %s", GetFileName(ctx->maze_path)),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Rows: %zu", ctx->ir.rows),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Cols: %zu", ctx->ir.cols),
                 ix, iy, LABEL_TEXT_SIZE, WHITE);
    GuiUnlock();

    drawGenerateFormWindow(&g_genForm);
}

/* ------------------------------------------------------------------ */
/*  VIEWER                                                             */
/* ------------------------------------------------------------------ */
static bool isAgentRunable(const MazeInternalRepr* ir, const Agent* a) {
    if (!ir || !a) return false;
    if (ir->rows == 0 || ir->cols == 0) return false;
    return ir->rows == a->q_table.len_state_y
        && ir->cols == a->q_table.len_state_x
        && ACTION_N_ACTIONS == a->q_table.len_state_actions;
}

static bool moveAgentToClickedCell(Agent* agent, MazeRenderCtx* r, MazeInternalRepr* ir, MouseButton mbtn) {
    if (!IsMouseButtonPressed(mbtn)) return false;
    CellId c = getMouseCell(r, ir);
    if (c.row == (size_t)-1 || c.col == (size_t)-1) return false;
    agentRestart(agent);
    agent->current_s.x = (int32_t)c.col;
    agent->current_s.y = (int32_t)c.row;
    return true;
}

static void renderAgentDot(const Agent* a, const MazeRenderCtx* r) {
    int posx = (r->offset_x + r->mouse_offset_x) + a->current_s.x * r->cr_lenght;
    int posy = (r->offset_y + r->mouse_offset_y) + a->current_s.y * r->cr_lenght;
    int cx = posx + r->cr_lenght / 2, cy = posy + r->cr_lenght / 2;
    int rad = (int)(r->cr_lenght * 0.35f);
    if (rad < 2) rad = 2;
    DrawCircle(cx, cy, rad, YELLOW);
}

static void viewerStepOnce(AppContext* ctx) {
    Agent* ag = &ctx->agent;
    MazeEnv* env = &ctx->ir;
    size_t MAX_STEPS = ag->q_table.len_state_x * ag->q_table.len_state_y * ag->q_table.len_state_actions;

    if (g_view.is_goal || g_view.steps_taken > MAX_STEPS) {
        agentRestart(ag);
        g_view.accum_q     = 0.0f;
        g_view.steps_taken = 0;
        g_view.is_goal     = false;
        g_view.agent_steps.count = 0;
        return;
    }
    agentPolicy(ag, env);
    g_view.accum_q += getQtableValue(ag, ag->current_s, ag->policy_action);

    /* record step if tracking */
    if (g_view.track_steps) {
        da_append(&g_view.agent_steps,
                  ((agentStep){.s = ag->current_s, .a = ag->policy_action}));
    }

    state_t next = GetNextState(ag->current_s, ag->policy_action);
    stepResult sr = stepIntoState(env, next, ctx->walls_count, ctx->opens_count);
    g_view.steps_taken++;

    if (sr.terminal) {
        if (sr.isGoal) { agentUpdateState(ag, next); g_view.is_goal = true; }
        else g_view.is_goal = false;
        return;
    }
    agentUpdateState(ag, next);
    g_view.is_goal = sr.isGoal;
}

static void runViewer(AppContext* ctx) {
    static bool lockEdit = false;

    if (g_view.update_interval <= 0.0) g_view.update_interval = 0.5;

    /* ---- hotkeys ---- */
    if (IsKeyPressed(KEY_RIGHT)) g_view.update_interval *= 0.5;
    if (IsKeyPressed(KEY_LEFT))  g_view.update_interval *= 2.0;
    if (IsKeyPressed(KEY_P))     g_view.paused_on_goal = !g_view.paused_on_goal;

    /* T — toggle step tracking */
    if (IsKeyPressed(KEY_T)) {
        g_view.track_steps = !g_view.track_steps;
        if (!g_view.track_steps) g_view.agent_steps.count = 0;
    }

    /* Z — toggle lock camera to agent */
    if (IsKeyPressed(KEY_Z)) {
        g_view.lock_camera = !g_view.lock_camera;
        if (g_view.lock_camera) {
            g_view.prev_mouse_offset_x = ctx->render.mouse_offset_x;
            g_view.prev_mouse_offset_y = ctx->render.mouse_offset_y;
            g_view.cam_from_x = ctx->render.mouse_offset_x;
            g_view.cam_from_y = ctx->render.mouse_offset_y;

            ctx->render.width  = GetScreenWidth();
            ctx->render.heigth = GetScreenHeight();
            UpdateCellRectParams(&ctx->render, &ctx->ir);

            compute_camera_target_for_agent(&ctx->render, &ctx->ir, &ctx->agent,
                                            &g_view.cam_target_x, &g_view.cam_target_y);

            g_view.cam_transition_duration = fmax(g_view.update_interval * 0.5, 0.05);
            g_view.cam_transition_start    = GetTime();
            g_view.cam_transition_active   = true;
        } else {
            g_view.cam_transition_active = false;
            ctx->render.mouse_offset_x = g_view.prev_mouse_offset_x;
            ctx->render.mouse_offset_y = g_view.prev_mouse_offset_y;
        }
    }

    /* ---- right-click to teleport agent ---- */
    if (g_view.running && !g_fdlg.windowActive && !ctx->popup_active) {
        if (moveAgentToClickedCell(&ctx->agent, &ctx->render, &ctx->ir, MOUSE_BUTTON_RIGHT)) {
            g_view.steps_taken       = 0;
            g_view.is_goal           = false;
            g_view.accum_q           = 0.0f;
            g_view.agent_steps.count = 0;

            if (g_view.lock_camera) {
                g_view.cam_from_x = ctx->render.mouse_offset_x;
                g_view.cam_from_y = ctx->render.mouse_offset_y;
                ctx->render.width  = GetScreenWidth();
                ctx->render.heigth = GetScreenHeight();
                UpdateCellRectParams(&ctx->render, &ctx->ir);
                compute_camera_target_for_agent(&ctx->render, &ctx->ir, &ctx->agent,
                                                &g_view.cam_target_x, &g_view.cam_target_y);
                g_view.cam_transition_duration = fmax(g_view.update_interval * 0.5, 0.05);
                g_view.cam_transition_start    = GetTime();
                g_view.cam_transition_active   = true;
                g_view.prev_agent_pos = ctx->agent.current_s;
            }
        }
    }

    /* ---- step the agent ---- */
    if (g_view.running) {
        double now = GetTime();
        if (now - g_view.last_update >= g_view.update_interval) {
            if (!(g_view.paused_on_goal && g_view.is_goal)) viewerStepOnce(ctx);
            g_view.last_update = now;

            /* after stepping, trigger camera transition if locked and agent moved */
            if (g_view.lock_camera) {
                state_t cur = ctx->agent.current_s;
                if (cur.x != g_view.prev_agent_pos.x || cur.y != g_view.prev_agent_pos.y) {
                    g_view.cam_from_x = ctx->render.mouse_offset_x;
                    g_view.cam_from_y = ctx->render.mouse_offset_y;

                    ctx->render.width  = GetScreenWidth();
                    ctx->render.heigth = GetScreenHeight();
                    UpdateCellRectParams(&ctx->render, &ctx->ir);

                    compute_camera_target_for_agent(&ctx->render, &ctx->ir, &ctx->agent,
                                                    &g_view.cam_target_x, &g_view.cam_target_y);

                    g_view.cam_transition_duration = fmax(g_view.update_interval * 0.5, 0.05);
                    g_view.cam_transition_start    = now;
                    g_view.cam_transition_active   = true;
                }
                g_view.prev_agent_pos = cur;
            }
        }
    }

    /* ---- render ---- */

    /* Z-mode: handle zoom with scroll wheel while camera is locked */
    if (g_view.lock_camera) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            Vector2 mPos = GetMousePosition();
            const float base = 1.25f;
            float targetZ = ctx->render.zoom_alpha * powf(base, wheel);
            targetZ = clampf(targetZ, 0.001f, 100.0f);
            float new_zoom = ctx->render.zoom_alpha + (targetZ - ctx->render.zoom_alpha) * 0.3f;

            float cell_px_before = ctx->render.cr_lenght > 0 ? (float)ctx->render.cr_lenght : 1.0f;
            float gx = (mPos.x - ctx->render.offset_x - ctx->render.mouse_offset_x) / cell_px_before;
            float gy = (mPos.y - ctx->render.offset_y - ctx->render.mouse_offset_y) / cell_px_before;

            ctx->render.zoom_alpha = new_zoom;
            UpdateCellRectParams(&ctx->render, &ctx->ir);

            float cell_px_after = (float)ctx->render.cr_lenght;
            ctx->render.mouse_offset_x = (int)(mPos.x - ctx->render.offset_x - gx * cell_px_after);
            ctx->render.mouse_offset_y = (int)(mPos.y - ctx->render.offset_y - gy * cell_px_after);
        }

        ctx->render.width  = GetScreenWidth();
        ctx->render.heigth = GetScreenHeight();
        UpdateCellRectParams(&ctx->render, &ctx->ir);

        /* smooth camera transition (smoothstep interpolation) */
        if (g_view.cam_transition_active) {
            double now = GetTime();
            double elapsed = now - g_view.cam_transition_start;
            if (elapsed >= g_view.cam_transition_duration) {
                ctx->render.mouse_offset_x = g_view.cam_target_x;
                ctx->render.mouse_offset_y = g_view.cam_target_y;
                g_view.cam_transition_active = false;
            } else {
                float t = (float)(elapsed / g_view.cam_transition_duration);
                float s = t * t * (3.0f - 2.0f * t); /* smoothstep */
                ctx->render.mouse_offset_x = (int)(g_view.cam_from_x + s * (g_view.cam_target_x - g_view.cam_from_x));
                ctx->render.mouse_offset_y = (int)(g_view.cam_from_y + s * (g_view.cam_target_y - g_view.cam_from_y));
            }
        } else {
            compute_camera_target_for_agent(&ctx->render, &ctx->ir, &ctx->agent,
                                            &g_view.cam_target_x, &g_view.cam_target_y);
            ctx->render.mouse_offset_x = g_view.cam_target_x;
            ctx->render.mouse_offset_y = g_view.cam_target_y;
        }

        renderMaze(&ctx->render, &ctx->ir, true); /* lock editing while camera-locked */
    } else {
        renderMaze(&ctx->render, &ctx->ir, lockEdit || g_fdlg.windowActive);
    }

    /* draw tracked arrows (T) */
    if (g_view.track_steps) {
        renderTrackedArrows(&g_view.agent_steps, &ctx->render);
    }

    if (g_view.running) renderAgentDot(&ctx->agent, &ctx->render);

    /* ---- GUI overlay ---- */
    if (g_fdlg.windowActive) GuiLock();
        int vsw = GetScreenWidth();
        const int bh = 30, vgap = 4;
        int vx = 0, vy = 0;
        const float lscale = 1.67;
        #define VADV(w) do { vx += (w) + vgap; if (vx + 160 > vsw) { vx = 0; vy += bh + 2; } } while (0)

        if (GuiButton((Rectangle){vx, vy, lscale*100, bh}, GuiIconText(ICON_ARROW_LEFT_FILL, "Menu"))) {
            ctx->mode = APP_MODE_MENU;
        } VADV(lscale*100);
        if (GuiButton((Rectangle){vx, vy, lscale*180, bh}, GuiIconText(ICON_FILE_OPEN, "Load Maze"))) {
            openFileDialog(DLG_LOAD_MAZE, false, NULL); lockEdit = true;
        } VADV(lscale*180);
        if (GuiButton((Rectangle){vx, vy, lscale*180, bh}, GuiIconText(ICON_FILE_OPEN, "Load Qtable"))) {
            openFileDialog(DLG_LOAD_QTABLE, false, NULL); lockEdit = true;
        } VADV(lscale*180);
        if (GuiButton((Rectangle){vx, vy, lscale*180, bh},
                      GuiIconText(g_view.running ? ICON_PLAYER_STOP : ICON_PLAYER_PLAY,
                                  g_view.running ? "Stop" : "Run Agent"))) {
            if (!g_view.running) {
                if (!isAgentRunable(&ctx->ir, &ctx->agent)) {
                    APP_POPUP(ctx, "Agent qtable does not match current maze");
                } else {
                    ctx->agent.epsilon = 0.0f;
                    agentRestart(&ctx->agent);
                    g_view.steps_taken = 0;
                    g_view.is_goal     = false;
                    g_view.accum_q     = 0.0f;
                    g_view.agent_steps.count = 0;
                    g_view.running     = true;
                }
            } else {
                g_view.running = false;
            }
        } VADV(180);
        #undef VADV

        int ix = UI_LEFT_X, iy = vy + bh + UI_BTN_SPACING;
        DrawText(TextFormat("Update: %.3fs", g_view.update_interval),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Steps:  %zu",   g_view.steps_taken),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Cum Q:  %.3f",  (double)g_view.accum_q),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Pause on goal: %s", g_view.paused_on_goal ? "ON" : "OFF"),
                 ix, iy, LABEL_TEXT_SIZE, g_view.paused_on_goal ? RED : GREEN);
                 iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Track steps [T]: %s", g_view.track_steps ? "ON" : "OFF"),
                 ix, iy, LABEL_TEXT_SIZE, g_view.track_steps ? GREEN : GRAY);
                 iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Lock camera [Z]: %s", g_view.lock_camera ? "ON" : "OFF"),
                 ix, iy, LABEL_TEXT_SIZE, g_view.lock_camera ? GREEN : GRAY);
                 iy += LABEL_TEXT_SIZE + 16;
        DrawText(TextFormat("Map:    %s", GetFileName(ctx->maze_path)),
                 ix, iy, LABEL_TEXT_SIZE, WHITE); iy += LABEL_TEXT_SIZE + 8;
        DrawText(TextFormat("Qtable: %s", GetFileName(ctx->qtable_path)),
                 ix, iy, LABEL_TEXT_SIZE, WHITE);
    GuiUnlock();
}

/* ------------------------------------------------------------------ */
/*  TRAINER                                                            */
/* ------------------------------------------------------------------ */
static void drawIntInput(Rectangle r, const char* label, int* val, bool* edit,
                          char* buf, size_t buf_sz) {
    DrawText(label, (int)r.x, (int)r.y - 26, 18, RAYWHITE);
    if (!*edit) snprintf(buf, buf_sz, "%d", *val);
    if (GuiTextBox(r, buf, (int)buf_sz, *edit)) *edit = !*edit;
    if (!*edit) {
        int v = atoi(buf);
        if (v > 0) *val = v;
    }
}

static void drawFloatInput(Rectangle r, const char* label, float* val, bool* edit,
                           char* buf, size_t buf_sz) {
    DrawText(label, (int)r.x, (int)r.y - 26, 18, RAYWHITE);
    if (!*edit) snprintf(buf, buf_sz, "%g", (double)*val);
    if (GuiTextBox(r, buf, (int)buf_sz, *edit)) *edit = !*edit;
    if (!*edit) {
        float v = (float)atof(buf);
        if (v >= 0.0f) *val = v;
    }
}

static void runTrainer(AppContext* ctx) {
    /* drive training */
    if (g_train.running && !g_train.paused) {
        for (int i = 0; i < g_train.episodes_per_frame && g_train.running; i++) {
            trainOneEpisode(ctx);
        }
    }

    /* layout */
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int top_h = 90;

    ClearBackground((Color){18, 22, 30, 255});

    /* top control bar — flow from left, wrap if needed */
    int bx = 10, by = 10, bh = 32, bgap = 6;
    #define ADV(w) do { bx += (w) + bgap; if (bx + 120 > sw) { bx = 10; by += bh + 4; } } while (0)
    const float lscale = 1.67;
    if (GuiButton((Rectangle){bx, by, lscale*110, bh},
                  GuiIconText(ICON_ARROW_LEFT_FILL, "Menu"))) {
        ctx->mode = APP_MODE_MENU;
    } ADV(lscale*110);
    if (GuiButton((Rectangle){bx, by, lscale*130, bh},
                  GuiIconText(ICON_FILE_OPEN, "Load Maze"))) {
        openFileDialog(DLG_LOAD_MAZE, false, NULL);
    } ADV(lscale*130);
    if (GuiButton((Rectangle){bx, by, lscale*130, bh},
                  GuiIconText(g_train.running
                                ? (g_train.paused ? ICON_PLAYER_PLAY : ICON_PLAYER_PAUSE)
                                : ICON_PLAYER_PLAY,
                              g_train.running
                                ? (g_train.paused ? "Resume" : "Pause")
                                : (g_train.done ? "Restart" : "Start")))) {
        if (!g_train.running) trainStart(ctx);
        else g_train.paused = !g_train.paused;
    } ADV(lscale*130);
    if (GuiButton((Rectangle){bx, by, lscale*110, bh},
                  GuiIconText(ICON_PLAYER_STOP, "Reset"))) {
        trainFreeMetrics(&g_train);
    } ADV(lscale*110);

    bool can_save = g_train.allocated && g_train.cur_episode > 0;
    if (!can_save) GuiLock();
    if (GuiButton((Rectangle){bx, by, lscale*130, bh},
                  GuiIconText(ICON_FILE_SAVE, "Save CSV"))) {
        openFileDialog(DLG_SAVE_METRICS, true,
                       ctx->metrics_path[0] ? ctx->metrics_path : "metrics.csv");
    } ADV(lscale*130);
    if (GuiButton((Rectangle){bx, by, lscale*140, bh},
                  GuiIconText(ICON_FILE_SAVE, "Save Qtable"))) {
        openFileDialog(DLG_SAVE_QTABLE, true,
                       ctx->qtable_path[0] ? ctx->qtable_path : "agent.qtable");
    } ADV(lscale*140);
    if (GuiButton((Rectangle){bx, by, lscale*170, bh},
                  GuiIconText(ICON_PLAYER_NEXT, "Open in Viewer"))) {
        ctx->mode = APP_MODE_VIEWER;
        memset(&g_view, 0, sizeof(g_view));
    } ADV(lscale*170);
    if (!can_save) GuiUnlock();
    #undef ADV

    /* if button row wrapped, push remaining content down */
    int row_bottom = by + bh + 6;

    /* hyper-param strip */
    int hy = row_bottom;
    DrawText(TextFormat("Maze: %s  (%zux%zu)",
                        ctx->maze_loaded ? GetFileName(ctx->maze_path) : "(none)",
                        ctx->ir.rows, ctx->ir.cols),
             10, hy, 16, ctx->maze_loaded ? GREEN : RED);
    DrawText(TextFormat("Episode %zu / %zu   goals=%zu   eps=%.3f",
                        g_train.cur_episode, ctx->num_episodes,
                        g_train.goals_count, ctx->agent.epsilon),
             10, hy + 22, 16, RAYWHITE);
    DrawText(TextFormat("alpha=%.4f   gamma=%.4f   eps_decay=%.1f",
                        (double)ctx->learning_rate,
                        (double)ctx->discount_factor,
                        (double)ctx->epsilon_decay),
             10, hy + 44, 16, RAYWHITE);

    /* hyper-param controls — dock to the right side */
    static bool ed_ep = false, ed_st = false, ed_epf = false;
    static bool ed_lr = false, ed_df = false, ed_ed = false;
    static char buf_ep[32], buf_st[32], buf_epf[32];
    static char buf_lr[32], buf_df[32], buf_ed[32];

    /* tweak these to resize the trainer metric input boxes */
    const float field_w_mult     = 1.67f;
    const float field_h_mult     = 1.0f;
    const float field_gap_mult   = 1.67f;
    const float row_spacing_mult = 1.0f;
    const int   field_w_base     = 100;
    const int   field_h_base     = 28;
    const int   field_gap_base   = 140;
    const int   row_spacing_base = 30;

    int field_w     = (int)(field_w_base     * field_w_mult);
    int field_h     = (int)(field_h_base     * field_h_mult);
    int field_gap   = (int)(field_gap_base   * field_gap_mult);
    int row_spacing = (int)(row_spacing_base * row_spacing_mult);

    int ixx = sw - 3 * field_gap - 10;
    if (ixx < 10) ixx = 10;
    int iyy = hy;
    int iyy2 = iyy + field_h + row_spacing;

    top_h = iyy2 + field_h + 16;
    int ne = (int)ctx->num_episodes;
    int ms = (int)ctx->max_steps;

    /* trainer-only: white text inside the input boxes */
    int saved_tc_normal  = GuiGetStyle(TEXTBOX, TEXT_COLOR_NORMAL);
    int saved_tc_focused = GuiGetStyle(TEXTBOX, TEXT_COLOR_FOCUSED);
    int saved_tc_pressed = GuiGetStyle(TEXTBOX, TEXT_COLOR_PRESSED);
    GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL,  ColorToInt(RAYWHITE));
    GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, ColorToInt(WHITE));
    GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, ColorToInt(WHITE));

    drawIntInput((Rectangle){ixx,                  iyy, field_w, field_h}, "episodes",
                 &ne, &ed_ep, buf_ep, sizeof(buf_ep));
    drawIntInput((Rectangle){ixx + field_gap,      iyy, field_w, field_h}, "max_steps",
                 &ms, &ed_st, buf_st, sizeof(buf_st));
    drawIntInput((Rectangle){ixx + 2 * field_gap,  iyy, field_w, field_h}, "ep/frame",
                 &g_train.episodes_per_frame, &ed_epf, buf_epf, sizeof(buf_epf));
    if (g_train.episodes_per_frame < 1) g_train.episodes_per_frame = 1;

    if (g_train.running) GuiLock();
    drawFloatInput((Rectangle){ixx,                 iyy2, field_w, field_h}, "alpha",
                   &ctx->learning_rate,   &ed_lr, buf_lr, sizeof(buf_lr));
    drawFloatInput((Rectangle){ixx + field_gap,     iyy2, field_w, field_h}, "gamma",
                   &ctx->discount_factor, &ed_df, buf_df, sizeof(buf_df));
    drawFloatInput((Rectangle){ixx + 2 * field_gap, iyy2, field_w, field_h}, "eps_decay",
                   &ctx->epsilon_decay,   &ed_ed, buf_ed, sizeof(buf_ed));
    if (g_train.running) GuiUnlock();

    GuiSetStyle(TEXTBOX, TEXT_COLOR_NORMAL,  saved_tc_normal);
    GuiSetStyle(TEXTBOX, TEXT_COLOR_FOCUSED, saved_tc_focused);
    GuiSetStyle(TEXTBOX, TEXT_COLOR_PRESSED, saved_tc_pressed);

    if (!g_train.running) {
        ctx->num_episodes = (size_t)(ne > 0 ? ne : 1);
        ctx->max_steps    = (size_t)(ms > 0 ? ms : 1);
    }

    int gx = 20, gy = top_h + 20;
    int gw = (sw - 60) / 2;
    int gh = (sh - top_h - 60) / 2;

    Rectangle rR = {gx,           gy,           gw, gh};
    Rectangle rS = {gx + gw + 20, gy,           gw, gh};
    Rectangle rL = {gx,           gy + gh + 20, gw, gh};
    Rectangle rT = {gx + gw + 20, gy + gh + 20, gw, gh};

    size_t n = g_train.cur_episode;

    /* smooth every series with a zero-padded trailing window whose length
     * is g_train.episodes_per_frame (reuses the speed knob as a bias knob) */
    {
        static float sm_R[8192], sm_S[8192], sm_L[8192], sm_T[8192];
        int   win  = g_train.episodes_per_frame > 0 ? g_train.episodes_per_frame : 1;
        size_t take = n > 8192 ? 8192 : n;
        size_t skip = n - take;

        if (g_train.allocated && take > 0) {
            smoothToF_from_f(g_train.rewards      + skip, sm_R, take, win);
            smoothToF_from_d(g_train.success_rate + skip, sm_S, take, win);
            smoothToF_from_d(g_train.loss         + skip, sm_L, take, win);
            smoothToF_from_z(g_train.steps        + skip, sm_T, take, win);
            drawLineGraphF(rR, sm_R, take, "Reward / episode",            SKYBLUE, skip);
            drawLineGraphF(rS, sm_S, take, "Success rate (%) [rolling]",  LIME,    skip);
            drawLineGraphF(rL, sm_L, take, "Huber loss / episode",        RED,     skip);
            drawLineGraphF(rT, sm_T, take, "Steps / episode",             ORANGE,  skip);
        } else {
            drawLineGraphF(rR, NULL, 0, "Reward / episode",           SKYBLUE, 0);
            drawLineGraphF(rS, NULL, 0, "Success rate (%) [rolling]", LIME,    0);
            drawLineGraphF(rL, NULL, 0, "Huber loss / episode",       RED,     0);
            drawLineGraphF(rT, NULL, 0, "Steps / episode",            ORANGE,  0);
        }
    }

    if (g_train.done) {
        const char* msg = "Training complete";
        int tw = MeasureText(msg, 22);
        DrawText(msg, (sw - tw) / 2, top_h - 24, 22, GREEN);
    }
}

/* ------------------------------------------------------------------ */
/*  Common dialog handler                                              */
/* ------------------------------------------------------------------ */
static void handleFileDialogResult(AppContext* ctx) {
    if (!g_fdlg.SelectFilePressed) return;

    char path[APP_PATH_MAX];
    snprintf(path, sizeof(path), "%s" PATH_SEPERATOR "%s",
             g_fdlg.dirPathText, g_fdlg.fileNameText);

    switch (g_dlg_target) {
    case DLG_LOAD_MAZE:
        if (!appLoadMazeFile(ctx, path)) APP_POPUP(ctx, "Could not load maze file");
        else                              appSaveState(ctx);
        break;

    case DLG_SAVE_MAZE:
        if (!IsFileExtension(path, ".maze") && !IsFileExtension(path, ".npy")) {
            APP_POPUP(ctx, "Maze must end in .maze or .npy");
        } else if (!appSaveMazeFile(ctx, path)) {
            APP_POPUP(ctx, "Failed to save maze");
        } else {
            strncpy(ctx->maze_path, path, sizeof(ctx->maze_path) - 1);
            APP_POPUP(ctx, "Maze saved");
            appSaveState(ctx);
        }
        break;

    case DLG_LOAD_QTABLE:
        if (!appLoadQtableFile(ctx, path)) APP_POPUP(ctx, "Could not load qtable");
        else                                appSaveState(ctx);
        break;

    case DLG_SAVE_QTABLE:
        if (!IsFileExtension(path, ".qtable")) {
            APP_POPUP(ctx, "Qtable must end in .qtable");
        } else if (!appSaveQtableFile(ctx, path)) {
            APP_POPUP(ctx, "Failed to save qtable");
        } else {
            strncpy(ctx->qtable_path, path, sizeof(ctx->qtable_path) - 1);
            APP_POPUP(ctx, "Qtable saved");
            appSaveState(ctx);
        }
        break;

    case DLG_SAVE_METRICS:
        if (!IsFileExtension(path, ".csv")) {
            APP_POPUP(ctx, "Metrics must end in .csv");
        } else if (!saveMetricsCSV(ctx, path)) {
            APP_POPUP(ctx, "Failed to save metrics");
        } else {
            strncpy(ctx->metrics_path, path, sizeof(ctx->metrics_path) - 1);
            APP_POPUP(ctx, "Metrics CSV saved");
            appSaveState(ctx);
        }
        break;

    default: break;
    }

    g_fdlg.SelectFilePressed = false;
    g_fdlg.saveFileMode      = false;
    g_dlg_target             = DLG_NONE;
}

/* ------------------------------------------------------------------ */
/*  main                                                               */
/* ------------------------------------------------------------------ */
int main(int argc, char** argv) {
    (void)argc; (void)argv;

    AppContext ctx;
    appContextInit(&ctx);
    appLoadState(&ctx);

    if (ctx.maze_path[0])   appLoadMazeFile  (&ctx, ctx.maze_path);
    if (ctx.qtable_path[0]) appLoadQtableFile(&ctx, ctx.qtable_path);

    g_train.episodes_per_frame = 5;

    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WINDOW_W, WINDOW_H, "cqlearning");
    SetTargetFPS(60);

    setCustomStyle();
    g_fdlg    = InitGuiWindowFileDialog(GetWorkingDirectory());
    g_genForm = initGenerateFormState("random.maze", 10, 10);

    
    while (!WindowShouldClose() && ctx.mode != (AppMode)-1) {
        BeginDrawing();

            switch (ctx.mode) {
            case APP_MODE_MENU:    drawMenu  (&ctx); break;
            case APP_MODE_EDITOR:  runEditor (&ctx); break;
            case APP_MODE_VIEWER:  runViewer (&ctx); break;
            case APP_MODE_TRAINER: runTrainer(&ctx); break;
            default:               drawMenu  (&ctx); break;
            }

            handleFileDialogResult(&ctx);

            setDefaultStyle();
                GuiWindowFileDialog(&g_fdlg);
            setCustomStyle();

            drawPopup(&ctx);

        EndDrawing();
    }

    appSaveState(&ctx);
    trainFreeMetrics(&g_train);
    free(g_view.agent_steps.items); /* free tracked steps */
    if (ctx.maze_loaded) freeMaze(&ctx.ir);
    if (ctx.agent.q_table.vals) free(ctx.agent.q_table.vals);

    CloseWindow();
    return 0;
}