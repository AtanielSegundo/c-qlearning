/*******************************************************************************************
*
*   Window File Dialog v1.2 - Modal file dialog to open/save files
*
*   MODULE USAGE:
*       #define GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION
*       #include "gui_window_file_dialog.h"
*
*       INIT: GuiWindowFileDialogState state = GuiInitWindowFileDialog();
*       DRAW: GuiWindowFileDialog(&state);
*
*   NOTE: This module depends on some raylib file system functions:
*       - LoadDirectoryFiles()
*       - UnloadDirectoryFiles()
*       - GetWorkingDirectory()
*       - DirectoryExists()
*       - FileExists()
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2019-2024 Ramon Santamaria (@raysan5)
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "raylib.h"

#ifndef GUI_WINDOW_FILE_DIALOG_H
#define GUI_WINDOW_FILE_DIALOG_H

// Gui file dialog context data
typedef struct {

    // Window management variables
    bool windowActive;
    Rectangle windowBounds;
    Vector2 panOffset;
    bool dragMode;
    bool supportDrag;

    // UI variables
    bool dirPathEditMode;
    char dirPathText[1024];

    int filesListScrollIndex;
    bool filesListEditMode;
    int filesListActive;

    bool fileNameEditMode;
    char fileNameText[1024];
    bool SelectFilePressed;
    bool CancelFilePressed;
    int fileTypeActive;
    int itemFocused;

    // Custom state variables
    FilePathList dirFiles;
    char filterExt[256];
    char dirPathTextCopy[1024];
    char fileNameTextCopy[1024];

    int prevFilesListActive;

    bool saveFileMode;

} GuiWindowFileDialogState;

#ifdef __cplusplus
extern "C" {            // Prevents name mangling of functions
#endif

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
// ...

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
//...

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
GuiWindowFileDialogState InitGuiWindowFileDialog(const char *initPath);
void GuiWindowFileDialog(GuiWindowFileDialogState *state);

#ifdef __cplusplus
}
#endif

#endif // GUI_WINDOW_FILE_DIALOG_H

/***********************************************************************************
*
*   GUI_WINDOW_FILE_DIALOG IMPLEMENTATION
*
************************************************************************************/
#if defined(GUI_WINDOW_FILE_DIALOG_IMPLEMENTATION)

#include "raygui.h"

#include <string.h>     // Required for: strcpy()

//----------------------------------------------------------------------------------
// Defines and Macros
//----------------------------------------------------------------------------------
#define MAX_DIRECTORY_FILES    2048
#define MAX_ICON_PATH_LENGTH    512
#ifdef _WIN32
#define PATH_SEPERATOR "\\"
#else
#define PATH_SEPERATOR "/"
#endif

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
#if defined(USE_CUSTOM_LISTVIEW_FILEINFO)
// Detailed file info type
typedef struct FileInfo {
    const char *name;
    int size;
    int modTime;
    int type;
    int icon;
} FileInfo;
#else
// Filename only
typedef char *FileInfo;             // Files are just a path string
#endif

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
FileInfo *dirFilesIcon = NULL;      // Path string + icon (for fancy drawing)

//----------------------------------------------------------------------------------
// Internal Module Functions Definition
//----------------------------------------------------------------------------------
// Read files in new path
static void ReloadDirectoryFiles(GuiWindowFileDialogState *state);

#if defined(USE_CUSTOM_LISTVIEW_FILEINFO)
// List View control for files info with extended parameters
static int GuiListViewFiles(Rectangle bounds, FileInfo *files, int count, int *focus, int *scrollIndex, int active);
#endif

