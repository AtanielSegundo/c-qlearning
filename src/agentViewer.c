#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "raylib.h"
#include "raygui.h"

#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"
#undef MAZE_IR_IMPLEMENTATION

#define MAZE_RENDER_IMPLEMENTATION
#include "mazeRender.h"

#define AGENT_IMPLEMENTATION
#include "agent.h"

#define UI_IMPLEMENTATION
#include "UI.h"

#define WINDOW_WIDTH  1280U
#define WINDOW_HEIGTH  720U

#define POP_UP_MSG(const_msg) save_popup_active = true; snprintf(save_popup_msg, sizeof(save_popup_msg), (const_msg))

typedef enum{
	MAZE,
	QTABLE
} DIALOG_TARGET;


void drawPopUpMsg(bool* save_popup_active,char* save_popup_msg){
	GuiSetStyle(DEFAULT, TEXT_SIZE, 32);           
	GuiSetStyle(BUTTON, TEXT_SIZE, 26);            
	GuiSetStyle(DEFAULT, TEXT_SPACING, 6);         
	GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
	
	DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.45f));

	int modal_w = 640;
	int modal_h = 140;
	int modal_x = (GetScreenWidth() - modal_w) / 2;
	int modal_y = (GetScreenHeight() - modal_h) / 2;

	DrawRectangle(modal_x, modal_y, modal_w, modal_h, LIGHTGRAY);
	DrawRectangleLines(modal_x, modal_y, modal_w, modal_h, GRAY);

	int padding = 12;
	DrawTextEx(GetFontDefault(), save_popup_msg, (Vector2){ modal_x + padding, modal_y + padding + 4 }, 18, 1, BLACK);

	Rectangle okBtn = (Rectangle){ modal_x + (modal_w - 100) / 2, modal_y + modal_h - 44, 100, 32 };
	if (GuiButton(okBtn, "OK")) {
		*save_popup_active = false;
		memset(save_popup_msg, 0, sizeof(save_popup_msg));
	}	
}

// ======================
// STATE SAVE/LOAD (.agent_viewer_state)
// ======================

static void save_viewer_state(const char *filename,
                              const char *maze_path,
                              const char *agent_path,
                              bool lockMazeEditing) {
    FILE *f = fopen(filename, "w");
    if (!f) return;

    fprintf(f, "maze_path=%s\n", maze_path);
    fprintf(f, "agent_path=%s\n", agent_path);
    fprintf(f, "lockMazeEditing=%d\n", lockMazeEditing ? 1 : 0);

    fclose(f);
}

static void load_viewer_state(const char *filename,
                              char *maze_path,
                              size_t maze_sz,
                              char *agent_path,
                              size_t agent_sz,
                              bool *lockMazeEditing) {
    FILE *f = fopen(filename, "r");
    if (!f) return;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "maze_path=%511[^\n]", maze_path) == 1) continue;
        if (sscanf(line, "agent_path=%511[^\n]", agent_path) == 1) continue;

        int lock = 0;
        if (sscanf(line, "lockMazeEditing=%d", &lock) == 1) {
            *lockMazeEditing = (lock != 0);
            continue;
        }
    }

    fclose(f);
}

bool isAgentRunable(MazeInternalRepr* ir,Agent* agent){
	if(ir->rows != agent->q_table.len_state_y || ir->cols != agent->q_table.len_state_x || ACTION_N_ACTIONS != agent->q_table.len_state_actions){
		return false;
	} else {
		return true;
	}
};

void updateAgent(Agent* agent, MazeInternalRepr* ir, size_t* steps_taken, bool* isGoal){
    if(!agent || !ir || !steps_taken || !isGoal) return;

    size_t MAX_STEPS_PER_EPISODE = agent->q_table.len_state_x * agent->q_table.len_state_y * agent->q_table.len_state_actions;

    if(*isGoal || *steps_taken > MAX_STEPS_PER_EPISODE){
        agentRestart(agent);
        *steps_taken = 0;
        *isGoal = false;
        return;
    }

    agentPolicy(agent, ir);
    state_t next = GetNextState(agent->current_s, agent->policy_action);
    stepResult sr = stepIntoState(ir, next);
	agent->accum_reward += sr.reward;
    *steps_taken = *steps_taken + 1;

    if (sr.terminal) {
        if (sr.isGoal) {
            agentUpdateState(agent, next);
            *isGoal = true;
        } else {
			printf("[ERROR] OUT OF BOUNDS");
            *isGoal = false;
        }
        return;
    }

    agentUpdateState(agent, next);
    *isGoal = sr.isGoal;
}

void renderAgent(Agent* agent, MazeRenderCtx* render){
    if (!agent || !render) return;

    state_t agent_state = agent->current_s;

    int posx = (render->offset_x + render->mouse_offset_x) + agent_state.x * render->cr_lenght;
    int posy = (render->offset_y + render->mouse_offset_y) + agent_state.y * render->cr_lenght;

    int center_x = posx + render->cr_lenght / 2;
    int center_y = posy + render->cr_lenght / 2;

    int radius = (int)(render->cr_lenght * 0.35f);
    if (radius < 2) radius = 2;

    DrawCircle(center_x, center_y, radius, YELLOW);
}

