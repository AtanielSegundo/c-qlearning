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

void setCustomStyle(void){
	
	if(!defaultAssigned){
		appendDefault(DEFAULT, TEXT_SIZE);           
		appendDefault(BUTTON, TEXT_SIZE);            
		appendDefault(DEFAULT, TEXT_SPACING);         
		appendDefault(BUTTON, TEXT_ALIGNMENT);
		defaultAssigned = true;
	}

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

#endif