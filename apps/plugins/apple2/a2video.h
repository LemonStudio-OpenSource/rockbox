#ifndef A2VIDEO_H
#define A2VIDEO_H
#include <stdint.h>
#include "a2core.h"

/* framebuffer (defined in a2video.c) */
#define A2_FB_W 320
#define A2_FB_H 192
extern uint16_t a2_fb[A2_FB_W * A2_FB_H];

void a2_render_video(void);

#endif
