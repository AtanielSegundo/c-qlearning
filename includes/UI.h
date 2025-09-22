#ifndef UI_H

#define UI_H


struct styleElement{
	int control,property,value;
};

static struct styleElement defaultStyle[32];
static int defaultCount  = 0;  
static bool defaultAssigned = false;


#include "raygui.h"

const int UI_BTN_W        = 220;
const int UI_BTN_H        = 40;
const int UI_BTN_SPACING  = 12;
const int UI_LEFT_X       = 24;
const int UI_TOP_Y        = 20;
const int LABEL_TEXT_SIZE = 22;
const int BTN_TEXT_SIZE   = 20;


void setCustomStyle(void);
void setDefaultStyle(void);

#endif


#ifdef UI_IMPLEMENTATION

#define appendDefault(control,property) \
	   defaultStyle[defaultCount++] = (struct styleElement){control,property,GuiGetStyle(control,property)}        

void saveDefaultStyle(void){
	if(!defaultAssigned){
		appendDefault(DEFAULT, TEXT_SIZE);           
		appendDefault(BUTTON, TEXT_SIZE);            
		appendDefault(DEFAULT, TEXT_SPACING);         
		appendDefault(BUTTON, TEXT_ALIGNMENT);
		defaultAssigned = true;
	}
}

void setCustomStyle(void){
	saveDefaultStyle();
	GuiSetStyle(DEFAULT, TEXT_SIZE, 26);           
	GuiSetStyle(BUTTON, TEXT_SIZE, 26);            
	GuiSetStyle(DEFAULT, TEXT_SPACING, 6);         
	GuiSetStyle(BUTTON, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
	GuiSetIconScale(2);                            	
}

void setDefaultStyle(void){
	if(defaultAssigned){
		for(int i = 0;  i < defaultCount; i++){
			struct styleElement* e = &defaultStyle[i];
			GuiSetStyle(e->control, e->property, e->value);           
		}
		GuiSetIconScale(1);     
	}
}	


typedef struct {
	bool windowActive;
	bool createPressed;
	bool cancelPressed;
	char mazeName[32];
	char rowsText[32];
	char colsText[32];
	 int textActive; // 0 = nenhum, 1 = name, 2 = rows, 3 = cols
} GenerateFormState;

GenerateFormState initGenerateFormState(const char* defaultName,int defaultRows,int defaultCols){
	GenerateFormState ctx = {0};
	strncpy(ctx.mazeName,defaultName,_countof(ctx.mazeName));
	snprintf(ctx.rowsText,_countof(ctx.rowsText),"%u",defaultRows);
	snprintf(ctx.colsText,_countof(ctx.colsText),"%u",defaultCols);
	ctx.createPressed = false;
	return ctx;
};

void drawGenerateFormWindow(GenerateFormState* ctx){
	if(!ctx->windowActive) return;
	
	int modalW = 500;
	int modalH = 300;
	int modalX = (GetScreenWidth()  - modalW) / 2;
	int modalY = (GetScreenHeight() - modalH) / 2;
	
	Rectangle winRect = { modalX, modalY, modalW, modalH };

	
	if (GuiWindowBox(winRect, "Maze Parameters")) {
		ctx->windowActive = false;
	}

	// Inputs dentro da janela
	GuiLabel((Rectangle){ winRect.x + 20, winRect.y + 50,  100, 20 }, "Name:");
	GuiLabel((Rectangle){ winRect.x + 20, winRect.y + 100, 100, 20 }, "Rows:");
	GuiLabel((Rectangle){ winRect.x + 20, winRect.y + 150, 100, 20 }, "Cols:");
	
	if (GuiTextBox((Rectangle){ winRect.x + 120, winRect.y + 50, modalW - 120 - 20, 20 },
               ctx->mazeName, _countof(ctx->mazeName), ctx->textActive == 1)) {
    	ctx->textActive = (ctx->textActive == 1) ? 0 : 1;
	}

	// Rows
	if (GuiTextBox((Rectangle){ winRect.x + 120, winRect.y + 100, modalW - 120 - 20, 20 },
        ctx->rowsText, _countof(ctx->rowsText), ctx->textActive == 2)) {
    		ctx->textActive = (ctx->textActive == 2) ? 0 : 2;
		}

	// Cols
	if (GuiTextBox((Rectangle){ winRect.x + 120, winRect.y + 150, modalW - 120 - 20, 20 },
				ctx->colsText, _countof(ctx->colsText), ctx->textActive == 3)) {
	ctx->textActive = (ctx->textActive == 3) ? 0 : 3;
	}
	int butonPad = 80;
	int butonW = (modalW - butonPad*3)/2;
	// Botões
	if (GuiButton((Rectangle){ winRect.x + butonPad, winRect.y + 220, butonW, 30 }, "Create")) {
		ctx->createPressed = true;
	}
	if (GuiButton((Rectangle){ winRect.x + 2*butonPad+butonW, winRect.y + 220, butonW, 30 }, "Cancel")) {
		// TODO : SHOULD RESET THE TEXTS TO THE DEFAULT?
		ctx->cancelPressed = true;
	}
        
};


#endif