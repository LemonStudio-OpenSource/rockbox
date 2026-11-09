#ifndef T80VIDEO_H
#define T80VIDEO_H
#include <stdint.h>
#include "t80core.h"

/* framebuffer: the UI points this at the shared le_fb[] array from
   core/mempool.h (defined in t80video.c) */
#define T80_FB_W LE_FB_W
#define T80_FB_H LE_FB_H
extern uint16_t* t80_fb;

/* set the framebuffer pointer (must point at T80_FB_W*T80_FB_H uint16)
   and clear it. */
void t80_video_init(uint16_t* fb);
void t80_render_video(void);

#endif
