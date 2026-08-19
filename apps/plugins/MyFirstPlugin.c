#include "plugin.h"
void ShowTextOnTheScreen(const char *text);
void ShowTextByXY(int x,int y,const char *text);
void ShowTextOnTheScreen(const char *text)
	{
		rb->splash(HZ*3, text);
		return;
	}
void ShowTextByXY(int x,int y,const char *text)
	{
		rb->lcd_putsxy(x, y, text);
		return;
	}
enum plugin_status plugin_start(const void* parameter)
	{
		(void)parameter;
		ShowTextOnTheScreen("This is my first plugin!!!!!");
		rb->sleep(HZ*1);
		ShowTextByXY(0, 0, "This text is showing from 0,0");
		ShowTextByXY(0, 30, "This text is showing from 0,30");
		rb->lcd_update();
		rb->sleep(HZ*5);
		rb->lcd_clear_display();
		ShowTextByXY(0, 0, "Press any key to continue...");
		rb->lcd_update();
		rb->button_get(true);
		return PLUGIN_OK;
	}