//----------------------------------------------------------------------------------
// Module Functions Definition
//----------------------------------------------------------------------------------
GuiWindowFileDialogState InitGuiWindowFileDialog(const char *initPath)
{
    GuiWindowFileDialogState state = { 0 };

    // Window size as percentage of screen
    float win_w = GetScreenWidth() * 0.80f;   
    float win_h = GetScreenHeight() * 0.80f;  

    // Centered
    state.windowBounds = (Rectangle){
        (GetScreenWidth() - win_w) * 0.5f,
        (GetScreenHeight() - win_h) * 0.5f,
        win_w, win_h
    };

    state.windowActive = false;
    state.supportDrag = true;
    state.dragMode = false;
    state.panOffset = (Vector2){ 0, 0 };

    // Init path data
    state.dirPathEditMode = false;
    state.filesListActive = -1;
    state.prevFilesListActive = state.filesListActive;
    state.filesListScrollIndex = 0;

    state.fileNameEditMode = false;

    state.SelectFilePressed = false;
    state.CancelFilePressed = false;

    state.fileTypeActive = 0;

    strcpy(state.fileNameText, "\0");

    // Custom variables initialization
    if (initPath && DirectoryExists(initPath))
    {
        strcpy(state.dirPathText, initPath);
    }
    else if (initPath && FileExists(initPath))
    {
        strcpy(state.dirPathText, GetDirectoryPath(initPath));
        strcpy(state.fileNameText, GetFileName(initPath));
    }
    else strcpy(state.dirPathText, GetWorkingDirectory());

    strcpy(state.dirPathTextCopy, state.dirPathText);
    strcpy(state.fileNameTextCopy, state.fileNameText);

    state.filterExt[0] = '\0';

    state.dirFiles.count = 0;

    return state;
}

void GuiWindowFileDialog(GuiWindowFileDialogState *state)
{
    if (state->windowActive)
    {
        // --- compute adaptive sizes based on windowBounds (percentages) ---
        float winX = state->windowBounds.x;
        float winY = state->windowBounds.y;
        float winW = state->windowBounds.width;
        float winH = state->windowBounds.height;

        // text size scaled from window height (bigger window -> bigger text)
        int prev_text_size = GuiGetStyle(DEFAULT, TEXT_SIZE);
        int adaptive_text_size = (int)(winH / 25.0f); // heuristic
        if (adaptive_text_size < 12) adaptive_text_size = 12;

        // save other styles we may override
        int prev_list_text_align = GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT);
        int prev_list_item_h = GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT);

        GuiSetStyle(DEFAULT, TEXT_SIZE, adaptive_text_size);

        // control heights and gaps based on text size
        int ctrl_h = adaptive_text_size + (adaptive_text_size / 2); // textbox/button height
        int small_gap = (int)(winW * 0.01f); // small horizontal gap ~1% window width
        if (small_gap < 4) small_gap = 4;

        // BTN widths as percentage of dialog width
        int btn_w_small = (int)(winW * 0.10f);   // previous dir button
        int btn_w_std   = (int)(winW * 0.20f);   // select / cancel button
        if (btn_w_small < 44) btn_w_small = 44;
        if (btn_w_std   < 80) btn_w_std = 80;

        // Titlebar height estimate
        int title_h = adaptive_text_size + 12;

        // ----------------- dragging logic (uses title_h) -----------------
        if (state->supportDrag)
        {
            Vector2 mousePosition = GetMousePosition();

            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            {
                if (CheckCollisionPointRec(mousePosition, (Rectangle){ winX, winY, winW, (float)title_h }))
                {
                    state->dragMode = true;
                    state->panOffset.x = mousePosition.x - winX;
                    state->panOffset.y = mousePosition.y - winY;
                }
            }

            if (state->dragMode)
            {
                state->windowBounds.x = (mousePosition.x - state->panOffset.x);
                state->windowBounds.y = (mousePosition.y - state->panOffset.y);

                // Clamp
                if (state->windowBounds.x < 0) state->windowBounds.x = 0;
                else if (state->windowBounds.x > (GetScreenWidth() - state->windowBounds.width)) state->windowBounds.x = GetScreenWidth() - state->windowBounds.width;

                if (state->windowBounds.y < 0) state->windowBounds.y = 0;
                else if (state->windowBounds.y > (GetScreenHeight() - state->windowBounds.height)) state->windowBounds.y = GetScreenHeight() - state->windowBounds.height;

                if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) state->dragMode = false;
            }
        }
        // ----------------------------------------------------------------

        // Lazy load icons & files
        if (dirFilesIcon == NULL)
        {
            dirFilesIcon = (FileInfo *)RL_CALLOC(MAX_DIRECTORY_FILES, sizeof(FileInfo));
            for (int i = 0; i < MAX_DIRECTORY_FILES; i++) dirFilesIcon[i] = (char *)RL_CALLOC(MAX_ICON_PATH_LENGTH, 1);
        }
        if (state->dirFiles.paths == NULL) ReloadDirectoryFiles(state);

        // Draw window
        state->windowActive = !GuiWindowBox(state->windowBounds, "#198# Select File Dialog");

        // Previous directory button (top-right)
        Rectangle prevBtnRc = (Rectangle){
            winX + winW - btn_w_small - small_gap,
            winY + 24 ,
            (float)btn_w_small,
            (float)ctrl_h
        };
        if (GuiButton(prevBtnRc, "< .."))
        {
            strcpy(state->dirPathText, GetPrevDirectoryPath(state->dirPathText));
            ReloadDirectoryFiles(state);
            state->filesListActive = -1;
            memset(state->fileNameText, 0, 1024);
            memset(state->fileNameTextCopy, 0, 1024);
        }

        // Directory textbox: take most of width minus space for prev button
        Rectangle dirBoxRc = (Rectangle){
            winX + 8,
            winY + 24,
            (float)(winW - (btn_w_small + 3*small_gap)),
            (float)ctrl_h
        };
        if (GuiTextBox(dirBoxRc, state->dirPathText, 1024, state->dirPathEditMode))
        {
            if (state->dirPathEditMode)
            {
                if (DirectoryExists(state->dirPathText))
                {
                    ReloadDirectoryFiles(state);
                    strcpy(state->dirPathTextCopy, state->dirPathText);
                }
                else strcpy(state->dirPathText, state->dirPathTextCopy);
            }

            state->dirPathEditMode = !state->dirPathEditMode;
        }

        // List view: use dynamic heights based on text size
        GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
        int adaptive_list_item_h = adaptive_text_size + 8;
        if (adaptive_list_item_h < 20) adaptive_list_item_h = 20;
        GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, adaptive_list_item_h);

