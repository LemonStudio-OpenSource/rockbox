#include <stdint.h>
#include <string.h>
#include "t80core.h"
#include "t80video.h"
#include "t80font.h"

/* ============================================================
   TRS-80 Model I video renderer.
   The display is 64 columns x 16 rows of 5x7 characters, stored at
   $3C00-$3FFF (each row is 64 bytes).  We render into a 320x192
   framebuffer at exactly 320/64 = 5 px per column and 192/16 = 12 px
   per row, then the UI blits it with lcd_bitmap_part (same as apple2).
   ============================================================ */

uint16_t* t80_fb = NULL;

#define T80_COLS 64
#define T80_ROWS 16
#define T80_VIDEO_BASE 0x3C00

#define COLOR_PHOSPHOR 0x0FEF  /* light green, same family as apple2 */

void t80_video_init(uint16_t* fb) {
    t80_fb = fb;
    if (t80_fb)
        memset(t80_fb, 0, (size_t)T80_FB_W * T80_FB_H * sizeof(uint16_t));
}

static void t80_draw_char(int col, int row, int ch, uint16_t color) {
    int x = col * 5;          /* 64 * 5 = 320 */
    int y = row * 12;         /* 16 * 12 = 192 */
    int r, c;
    if (col < 0 || col >= T80_COLS || row < 0 || row >= T80_ROWS) return;
    if (ch < 0x20 || ch > 0x7F) return;
    for (r = 0; r < 7; r++) {
        uint8_t bits = t80font7[ch - 0x20][r];
        for (c = 0; c < 5; c++) {
            if (bits & (0x10 >> c)) {
                int xx = x + c;
                int yy = y + r;
                if (xx < T80_FB_W && yy < T80_FB_H)
                    t80_fb[yy * T80_FB_W + xx] = color;
            }
        }
    }
}

void t80_render_video(void) {
    int row, col;
    if (!t80_fb) return;
    memset(t80_fb, 0, (size_t)T80_FB_W * T80_FB_H * sizeof(uint16_t));
    for (row = 0; row < T80_ROWS; row++) {
        for (col = 0; col < T80_COLS; col++) {
            uint8_t b = trs_mem[T80_VIDEO_BASE + row * T80_COLS + col];
            t80_draw_char(col, row, b & 0x7F, COLOR_PHOSPHOR);
        }
    }
}
