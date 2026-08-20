#include <stdint.h>
#include "plugin.h"

/*
 * Apple I Emulator V0.1 - Terminal UI (System Fixed Font)
 * Uses FONT_SYSFIXED for reliable display.
 */

/* ============================================================
   Screen dimensions
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

/* ============================================================
   Colors (RGB565)
   ============================================================ */
#define COLOR_BG      0x0000
#define COLOR_TEXT    0x07E0    /* Green */
#define COLOR_CURSOR  0xFFFF
#define COLOR_LABEL   0x8410

/* ============================================================
   Video memory (max size, actual usage limited by cols/rows)
   ============================================================ */
#define MAX_COLS 40
#define MAX_ROWS 24
static char video[MAX_ROWS][MAX_COLS + 1];

/* Actual terminal size, set at runtime */
static int cols = 0;
static int rows = 0;

/* Cursor position */
static int cursor_x = 0;
static int cursor_y = 0;

/* Placeholder CPU state */
static uint16_t mock_pc = 0x0100;
static uint8_t  mock_a  = 0x00;
static uint8_t  mock_x  = 0x00;
static uint8_t  mock_y  = 0x00;

/* ============================================================
   Keyboard character set
   ============================================================ */
static const char *keyboard_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 .:-+* /=();,";
static int kb_index = 0;

/* Font dimensions */
static int char_w = 0;
static int char_h = 0;

/* ============================================================
   Initialize font and terminal size
   ============================================================ */
static void init_font(void)
{
    /* Use the system fixed-width font */
    rb->lcd_setfont(FONT_SYSFIXED);
    char_w = rb->font_get_width(FONT_SYSFIXED);
    char_h = rb->font_get_height(FONT_SYSFIXED);

    /* If get_width/height fail, fallback to reasonable defaults */
    if (char_w <= 0) char_w = 6;
    if (char_h <= 0) char_h = 10;

    /* Compute columns and rows, leave 16px for status bar at top */
    cols = (SCREEN_W - 4) / char_w;   /* small margin */
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 10) cols = 10;         /* minimum */

    rows = (SCREEN_H - 16 - 4) / char_h; /* status takes 16px, bottom margin */
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (rows < 4) rows = 4;
}

/* ============================================================
   Draw a character using system font at pixel coords
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color)
{
    if (ch == ' ') return;
    char str[2] = {ch, 0};
    rb->lcd_set_foreground(color);
    rb->lcd_setfont(FONT_SYSFIXED);
    rb->lcd_putsxy(px, py, str);
}

/* ============================================================
   Render terminal
   ============================================================ */
static void render_terminal(void)
{
    int px = 2;   /* small left margin */
    int py = 16;  /* status bar height */

    /* Clear terminal area */
    rb->lcd_set_foreground(COLOR_BG);
    rb->lcd_fillrect(px, py, cols * char_w, rows * char_h);

    /* Draw characters */
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            char ch = video[row][col];
            if (ch == 0) ch = ' ';
            int x = px + col * char_w;
            int y = py + row * char_h;
            draw_char_at(x, y, ch, COLOR_TEXT);
        }
    }

    /* Draw cursor (underline) */
    if (cursor_x < cols && cursor_y < rows) {
        int cx = px + cursor_x * char_w;
        int cy = py + cursor_y * char_h + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
}

/* ============================================================
   Render keyboard selector
   ============================================================ */
static void render_keyboard(void)
{
    int y = 16 + rows * char_h + 2;
    rb->lcd_set_foreground(COLOR_LABEL);
    rb->lcd_setfont(FONT_SYSFIXED);
    rb->lcd_puts(0, 7, "KB: ");  /* using text row coordinates */

    int len = rb->strlen(keyboard_chars);
    int start_x = 24;
    int char_spacing = char_w + 1;
    int total_width = len * char_spacing;
    int offset_x = (SCREEN_W - start_x - total_width) / 2 + start_x;

    for (int i = 0; i < len; i++) {
        int px = offset_x + i * char_spacing;
        char ch = keyboard_chars[i];
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_CURSOR);
            rb->lcd_fillrect(px - 1, y - 1, char_w + 2, char_h + 2);
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
    rb->lcd_setfont(FONT_SYSFIXED);
    rb->snprintf(buf, sizeof(buf), "PC:%04X A:%02X X:%02X Y:%02X",
                 mock_pc, mock_a, mock_x, mock_y);
    rb->lcd_puts(0, 0, buf);
    rb->lcd_puts(0, 1, "Apple I V0.1  (LEFT/RIGHT=move, PLAY=enter)");
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
   Type a character (with scrolling)
   ============================================================ */
static void type_char(char ch)
{
    if (ch == '\r') {
        cursor_x = 0;
        if (cursor_y < rows - 1) {
            cursor_y++;
        } else {
            for (int r = 1; r < rows; r++)
                rb->memcpy(video[r-1], video[r], cols);
            rb->memset(video[rows-1], ' ', cols);
        }
        return;
    }
    if (ch >= 32 && ch <= 95) {
        video[cursor_y][cursor_x] = ch;
        cursor_x++;
        if (cursor_x >= cols) {
            cursor_x = 0;
            if (cursor_y < rows - 1)
                cursor_y++;
            else {
                for (int r = 1; r < rows; r++)
                    rb->memcpy(video[r-1], video[r], cols);
                rb->memset(video[rows-1], ' ', cols);
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

    init_font();

    /* Clear video memory */
    for (int r = 0; r < MAX_ROWS; r++) {
        rb->memset(video[r], ' ', MAX_COLS);
        video[r][MAX_COLS] = '\0';
    }
    /* Set initial welcome text (within actual cols) */
    rb->strcpy(video[0], "WELCOME TO APPLE I");
    rb->strcpy(video[1], "TYPE HELLO");
    rb->strcpy(video[2], "PRESS ENTER");
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
            else if (cursor_y > 0) { cursor_y--; cursor_x = cols - 1; }
        } else if (btn == BUTTON_RIGHT) {
            if (cursor_x < cols - 1) cursor_x++;
            else if (cursor_y < rows - 1) { cursor_y++; cursor_x = 0; }
        }
    }
    return PLUGIN_OK;
}