#if defined(USE_CUSTOM_LISTVIEW_FILEINFO)
        state->filesListActive = GuiListViewFiles((Rectangle){ winX + 8, winY + 48 + title_h*0.1f, winW - 16, winH - 60 - 16 - 68 }, fileInfo, state->dirFiles.count, &state->itemFocused, &state->filesListScrollIndex, state->filesListActive);
#else
        GuiListViewEx((Rectangle){ winX + 8, winY + 48 + title_h*0.1f, winW - 16, winH - 60 - 16 - 68 },
                      (const char**)dirFilesIcon, state->dirFiles.count, &state->filesListScrollIndex, &state->filesListActive, &state->itemFocused);
#endif

        // Restore LISTVIEW alignment but keep item height override restored later
        GuiSetStyle(LISTVIEW, TEXT_ALIGNMENT, prev_list_text_align);

        // File selection handling (directory traversal)
        if ((state->filesListActive >= 0) && (state->filesListActive != state->prevFilesListActive))
        {
            if(state->saveFileMode){
				strcpy(state->fileNameTextCopy, state->fileNameText);
			}
			strcpy(state->fileNameText, GetFileName(state->dirFiles.paths[state->filesListActive]));

            if (DirectoryExists(TextFormat("%s/%s", state->dirPathText, state->fileNameText)))
            {
                if (TextIsEqual(state->fileNameText, "..")) strcpy(state->dirPathText, GetPrevDirectoryPath(state->dirPathText));
                else strcpy(state->dirPathText, TextFormat("%s/%s", (strcmp(state->dirPathText, "/") == 0)? "" : state->dirPathText, state->fileNameText));

                strcpy(state->dirPathTextCopy, state->dirPathText);

                ReloadDirectoryFiles(state);

                strcpy(state->dirPathTextCopy, state->dirPathText);

                state->filesListActive = -1;
                if (!state->saveFileMode){
					strcpy(state->fileNameText, "\0");
                	strcpy(state->fileNameTextCopy, state->fileNameText);
				} else {
					strcpy(state->fileNameText,state->fileNameTextCopy);
				}
            }

            state->prevFilesListActive = state->filesListActive;
        }

        // Bottom area: labels and controls sized by percentage of winW
        int label_file_name_w = MeasureText("File name:", adaptive_text_size) + 8;
        int label_filter_w = MeasureText("File filter:", adaptive_text_size) + 8;

        float fileBoxX = winX + label_file_name_w + 8;
        float fileBoxW = winW - (label_file_name_w + btn_w_std + 4*small_gap);
        if (fileBoxW < 80) fileBoxW = 80;

        GuiLabel((Rectangle){ winX + 8, winY + winH - 68, (float)label_file_name_w, (float)ctrl_h }, "File name:");
        if (GuiTextBox((Rectangle){ fileBoxX, winY + winH - 68, fileBoxW, (float)ctrl_h }, state->fileNameText, 128, state->fileNameEditMode))
        {
            if (*state->fileNameText)
            {
                if (FileExists(TextFormat("%s/%s", state->dirPathText, state->fileNameText)))
                {
                    for (unsigned int i = 0; i < state->dirFiles.count; i++)
                    {
                        if (TextIsEqual(state->fileNameText, state->dirFiles.paths[i]))
                        {
                            state->filesListActive = i;
                            strcpy(state->fileNameTextCopy, state->fileNameText);
                            break;
                        }
                    }
                }
                else if (!state->saveFileMode)
                {
                    strcpy(state->fileNameText, state->fileNameTextCopy);
                }
            }

            state->fileNameEditMode = !state->fileNameEditMode;
        }

        GuiLabel((Rectangle){ winX + 8, winY + winH - 24 - 12, (float)label_filter_w, (float)ctrl_h }, "File filter:");
        GuiComboBox((Rectangle){ fileBoxX, winY + winH - 24 - 12, fileBoxW, (float)ctrl_h }, "All files", &state->fileTypeActive);

        float selectX = winX + winW - btn_w_std - small_gap;
        state->SelectFilePressed = GuiButton((Rectangle){ selectX, winY + winH - 68, (float)btn_w_std, (float)ctrl_h }, "Select");

        if (GuiButton((Rectangle){ selectX, winY + winH - 24 - 12, (float)btn_w_std, (float)ctrl_h }, "Cancel")) state->windowActive = false;

        // Exit on file selected
        if (state->SelectFilePressed) state->windowActive = false;

        // File dialog close cleanup
        if (!state->windowActive)
        {
            for (int i = 0; i < MAX_DIRECTORY_FILES; i++) RL_FREE(dirFilesIcon[i]);

            RL_FREE(dirFilesIcon);
            dirFilesIcon = NULL;

            UnloadDirectoryFiles(state->dirFiles);

            state->dirFiles.count = 0;
            state->dirFiles.capacity = 0;
            state->dirFiles.paths = NULL;
        }

        // restore styles we changed
        GuiSetStyle(DEFAULT, TEXT_SIZE, prev_text_size);
        GuiSetStyle(LISTVIEW, LIST_ITEMS_HEIGHT, prev_list_item_h);
    }
}

