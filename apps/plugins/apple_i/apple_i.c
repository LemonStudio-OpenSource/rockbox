#include <stdint.h>
#include "plugin.h"

/*
 * Apple I Emulator V0.1 - Terminal UI (Final Fix)
 * Key mappings:
 *   Scroll Wheel   : Select character
 *   SELECT         : Input selected character
 *   LEFT/RIGHT     : Move cursor
 *   PLAY           : Enter
 *   MENU           : Exit
 */

/* ============================================================
   Screen dimensions
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

/* ============================================================
   Terminal parameters (5x7 dot matrix)
   ============================================================ */
#define COLS 40
#define ROWS 22
#define CHAR_W 5
#define CHAR_H 7
#define TERM_X_OFFSET ((SCREEN_W - (COLS * CHAR_W)) / 2)
#define TERM_Y_OFFSET 16

/* ============================================================
   Colors (RGB565)
   ============================================================ */
#define COLOR_BG      0x0000    /* Black */
#define COLOR_TEXT    0x07E0    /* Green (Apple I classic) */
#define COLOR_CURSOR  0xFFFF    /* White */
#define COLOR_LABEL   0x8410    /* Gray */

/* ============================================================
   Standard 5x7 font (ASCII 32~95) - Adafruit GFX verified
   ============================================================ */
static const unsigned char font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // 32 space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // 33 !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // 34 "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // 35 #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // 36 $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // 37 %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // 38 &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // 39 '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // 40 (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // 41 )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // 42 *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // 43 +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // 44 ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // 45 -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // 46 .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // 47 /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 48 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 49 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 50 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 51 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 52 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 53 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 54 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 55 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 56 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 57 9
    {0x00, 0x36, 0x36, 0x00, 0x00}, // 58 :
    {0x00, 0x56, 0x36, 0x00, 0x00}, // 59 ;
    {0x00, 0x08, 0x14, 0x22, 0x41}, // 60 <
    {0x14, 0x14, 0x14, 0x14, 0x14}, // 61 =
    {0x41, 0x22, 0x14, 0x08, 0x00}, // 62 >
    {0x02, 0x01, 0x51, 0x09, 0x06}, // 63 ?
    {0x32, 0x49, 0x79, 0x41, 0x3E}, // 64 @
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // 65 A
    {0x7F, 0x49, 0x49, 0x49, 0x36}, // 66 B
    {0x3E, 0x41, 0x41, 0x41, 0x22}, // 67 C
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // 68 D
    {0x7F, 0x49, 0x49, 0x49, 0x41}, // 69 E
    {0x7F, 0x09, 0x09, 0x09, 0x01}, // 70 F
    {0x3E, 0x41, 0x49, 0x49, 0x3A}, // 71 G
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // 72 H
    {0x00, 0x41, 0x7F, 0x41, 0x00}, // 73 I
    {0x20, 0x40, 0x41, 0x3F, 0x01}, // 74 J
    {0x7F, 0x08, 0x14, 0x22, 0x41}, // 75 K
    {0x7F, 0x40, 0x40, 0x40, 0x40}, // 76 L
    {0x7F, 0x02, 0x04, 0x02, 0x7F}, // 77 M
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // 78 N
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // 79 O
    {0x7F, 0x09, 0x09, 0x09, 0x06}, // 80 P
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // 81 Q
    {0x7F, 0x09, 0x19, 0x29, 0x46}, // 82 R
    {0x46, 0x49, 0x49, 0x49, 0x31}, // 83 S
    {0x01, 0x01, 0x7F, 0x01, 0x01}, // 84 T
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // 85 U
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // 86 V
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // 87 W
    {0x63, 0x14, 0x08, 0x14, 0x63}, // 88 X
    {0x03, 0x04, 0x78, 0x04, 0x03}, // 89 Y
    {0x61, 0x51, 0x49, 0x45, 0x43}, // 90 Z
    {0x00, 0x7F, 0x41, 0x41, 0x00}, // 91 [
    {0x02, 0x04, 0x08, 0x10, 0x20}, // 92 backslash
    {0x00, 0x41, 0x41, 0x7F, 0x00}, // 93 ]
    {0x04, 0x02, 0x01, 0x02, 0x04}, // 94 ^
    {0x40, 0x40, 0x40, 0x40, 0x40}  // 95 _
};

/* ============================================================
   Video memory
   ============================================================ */
static char video[ROWS][COLS + 1];
static int cursor_x = 0;
static int cursor_y = 0;

static uint16_t mock_pc = 0x0100;
static uint8_t  mock_a  = 0x00;
static uint8_t  mock_x  = 0x00;
static uint8_t  mock_y  = 0x00;

/* ============================================================
   Keyboard character set
   ============================================================ */
static const char *keyboard_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:-+* /=();,";
static int kb_index = 0;

/* ============================================================
   Draw a character at pixel position (x,y)
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color)
{
    if (ch == ' ') return;

    int idx = (unsigned char)ch - 32;
    if (idx < 0 || idx >= 64) return;

    rb->lcd_set_foreground(color);
    for (int row = 0; row < 7; row++) {
        uint8_t line = font5x7[idx][row];
        for (int col = 0; col < 5; col++) {
            /* Use (4 - col) to map bit order to screen left-to-right */
            if (line & (1 << (4 - col))) {
                rb->lcd_fillrect(px + col, py + row, 1, 1);
            }
        }
    }
}

