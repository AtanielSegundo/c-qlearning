#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>


#include "raylib.h"
// #include "argparse.h"

#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#undef RAYGUI_IMPLEMENTATION 

#define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
#include "gui_window_file_dialog.h"

#define MAZE_RENDER_IMPLEMENTATION
#include "mazeRender.h"

#define MAZE_IR_IMPLEMENTATION
#include "mazeIR.h"

#define UI_IMPLEMENTATION
#include "UI.h"

#define WINDOW_WIDTH  1280U
#define WINDOW_HEIGTH  720U

int main(int argc, char** argv) {	
	// ARGUMENT HANDLING
	
	char maze_path[512] = {0};
	char save_path[512] = {0};
	
	// Popup de confirmação de salvamento
	bool save_popup_active = false;
	char save_popup_msg[256] = {0};

	// RAYLIB STARTUP STUFF
	SetTraceLogLevel(LOG_WARNING); 
	    
	MazeInternalRepr ir  = {0};
	MazeRenderCtx render = {0};

	MazeRenderCtxInit(&render);
	
	SetConfigFlags(FLAG_WINDOW_RESIZABLE);
	InitWindow(WINDOW_WIDTH, WINDOW_HEIGTH, "map editor");
    SetTargetFPS(60);       
    
	setCustomStyle();

	GuiWindowFileDialogState fDialogCtx = InitGuiWindowFileDialog(GetWorkingDirectory());

    while (!WindowShouldClose())
    {
		// UPDATE LOGIC SHOULD GO HERE

		if (fDialogCtx.SelectFilePressed)
        {	
			bool isNumpy = IsFileExtension(fDialogCtx.fileNameText, ".npy");
			bool isRawMaze = IsFileExtension(fDialogCtx.fileNameText, ".maze");
			
            if (isNumpy || isRawMaze)
            {
				if(fDialogCtx.saveFileMode){
					strcpy(save_path, TextFormat("%s" PATH_SEPERATOR "%s", fDialogCtx.dirPathText, fDialogCtx.fileNameText));
					// escreve o arquivo
					writeMazeRaw(save_path,&ir);

					// TODO substituído: mostrar pop-up usando FileExists para confirmar
					if (FileExists(save_path)) {
						snprintf(save_popup_msg, sizeof(save_popup_msg), "Mapa salvo com sucesso:\n%s", save_path);
					} else {
						snprintf(save_popup_msg, sizeof(save_popup_msg), "Erro ao salvar o mapa:\n%s", save_path);
					}
					save_popup_active = true;

					fDialogCtx.saveFileMode = false;

				} else {
					strcpy(maze_path, TextFormat("%s" PATH_SEPERATOR "%s", fDialogCtx.dirPathText, fDialogCtx.fileNameText));
					if(isNumpy) readMazeNumpy(maze_path,&ir);
					else if (isRawMaze) readMazeRaw(maze_path,&ir);
				}
            }

            fDialogCtx.SelectFilePressed = false;
        }

			
		updateMazeClickedCell(&render,&ir);

		// END

		BeginDrawing();

			renderMaze(&render,&ir,fDialogCtx.windowActive);
				
			if (fDialogCtx.windowActive) 
			GuiLock();

				if (GuiButton((Rectangle){ 0, 0, 240, 30 }, GuiIconText(ICON_FILE_OPEN, "Open Maze"))) fDialogCtx.windowActive = true;
				
				if (GuiButton((Rectangle){ 240, 0, 240, 30 }, GuiIconText(ICON_FILE_OPEN, "Save Maze"))) {
					strcpy(fDialogCtx.dirPathText, GetDirectoryPath(maze_path));
        			strcpy(fDialogCtx.fileNameText, GetFileName(maze_path));
					fDialogCtx.windowActive = true;
					fDialogCtx.saveFileMode = true;
				}

				int info_x = UI_LEFT_X;
                int info_y = UI_TOP_Y + UI_BTN_H + UI_BTN_SPACING;

				DrawText(TextFormat("Map: %s", GetFileName(maze_path)), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
                info_y += LABEL_TEXT_SIZE + 8;
                DrawText(TextFormat("Rows: %d", ir.rows), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
                info_y += LABEL_TEXT_SIZE + 8;
                DrawText(TextFormat("Cols: %d", ir.cols), info_x, info_y, LABEL_TEXT_SIZE, WHITE);
                info_y += LABEL_TEXT_SIZE + 16;

			GuiUnlock();
			
			
			// RENDER LOGIC SHOULD GO HERE
			setDefaultStyle();
				GuiWindowFileDialog(&fDialogCtx);
			setCustomStyle();

			// --- Popup de confirmação/erro de salvamento ---
			if (save_popup_active) {
				// overlay
				DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), Fade(BLACK, 0.45f));

				int modal_w = 640;
				int modal_h = 140;
				int modal_x = (GetScreenWidth() - modal_w) / 2;
				int modal_y = (GetScreenHeight() - modal_h) / 2;

				DrawRectangle(modal_x, modal_y, modal_w, modal_h, LIGHTGRAY);
				DrawRectangleLines(modal_x, modal_y, modal_w, modal_h, GRAY);

				// mensagem (quebra de linha simples)
				int padding = 12;
				DrawTextEx(GetFontDefault(), save_popup_msg, (Vector2){ modal_x + padding, modal_y + padding + 4 }, 18, 1, BLACK);

				// botão OK
				Rectangle okBtn = (Rectangle){ modal_x + (modal_w - 100) / 2, modal_y + modal_h - 44, 100, 32 };
				if (GuiButton(okBtn, "OK")) {
					save_popup_active = false;
					// limpa msg opcionalmente
					memset(save_popup_msg, 0, sizeof(save_popup_msg));
				}
			}
			// ------------------------------------------------------
			

		// END
        EndDrawing();

    }

    CloseWindow();


    return 0;
}


#if 0

// EX 1: RESIZABLE WINDOW WITH INPUTS

Vector2 mousePosition = { -100.0f, -100.0f };
char textBuffer[1024] = {0};

int main(void)
{
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 450, "Hello World");
    SetTargetFPS(60);       

    while (!WindowShouldClose())
    {
        BeginDrawing();
        
            mousePosition = GetMousePosition();
            ClearBackground(RAYWHITE);
            DrawRectangle(190, 300,100,80,RED);
            if(!IsKeyDown(KEY_A))
                sprintf(textBuffer,"NOT A| M: (%.3f,%.3f)",mousePosition.x,mousePosition.y);
            else
                sprintf(textBuffer,"A| M: (%.3f,%.3f)",mousePosition.x,mousePosition.y);
            DrawText(textBuffer,190, 200, 20, BLACK);
        
        EndDrawing();
    }

    CloseWindow();

    return 0;
}

#endif