#ifndef A2VIDEO_H
#define A2VIDEO_H
#include <stdint.h>
#include "a2core.h"

/* framebuffer: le_fb[] from core/mempool.h, shared with the TRS-80
   renderer (the array itself is defined in core/mempool.c) */
#define A2_FB_W LE_FB_W
#define A2_FB_H LE_FB_H

void a2_render_video(void);

#endif