void moveAgentToClickedCell(Agent* agent,MazeRenderCtx *r,MazeInternalRepr *ir,MouseButton mbtn){
	Vector2 dt = GetMouseDelta();
	if(IsMouseButtonPressed(mbtn)){
		CellId c = getMouseCell(r,ir);
		if(c.row >= 0 && c.col >= 0){
			agentRestart(agent);
			agent->current_s.x = c.col;
			agent->current_s.y = c.row;
		}
	}
}

int main(int argc, char const *argv[])
{
	SetTraceLogLevel(LOG_WARNING); 

	MazeInternalRepr ir  = {0};
	MazeRenderCtx render = {0};
	Agent agent = {0};
	
	char maze_path[512]  = {0};
	char agent_path[512] = {0};
	char save_popup_msg[256] = {0};
	
	bool save_popup_active = false;
	bool lockMazeEditing = false;
	bool runAgent = false;
	
	bool lockCameraToAgent = false;
	int prev_mouse_offset_x = 0;
	int prev_mouse_offset_y = 0;

	// transição suave
	bool camera_transition_active = false;
	double camera_transition_start = 0.0;
	double camera_transition_duration = 0.0;
	int camera_transition_from_x = 0;
	int camera_transition_from_y = 0;
	
	double lastUpdate = 0.0;
	double updateInterval = 1.0f;

	DIALOG_TARGET dialog_target = MAZE;
	
	size_t steps_taken = 0;
	bool isAgentGoal = false;
	
	load_viewer_state(".agent_viewer_state", maze_path, sizeof(maze_path),
	agent_path, sizeof(agent_path), &lockMazeEditing);
	
	if(FileExists(maze_path)){
		bool isNumpy = IsFileExtension(maze_path, ".npy");
		bool isRawMaze = IsFileExtension(maze_path, ".maze");
		if (isNumpy || isRawMaze){
			if(isNumpy) readMazeNumpy(maze_path,&ir);
			else if (isRawMaze) readMazeRaw(maze_path,&ir);	
		}
	} 

	if(FileExists(agent_path)){
		bool isQtable = IsFileExtension(agent_path,".qtable");
		if(isQtable){
			agentReadQtable(&agent,agent_path);
		}
	}

	MazeRenderCtxInit(&render);

	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(WINDOW_WIDTH, WINDOW_HEIGTH, "Agent Viewer");
    SetTargetFPS(60);       
    
	setCustomStyle();

	GuiWindowFileDialogState fDialogCtx = InitGuiWindowFileDialog(GetWorkingDirectory());

	while (!WindowShouldClose())
    {	
		// UPDATE LOGIC SHOULD GO HERE

		if (fDialogCtx.SelectFilePressed)
        {	
			switch(dialog_target){
				case MAZE:
					bool isNumpy = IsFileExtension(fDialogCtx.fileNameText, ".npy");
					bool isRawMaze = IsFileExtension(fDialogCtx.fileNameText, ".maze");
					if (isNumpy || isRawMaze)
					{
						strcpy(maze_path, TextFormat("%s" PATH_SEPERATOR "%s", fDialogCtx.dirPathText, fDialogCtx.fileNameText));
						if(isNumpy) readMazeNumpy(maze_path,&ir);
						else if (isRawMaze) readMazeRaw(maze_path,&ir);
						else{
							POP_UP_MSG("Cant Read Selected File");	
						}
					} else {
						POP_UP_MSG("File Selected Is Not An Maze");
					}
				break;

				case QTABLE:
					bool isQtable = IsFileExtension(fDialogCtx.fileNameText,".qtable");
					if(isQtable){
						strcpy(agent_path, TextFormat("%s" PATH_SEPERATOR "%s", fDialogCtx.dirPathText, fDialogCtx.fileNameText));
						agentReadQtable(&agent,agent_path);
					} else {
						POP_UP_MSG("File Selected Is Not An Qtable");
					}
					break;
					
				}
				
				fDialogCtx.SelectFilePressed = false;
				lockMazeEditing = false;
			
			runAgent = !isAgentRunable(&ir,&agent) ? false : runAgent;
			save_viewer_state(".agent_viewer_state", maze_path, agent_path, lockMazeEditing);
        }
		
		if (IsKeyPressed(KEY_Z)) {
			lockCameraToAgent = !lockCameraToAgent;
			if (lockCameraToAgent) {

				prev_mouse_offset_x = render.mouse_offset_x;
				prev_mouse_offset_y = render.mouse_offset_y;
				camera_transition_from_x = prev_mouse_offset_x;
				camera_transition_from_y = prev_mouse_offset_y;

				camera_transition_duration = fmax(updateInterval * 0.5, 0.05);
				camera_transition_start = GetTime();
				camera_transition_active = true;

				render.width = GetScreenWidth();
				render.heigth = GetScreenHeight();
				UpdateCellRectParams(&render, &ir);
			} else {

				camera_transition_active = false;
				render.mouse_offset_x = prev_mouse_offset_x;
				render.mouse_offset_y = prev_mouse_offset_y;
			}
		}

		if(runAgent){
			double now = GetTime();
			if(now - lastUpdate >= updateInterval){
				updateAgent(&agent,&ir,&steps_taken,&isAgentGoal);
				lastUpdate = now;
			}
			moveAgentToClickedCell(&agent,&render,&ir,MOUSE_BUTTON_RIGHT);
		}

		if(IsKeyPressed(KEY_RIGHT)) updateInterval = updateInterval / 2.0f;
		if(IsKeyPressed(KEY_LEFT)) updateInterval = updateInterval * 2.0f;

		// END		

		BeginDrawing();

			if (lockCameraToAgent) {
				float wheel = GetMouseWheelMove();
				if (wheel != 0.0f) {
					const float base = 1.25f;
					float targetZ = render.zoom_alpha * powf(base, wheel);
					targetZ = clampf(targetZ, 0.001f, 100.0f);
					float smooth = 0.3f;
					render.zoom_alpha += (targetZ - render.zoom_alpha) * smooth;
				}

				render.width = GetScreenWidth();
				render.heigth = GetScreenHeight();
				UpdateCellRectParams(&render, &ir);

				int target_x = render.mouse_offset_x;
				int target_y = render.mouse_offset_y;
				if (ir.rows > 0 && ir.cols > 0) {
					state_t s = agent.current_s;
					target_x = (int)( (GetScreenWidth() / 2) - (render.offset_x + s.x * render.cr_lenght + render.cr_lenght/2) );
					target_y = (int)( (GetScreenHeight() / 2) - (render.offset_y + s.y * render.cr_lenght + render.cr_lenght/2) );
				}

				if (camera_transition_active) {
					double now = GetTime();
					double elapsed = now - camera_transition_start;
					if (elapsed >= camera_transition_duration) {
						render.mouse_offset_x = target_x;
						render.mouse_offset_y = target_y;
						camera_transition_active = false;
					} else {
						float t = (float)(elapsed / camera_transition_duration);
						// smoothstep (3t^2 - 2t^3)
						float s = t * t * (3.0f - 2.0f * t);
						render.mouse_offset_x = (int) (camera_transition_from_x + s * (target_x - camera_transition_from_x));
						render.mouse_offset_y = (int) (camera_transition_from_y + s * (target_y - camera_transition_from_y));
					}
				} else {
					render.mouse_offset_x = target_x;
					render.mouse_offset_y = target_y;
				}

				renderMaze(&render, &ir, true);
			} else {
				renderMaze(&render, &ir, lockMazeEditing);
			}

			if(runAgent) renderAgent(&agent,&render);
			
			if (fDialogCtx.windowActive) 
			GuiLock();
			
			const int button_w = 240;
			const int button_h = 30;

			if (GuiButton((Rectangle){0, 0, button_w, button_h }, GuiIconText(ICON_FILE_OPEN, "Load Maze"))) {
				fDialogCtx.windowActive = true;
				lockMazeEditing = true;
				dialog_target = MAZE;
			};

			if (GuiButton((Rectangle){ button_w, 0, button_w, button_h }, GuiIconText(ICON_FILE_OPEN, "Load Qtable"))) {
				fDialogCtx.windowActive = true;
				lockMazeEditing = true;
				dialog_target = QTABLE;
			};

			if (GuiButton((Rectangle){ 2*button_w, 0, button_w, button_h }, GuiIconText(ICON_PLAYER_PLAY, "Run Agent"))) {
				runAgent = isAgentRunable(&ir,&agent);
				if(!runAgent){
					POP_UP_MSG("ERROR: Agent Qtable Dont Match Maze");
				}
				else{
					agentInit(&agent,&ir,0.0,0.0,0.0,0U);
					agent.epsilon = 0.0f;
				}
			};

			int info_x = UI_LEFT_X;
            int info_y = UI_TOP_Y + UI_BTN_H + UI_BTN_SPACING;

			DrawText(TextFormat("Update Interval: %.3f s", updateInterval), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("Cumulative Reward: %.3f", agent.accum_reward), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 32;

			DrawText(TextFormat("Map: %s", GetFileName(maze_path)), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("Rows: %d", ir.rows), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("Cols: %d", ir.cols), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 16;

			DrawText(TextFormat("Qtable: %s", GetFileName(agent_path)), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("n_x: %d", agent.q_table.len_state_x), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("n_y: %d", agent.q_table.len_state_y), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 8;
			DrawText(TextFormat("n_actions: %d", agent.q_table.len_state_actions), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
			info_y += LABEL_TEXT_SIZE + 16;
			
			GuiUnlock();

			setDefaultStyle();
				GuiWindowFileDialog(&fDialogCtx);
			setCustomStyle();

			if(save_popup_active){
				setDefaultStyle();
					drawPopUpMsg(&save_popup_active,save_popup_msg);
				setCustomStyle();
			}

		EndDrawing();

	}

	CloseWindow();
	
	return 0;
}