// Compare two files from a directory
static inline int FileCompare(const char *d1, const char *d2, const char *dir)
{
    const bool b1 = DirectoryExists(TextFormat("%s/%s", dir, d1));
    const bool b2 = DirectoryExists(TextFormat("%s/%s", dir, d2));

    if (b1 && !b2) return -1;
    if (!b1 && b2) return 1;

    if (!FileExists(TextFormat("%s/%s", dir, d1))) return 1;
    if (!FileExists(TextFormat("%s/%s", dir, d2))) return -1;

    return strcmp(d1, d2);
}

// Read files in new path
static void ReloadDirectoryFiles(GuiWindowFileDialogState *state)
{
    UnloadDirectoryFiles(state->dirFiles);

    state->dirFiles = LoadDirectoryFilesEx(state->dirPathText, (state->filterExt[0] == '\0')? NULL : state->filterExt, false);
    state->itemFocused = 0;

    // Reset dirFilesIcon memory
    for (int i = 0; i < MAX_DIRECTORY_FILES; i++) memset(dirFilesIcon[i], 0, MAX_ICON_PATH_LENGTH);

    // Copy paths as icon + fileNames into dirFilesIcon
    for (unsigned int i = 0; i < state->dirFiles.count; i++)
    {
        if (IsPathFile(state->dirFiles.paths[i]))
        {
            // Path is a file, a file icon for convenience (for some recognized extensions)
            if (IsFileExtension(state->dirFiles.paths[i], ".png;.bmp;.tga;.gif;.jpg;.jpeg;.psd;.hdr;.qoi;.dds;.pkm;.ktx;.pvr;.astc"))
            {
                strcpy(dirFilesIcon[i], TextFormat("#12#%s", GetFileName(state->dirFiles.paths[i])));
            }
            else if (IsFileExtension(state->dirFiles.paths[i], ".wav;.mp3;.ogg;.flac;.xm;.mod;.it;.wma;.aiff"))
            {
                strcpy(dirFilesIcon[i], TextFormat("#11#%s", GetFileName(state->dirFiles.paths[i])));
            }
            else if (IsFileExtension(state->dirFiles.paths[i], ".txt;.info;.md;.nfo;.xml;.json;.c;.cpp;.cs;.lua;.py;.glsl;.vs;.fs"))
            {
                strcpy(dirFilesIcon[i], TextFormat("#10#%s", GetFileName(state->dirFiles.paths[i])));
            }
            else if (IsFileExtension(state->dirFiles.paths[i], ".exe;.bin;.raw;.msi"))
            {
                strcpy(dirFilesIcon[i], TextFormat("#200#%s", GetFileName(state->dirFiles.paths[i])));
            }
            else strcpy(dirFilesIcon[i], TextFormat("#218#%s", GetFileName(state->dirFiles.paths[i])));
        }
        else
        {
            // Path is a directory, add a directory icon
            strcpy(dirFilesIcon[i], TextFormat("#1#%s", GetFileName(state->dirFiles.paths[i])));
        }
    }
}

