#include <stdint.h>
#include "mazeIR.h"
#include "raylib.h"
#include <math.h>

#ifndef  MAZE_RENDER_H

#define MAZE_RENDER_H

typedef struct {
	// WINDOW RENDER INFO
	size_t width;
	size_t heigth;

	// WINDOW START OFFSET
	size_t offset_x;
	size_t offset_y;			// THE Y = 0 => START IN WINDOW UPPER LEFT
	int mouse_offset_x;
	int mouse_offset_y;

	// ZOOM SCALE OFFSET
	float  zoom_alpha;

	// CELL RECTANGLE PARAMETERS, cr := cell rect
	size_t cr_lenght;
	size_t cr_outline;
	float cr_outline_percent;

} MazeRenderCtx;

#endif

static inline float clampf(float v, float a, float b){ return (v < a) ? a : (v > b) ? b : v; }

void _MazeRenderCtxUpdate_(MazeRenderCtx *r, MazeRenderCtx des_in){
	r->width              = des_in.width;
	r->heigth             = des_in.heigth;
	r->offset_x           = des_in.offset_x;
	r->offset_y           = des_in.offset_y;
	r->zoom_alpha         = des_in.zoom_alpha;
	r->cr_lenght          = des_in.cr_lenght;
	r->cr_outline         = des_in.cr_outline;
	r->cr_outline_percent = des_in.cr_outline_percent;
	r->mouse_offset_x     = des_in.mouse_offset_x;
	r->mouse_offset_y     = des_in.mouse_offset_y;
};

void UpdateCellRectParams(MazeRenderCtx *r, MazeInternalRepr *ir){
	if (ir->rows == 0 || ir->cols == 0) return;
	
	r->cr_lenght  = __min(r->width/ir->cols, r->heigth/ir->rows);  
	r->offset_x   = (r->width - r->cr_lenght*ir->cols) / 2;
	r->offset_y   = (r->heigth - r->cr_lenght*ir->rows) / 2;

	r->cr_lenght  = r->zoom_alpha*r->cr_lenght;
	r->cr_outline = (uint8_t)(r->cr_outline_percent*(float)r->cr_lenght);
};

void DrawRectangleWithOutline(int posX, int posY, int width, int height, Color color, int offset, Color outline_color){
    DrawRectangle(posX,posY,width,height,outline_color);
    DrawRectangle(posX,posY,width-offset,height-offset,color);
}

void renderMaze(MazeRenderCtx *r, MazeInternalRepr *ir, bool lockUpdates){
	r->width  = GetScreenWidth();
	r->heigth = GetScreenHeight();
	
	float wheel = GetMouseWheelMove();
	Vector2 mDelta = GetMouseDelta();
		
	if (!lockUpdates && wheel != 0.0f) {
		const float base = 1.25f; 
		float targetZ = r->zoom_alpha * powf(base, wheel);
		targetZ = clampf(targetZ, 0.001f, 100.0f);
		float smooth = 0.3f;
		r->zoom_alpha += (targetZ - r->zoom_alpha) * smooth;
		// printf("%f\n",r->zoom_alpha);
	}
	
	UpdateCellRectParams(r,ir);
	
	if(!lockUpdates && mDelta.x != 0.0f && mDelta.y != 0.0f && IsMouseButtonDown(MOUSE_BUTTON_LEFT)){
		// printf("(%f, %f)\n",mDelta.x,mDelta.y);
		r->mouse_offset_x += mDelta.x;
		r->mouse_offset_y += mDelta.y;
	}
	
	ClearBackground(BLACK);

	for(int i=0; i < ir->rows; i++){
        for(int j=0; j < ir->cols; j++){
            Color cell_color = cellTypeToColor[getCell(ir,i,j)];
            int posx = (r->offset_x + r->mouse_offset_x) + j*r->cr_lenght;
			int posy = (r->offset_y + r->mouse_offset_y) + i*r->cr_lenght;
			DrawRectangleWithOutline(posx,posy,r->cr_lenght,r->cr_lenght,cell_color,r->cr_outline,BLACK);
        }
    }
};


typedef struct {size_t row; size_t col} CellId;

CellId getMouseCell(MazeRenderCtx *r, MazeInternalRepr *ir){
    if (!r || !ir) return (CellId){ (size_t)-1, (size_t)-1 };
    if (ir->rows == 0 || ir->cols == 0) return (CellId){ (size_t)-1, (size_t)-1 };
    if (r->cr_lenght == 0) return (CellId){ (size_t)-1, (size_t)-1 }; /* nada desenhado */

    Vector2 mp = GetMousePosition();

    long grid_x0 = (long)r->offset_x + (long)r->mouse_offset_x;
    long grid_y0 = (long)r->offset_y + (long)r->mouse_offset_y;

    float rel_x = mp.x - (float)grid_x0;
    float rel_y = mp.y - (float)grid_y0;

    if (rel_x < 0.0f || rel_y < 0.0f) return (CellId){ (size_t)-1, (size_t)-1 };

    float cell_px = (float) r->cr_lenght;
    if (cell_px <= 0.0f) return (CellId){ (size_t)-1, (size_t)-1 };

    size_t col = (size_t) (rel_x / cell_px);
    size_t row = (size_t) (rel_y / cell_px);

    if (col >= ir->cols || row >= ir->rows) return (CellId){ (size_t)-1, (size_t)-1 };

    return (CellId){ row, col };
}

void updateMazeClickedCell(MazeRenderCtx *r, MazeInternalRepr *ir){
	Vector2 dt = GetMouseDelta();
	if(IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)){
		CellId c = getMouseCell(r,ir);
		if(c.row >= 0 && c.col >= 0){
			GridCellType ct =  getCell(ir,c.row,c.col);
			switch (ct)
			{
				case GRID_OPEN: 		 setCell(ir,c.row,c.col,GRID_WALL);break;
				case GRID_WALL: 		 setCell(ir,c.row,c.col,GRID_AGENT_GOAL);break;
				case GRID_AGENT_GOAL: 	 setCell(ir,c.row,c.col,GRID_AGENT_START);break;
				case GRID_AGENT_START:	 setCell(ir,c.row,c.col,GRID_OPEN);break;
				default:				 setCell(ir,c.row,c.col,GRID_OPEN);break;
			}
			
		}
	}
}


#define MazeRenderCtxInit(r, ...) \
		_MazeRenderCtxUpdate_((r),(MazeRenderCtx){.width=800,.heigth=450,.offset_x=0,.offset_y=0,.zoom_alpha=0.75f,.cr_lenght=0,.cr_outline=0,.mouse_offset_x=0,.mouse_offset_y=0,.cr_outline_percent=0.05f,__VA_ARGS__})

#define MazeRenderCtxUpdate(r,...) \
		_MazeRenderCtxUpdate_((r),(MazeRenderCtx){.width=(r)->width,.heigth=(r)->heigth,.offset_x=(r)->offset_x,.offset_y=(r)->offset_y, \ 
												.zoom_alpha=(r)->zoom_alpha,.cr_lenght=(r)->cr_lenght,.cr_outline=(r)->cr_outline,\ 
												.cr_outline_percent=(r)->cr_outline_percent, \
												.mouse_offset_x=(r)->mouse_offset_x,.mouse_offset_y=(r)->mouse_offset_y,__VA_ARGS__})

#ifdef MAZE_RENDER_IMPLEMENTATION

#endif