/* ============================================================
   Render terminal
   ============================================================ */
static void render_terminal(void)
{
    int px = TERM_X_OFFSET;
    int py = TERM_Y_OFFSET;

    /* Clear background */
    rb->lcd_set_foreground(COLOR_BG);
    rb->lcd_fillrect(px, py, COLS * CHAR_W, ROWS * CHAR_H);

    /* Draw characters */
    for (int row = 0; row < ROWS; row++) {
        for (int col = 0; col < COLS; col++) {
            char ch = video[row][col];
            if (ch == 0) ch = ' ';
            int x = px + col * CHAR_W;
            int y = py + row * CHAR_H;
            draw_char_at(x, y, ch, COLOR_TEXT);
        }
    }

    /* Draw cursor */
    if (cursor_x < COLS && cursor_y < ROWS) {
        int cx = px + cursor_x * CHAR_W;
        int cy = py + cursor_y * CHAR_H + CHAR_H - 1;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, CHAR_W, 1);
    }
}

/* ============================================================
   Render keyboard selector
   ============================================================ */
static void render_keyboard(void)
{
    int y = TERM_Y_OFFSET + ROWS * CHAR_H + 2;
    rb->lcd_set_foreground(COLOR_LABEL);
    rb->lcd_setfont(FONT_UI);
    rb->lcd_puts(0, 7, "KB: ");

    int len = rb->strlen(keyboard_chars);
    int start_x = 24;
    int char_spacing = 7;
    int offset_x = (SCREEN_W - start_x - len * char_spacing) / 2 + start_x;

    for (int i = 0; i < len; i++) {
        int px = offset_x + i * char_spacing;
        char ch = keyboard_chars[i];
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_CURSOR);
            rb->lcd_fillrect(px - 1, y - 1, CHAR_W + 2, CHAR_H + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }
}

/* ============================================================
   Status bar
   ============================================================ */
static void render_status(void)
{
    char buf[64];
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_setfont(FONT_UI);
    rb->snprintf(buf, sizeof(buf), "PC:%04X A:%02X X:%02X Y:%02X",
                 mock_pc, mock_a, mock_x, mock_y);
    rb->lcd_puts(0, 0, buf);
    rb->lcd_puts(0, 1, "Apple I Simulator V0.1  (LEFT/RIGHT=move, PLAY=enter)");
}

/* ============================================================
   Draw everything
   ============================================================ */
static void draw_ui(void)
{
    rb->lcd_clear_display();
    rb->lcd_set_background(COLOR_BG);
    render_status();
    render_terminal();
    render_keyboard();
    rb->lcd_update();
}

/* ============================================================
   Type a character
   ============================================================ */
static void type_char(char ch)
{
    if (ch == '\r') {
        cursor_x = 0;
        if (cursor_y < ROWS - 1) {
            cursor_y++;
        } else {
            for (int r = 1; r < ROWS; r++)
                rb->memcpy(video[r-1], video[r], COLS);
            rb->memset(video[ROWS-1], ' ', COLS);
        }
        return;
    }
    if (ch >= 32 && ch <= 95) {
        video[cursor_y][cursor_x] = ch;
        cursor_x++;
        if (cursor_x >= COLS) {
            cursor_x = 0;
            if (cursor_y < ROWS - 1)
                cursor_y++;
            else {
                for (int r = 1; r < ROWS; r++)
                    rb->memcpy(video[r-1], video[r], COLS);
                rb->memset(video[ROWS-1], ' ', COLS);
            }
        }
    }
}

/* ============================================================
   Plugin entry
   ============================================================ */
enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;
    int btn;

    /* Initialize video */
    for (int r = 0; r < ROWS; r++) {
        rb->memset(video[r], ' ', COLS);
        video[r][COLS] = '\0';
    }
    rb->strcpy(video[0], "  WELCOME TO APPLE I EMULATOR       ");
    rb->strcpy(video[1], "  TYPE 'HELLO' AND PRESS ENTER     ");
    rb->strcpy(video[2], "  TO SEE THE MAGIC!                ");
    cursor_x = 0;
    cursor_y = 2;

    while (1) {
        draw_ui();
        btn = rb->button_get(true);

        if (btn == BUTTON_MENU) break;
        else if (btn == BUTTON_PLAY) type_char('\r');
        else if (btn == BUTTON_SCROLL_FWD) {
            kb_index++;
            if (kb_index >= (int)rb->strlen(keyboard_chars)) kb_index = 0;
        } else if (btn == BUTTON_SCROLL_BACK) {
            kb_index--;
            if (kb_index < 0) kb_index = rb->strlen(keyboard_chars) - 1;
        } else if (btn == BUTTON_SELECT) {
            type_char(keyboard_chars[kb_index]);
        } else if (btn == BUTTON_LEFT) {
            if (cursor_x > 0) cursor_x--;
            else if (cursor_y > 0) { cursor_y--; cursor_x = COLS - 1; }
        } else if (btn == BUTTON_RIGHT) {
            if (cursor_x < COLS - 1) cursor_x++;
            else if (cursor_y < ROWS - 1) { cursor_y++; cursor_x = 0; }
        }
    }
    return PLUGIN_OK;
}