#if defined(USE_CUSTOM_LISTVIEW_FILEINFO)
// List View control for files info with extended parameters
static int GuiListViewFiles(Rectangle bounds, FileInfo *files, int count, int *focus, int *scrollIndex, int *active)
{
    int result = 0;
    GuiState state = guiState;
    int itemFocused = (focus == NULL)? -1 : *focus;
    int itemSelected = *active;

    // Check if we need a scroll bar
    bool useScrollBar = false;
    if ((GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT) + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING))*count > bounds.height) useScrollBar = true;

    // Define base item rectangle [0]
    Rectangle itemBounds = { 0 };
    itemBounds.x = bounds.x + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING);
    itemBounds.y = bounds.y + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING) + GuiGetStyle(DEFAULT, BORDER_WIDTH);
    itemBounds.width = bounds.width - 2*GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING) - GuiGetStyle(DEFAULT, BORDER_WIDTH);
    itemBounds.height = GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT);
    if (useScrollBar) itemBounds.width -= GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH);

    // Get items on the list
    int visibleItems = bounds.height/(GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT) + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING));
    if (visibleItems > count) visibleItems = count;

    int startIndex = (scrollIndex == NULL)? 0 : *scrollIndex;
    if ((startIndex < 0) || (startIndex > (count - visibleItems))) startIndex = 0;
    int endIndex = startIndex + visibleItems;

    // Update control
    //--------------------------------------------------------------------
    if ((state != GUI_STATE_DISABLED) && !guiLocked)
    {
        Vector2 mousePoint = GetMousePosition();

        // Check mouse inside list view
        if (CheckCollisionPointRec(mousePoint, bounds))
        {
            state = GUI_STATE_FOCUSED;

            // Check focused and selected item
            for (int i = 0; i < visibleItems; i++)
            {
                if (CheckCollisionPointRec(mousePoint, itemBounds))
                {
                    itemFocused = startIndex + i;
                    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) itemSelected = startIndex + i;
                    break;
                }

                // Update item rectangle y position for next item
                itemBounds.y += (GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT) + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING));
            }

            if (useScrollBar)
            {
                int wheelMove = GetMouseWheelMove();
                startIndex -= wheelMove;

                if (startIndex < 0) startIndex = 0;
                else if (startIndex > (count - visibleItems)) startIndex = count - visibleItems;

                endIndex = startIndex + visibleItems;
                if (endIndex > count) endIndex = count;
            }
        }
        else itemFocused = -1;

        // Reset item rectangle y to [0]
        itemBounds.y = bounds.y + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING) + GuiGetStyle(DEFAULT, BORDER_WIDTH);
    }
    //--------------------------------------------------------------------

    // Draw control
    //--------------------------------------------------------------------
    DrawRectangleRec(bounds, GetColor(GuiGetStyle(DEFAULT, BACKGROUND_COLOR)));     // Draw background
    DrawRectangleLinesEx(bounds, GuiGetStyle(DEFAULT, BORDER_WIDTH), Fade(GetColor(GuiGetStyle(LISTVIEW, BORDER + state*3)), guiAlpha));

    // TODO: Draw list view header with file sections: icon+name | size | type | modTime

    // Draw visible items
    for (int i = 0; i < visibleItems; i++)
    {
        if (state == GUI_STATE_DISABLED)
        {
            if ((startIndex + i) == itemSelected)
            {
                DrawRectangleRec(itemBounds, Fade(GetColor(GuiGetStyle(LISTVIEW, BASE_COLOR_DISABLED)), guiAlpha));
                DrawRectangleLinesEx(itemBounds, GuiGetStyle(LISTVIEW, BORDER_WIDTH), Fade(GetColor(GuiGetStyle(LISTVIEW, BORDER_COLOR_DISABLED)), guiAlpha));
            }

            // TODO: Draw full file info line: icon+name | size | type | modTime

            GuiDrawText(files[startIndex + i].name, GetTextBounds(DEFAULT, itemBounds), GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT), Fade(GetColor(GuiGetStyle(LISTVIEW, TEXT_COLOR_DISABLED)), guiAlpha));
        }
        else
        {
            if ((startIndex + i) == itemSelected)
            {
                // Draw item selected
                DrawRectangleRec(itemBounds, Fade(GetColor(GuiGetStyle(LISTVIEW, BASE_COLOR_PRESSED)), guiAlpha));
                DrawRectangleLinesEx(itemBounds, GuiGetStyle(LISTVIEW, BORDER_WIDTH), Fade(GetColor(GuiGetStyle(LISTVIEW, BORDER_COLOR_PRESSED)), guiAlpha));

                GuiDrawText(files[startIndex + i].name, GetTextBounds(DEFAULT, itemBounds), GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT), Fade(GetColor(GuiGetStyle(LISTVIEW, TEXT_COLOR_PRESSED)), guiAlpha));
            }
            else if ((startIndex + i) == itemFocused)
            {
                // Draw item focused
                DrawRectangleRec(itemBounds, Fade(GetColor(GuiGetStyle(LISTVIEW, BASE_COLOR_FOCUSED)), guiAlpha));
                DrawRectangleLinesEx(itemBounds, GuiGetStyle(LISTVIEW, BORDER_WIDTH), Fade(GetColor(GuiGetStyle(LISTVIEW, BORDER_COLOR_FOCUSED)), guiAlpha));

                GuiDrawText(files[startIndex + i].name, GetTextBounds(DEFAULT, itemBounds), GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT), Fade(GetColor(GuiGetStyle(LISTVIEW, TEXT_COLOR_FOCUSED)), guiAlpha));
            }
            else
            {
                // Draw item normal
                GuiDrawText(files[startIndex + i].name, GetTextBounds(DEFAULT, itemBounds), GuiGetStyle(LISTVIEW, TEXT_ALIGNMENT), Fade(GetColor(GuiGetStyle(LISTVIEW, TEXT_COLOR_NORMAL)), guiAlpha));
            }
        }

        // Update item rectangle y position for next item
        itemBounds.y += (GuiGetStyle(LISTVIEW, LIST_ITEMS_HEIGHT) + GuiGetStyle(LISTVIEW, LIST_ITEMS_PADDING));
    }

    if (useScrollBar)
    {
        Rectangle scrollBarBounds = {
            bounds.x + bounds.width - GuiGetStyle(LISTVIEW, BORDER_WIDTH) - GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH),
            bounds.y + GuiGetStyle(LISTVIEW, BORDER_WIDTH), (float)GuiGetStyle(LISTVIEW, SCROLLBAR_WIDTH),
            bounds.height - 2*GuiGetStyle(DEFAULT, BORDER_WIDTH)
        };

        // Calculate percentage of visible items and apply same percentage to scrollbar
        float percentVisible = (float)(endIndex - startIndex)/count;
        float sliderSize = bounds.height*percentVisible;

        int prevSliderSize = GuiGetStyle(SCROLLBAR, SLIDER_WIDTH);  // Save default slider size
        int prevScrollSpeed = GuiGetStyle(SCROLLBAR, SCROLL_SPEED); // Save default scroll speed
        GuiSetStyle(SCROLLBAR, SLIDER_WIDTH, sliderSize);           // Change slider size
        GuiSetStyle(SCROLLBAR, SCROLL_SPEED, count - visibleItems); // Change scroll speed

        startIndex = GuiScrollBar(scrollBarBounds, startIndex, 0, count - visibleItems);

        GuiSetStyle(SCROLLBAR, SCROLL_SPEED, prevScrollSpeed); // Reset scroll speed to default
        GuiSetStyle(SCROLLBAR, SLIDER_WIDTH, prevSliderSize);  // Reset slider size to default
    }
    //--------------------------------------------------------------------

    if (focus != NULL) *focus = itemFocused;
    if (scrollIndex != NULL) *scrollIndex = startIndex;

    *active = itemSelected;
    return result;
}
#endif // USE_CUSTOM_LISTVIEW_FILEINFO

#endif // GUI_FILE_DIALOG_IMPLEMENTATION
