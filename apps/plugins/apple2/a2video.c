#include <stdint.h>
#include <string.h>
#include "a2core.h"
#include "a2video.h"
#include "a2font.h"

/* ============================================================
   Apple II video renderer (text / lo-res / hi-res)
   320x192 framebuffer
   ============================================================ */

uint16_t a2_fb[A2_FB_W * A2_FB_H];

#define COLOR_PHOSPHOR 0x0FEF  /* light green */

/* Apple II lo-res palette (RGB565) */
static const uint16_t palette16[16] = {
    0x0000, 0xE030, 0x0C30, 0xF67C,
    0x0448, 0x7398, 0x1C78, 0x7DF8,
    0xA830, 0xF858, 0xBDD8, 0xFD28,
    0x1E60, 0xFF50, 0x3F78, 0xFFFF
};

static inline int a2_line_base(int row) {
    return ((row & 0x07) << 7) | (((row >> 3) & 3) * 0x28);
}
static inline int a2_text_base(void) {
    return a2_sw_page2 ? 0x0800 : 0x0400;
}
static inline int a2_hires_base(void) {
    return a2_sw_page2 ? 0x4000 : 0x2000;
}
static inline int a2_hires_line_off(int y) {
    return ((y & 0x07) << 10) | (((y >> 3) & 0x07) << 7) | (((y >> 6) & 0x03) * 0x28);
}

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    int i, j;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= A2_FB_H) continue;
        for (i = 0; i < w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= A2_FB_W) continue;
            a2_fb[yy * A2_FB_W + xx] = color;
        }
    }
}

void a2_draw_char_cell(int col, int row, int ch, uint16_t fg, uint16_t bg) {
    int x = col * 8;
    int y = row * 8;
    int r, c;
    if (col < 0 || col >= 40 || row < 0 || row >= 24) return;
    if (bg != 0)
        fill_rect(x, y, 8, 8, bg);
    if (ch < 0x20 || ch > 0x7F) return;
    for (r = 0; r < 8; r++) {
        uint8_t bits = a2font7[ch - 0x20][r];
        for (c = 0; c < 5; c++) {
            if (bits & (0x10 >> c)) {
                int xx = x + c + 1;
                int yy = y + r;
                if (xx < A2_FB_W && yy < A2_FB_H)
                    a2_fb[yy * A2_FB_W + xx] = fg;
            }
        }
    }
}

static void render_text(void) {
    int row, col;
    int base = a2_text_base();
    for (row = 0; row < 24; row++) {
        int lb = a2_line_base(row);
        for (col = 0; col < 40; col++) {
            uint8_t b = a2_mem[base + lb + col];
            a2_draw_char_cell(col, row, b & 0x7F, COLOR_PHOSPHOR, 0);
        }
    }
}

static void render_lores(void) {
    int row, col;
    int base = a2_text_base();
    int maxrow = a2_sw_mixed ? 40 : 48;
    for (row = 0; row < maxrow; row++) {
        for (col = 0; col < 40; col++) {
            int lb = a2_line_base(row / 2);
            uint8_t b = a2_mem[base + lb + col];
            int color = (row & 1) ? ((b >> 4) & 0xF) : (b & 0xF);
            fill_rect(col * 8, row * 4, 8, 4, palette16[color]);
        }
    }
    if (a2_sw_mixed) {
        for (row = 20; row < 24; row++) {
            int lb = a2_line_base(row);
            for (col = 0; col < 40; col++) {
                uint8_t b = a2_mem[base + lb + col];
                a2_draw_char_cell(col, row, b & 0x7F, COLOR_PHOSPHOR, 0);
            }
        }
    }
}

static void render_hires(void) {
    int y, xb, x;
    int base = a2_hires_base();
    int maxy = a2_sw_mixed ? 160 : 192;
    for (y = 0; y < maxy; y++) {
        int lo = a2_hires_line_off(y);
        if (y >= A2_FB_H) break;
        for (xb = 0; xb < 40; xb++) {
            uint8_t b = a2_mem[base + lo + xb];
            for (x = 0; x < 7; x++) {
                int bit = (b >> (6 - x)) & 1;
                if (!bit) continue;
                int xx = xb * 7 + x + 20;
                if (xx < A2_FB_W && y < A2_FB_H)
                    a2_fb[y * A2_FB_W + xx] = COLOR_PHOSPHOR;
            }
        }
    }
    if (a2_sw_mixed) {
        int base2 = a2_text_base();
        for (int row = 20; row < 24; row++) {
            int lb = a2_line_base(row);
            for (int col = 0; col < 40; col++) {
                uint8_t b = a2_mem[base2 + lb + col];
                a2_draw_char_cell(col, row, b & 0x7F, COLOR_PHOSPHOR, 0);
            }
        }
    }
}

void a2_render_video(void) {
    memset(a2_fb, 0, sizeof(a2_fb));
    if (a2_sw_text) {
        render_text();
    } else if (a2_sw_hires) {
        render_hires();
    } else {
        render_lores();
    }
}
