#ifndef LEMON_COMPUTER_EMU_H
#define LEMON_COMPUTER_EMU_H

#include "plugin.h"

/* 各模拟器入口，由主菜单调用。返回后主菜单重新显示。 */
enum plugin_status apple1_run(const void* parameter);
enum plugin_status apple2_run(const void* parameter);

#endif