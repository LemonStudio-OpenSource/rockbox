#include <stdint.h>
#include "plugin.h"

/*
 * Apple I Emulator V0.1 - Terminal UI (Explicit 10-Fixed Font)
 * Loads /.rockbox/fonts/10-Fixed.fnt
 * Keyboard chars split into two rows.
 */

/* ============================================================
   Screen dimensions
   ============================================================ */
#define SCREEN_W 220
#define SCREEN_H 176

/* ============================================================
   Colors (RGB565) - USER CAN CHANGE HERE
   ============================================================ */
#define COLOR_BG      0x0000    /* Background: black */
#define COLOR_TEXT    0x67E0    /* Text color: green (change to e.g. 0xFFFF for white) */
#define COLOR_CURSOR  0xFFFF    /* Cursor underline: white */
#define COLOR_LABEL   0x8410    /* Label "KB:": gray */
#define COLOR_HIGHLIGHT_BG 0xFFFF /* Highlight background: white */

/* ============================================================
   Video memory
   ============================================================ */
#define MAX_COLS 40
#define MAX_ROWS 24
static char video[MAX_ROWS][MAX_COLS + 1];

static int cols = 0;
static int rows = 0;
static int cursor_x = 0;
static int cursor_y = 0;

static uint16_t mock_pc = 0x0100;
static uint8_t  mock_a  = 0x00;
static uint8_t  mock_x  = 0x00;
static uint8_t  mock_y  = 0x00;

/* ============================================================
   Keyboard character set (split into two rows)
   ============================================================ */
static const char *keyboard_chars_row1 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char *keyboard_chars_row2 = "0123456789 .:-+* /=();,";
static int kb_index = 0; /* global index into combined string */
static int kb_total_len = 0;

/* Font handle and dimensions */
static struct font *fixed_font = NULL;
static int char_w = 0;
static int char_h = 0;

/* Terminal geometry constants */
static const int TOP_OFFSET = 14;
static const int BOTTOM_MARGIN = 4;
static const int LEFT_OFFSET = 0;

/* ============================================================
   Explicitly load 10-Fixed font
   ============================================================ */
static bool init_font(void)
{
    fixed_font = rb->font_load("/.rockbox/fonts/10-Fixed.fnt");
    if (!fixed_font) {
        rb->splash(HZ*2, "Failed to load 10-Fixed.fnt");
        return false;
    }

    rb->lcd_setfont(fixed_font);

    int w, h;
    rb->lcd_getstringsize("M", &w, &h);
    char_w = w;
    char_h = h;

    if (char_w <= 0 || char_h <= 0) {
        char_w = 6;
        char_h = 10;
    }

    /* Compute terminal size */
    cols = (SCREEN_W - LEFT_OFFSET) / char_w;
    if (cols > MAX_COLS) cols = MAX_COLS;
    if (cols < 10) cols = 10;

    rows = (SCREEN_H - TOP_OFFSET - BOTTOM_MARGIN) / char_h;
    if (rows > MAX_ROWS) rows = MAX_ROWS;
    if (rows < 4) rows = 4;

    kb_total_len = rb->strlen(keyboard_chars_row1) + rb->strlen(keyboard_chars_row2);
    return true;
}

/* ============================================================
   Get character at global index
   ============================================================ */
static char get_kb_char(int idx)
{
    int len1 = rb->strlen(keyboard_chars_row1);
    if (idx < len1)
        return keyboard_chars_row1[idx];
    else
        return keyboard_chars_row2[idx - len1];
}

/* ============================================================
   Draw a character using loaded font
   ============================================================ */
static void draw_char_at(int px, int py, char ch, uint16_t color)
{
    if (ch == ' ') return;
    char str[2] = {ch, 0};
    rb->lcd_set_foreground(color);
    rb->lcd_setfont(fixed_font);
    rb->lcd_putsxy(px, py, str);
}

/* ============================================================
   Render terminal
   ============================================================ */
static void render_terminal(void)
{
    int px = LEFT_OFFSET;
    int py = TOP_OFFSET;

    rb->lcd_set_foreground(COLOR_BG);
    rb->lcd_fillrect(px, py, cols * char_w, rows * char_h);

    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            char ch = video[row][col];
            if (ch == 0) ch = ' ';
            int x = px + col * char_w;
            int y = py + row * char_h;
            draw_char_at(x, y, ch, COLOR_TEXT);
        }
    }

    if (cursor_x < cols && cursor_y < rows) {
        int cx = px + cursor_x * char_w;
        int cy = py + cursor_y * char_h + char_h - 2;
        rb->lcd_set_foreground(COLOR_CURSOR);
        rb->lcd_fillrect(cx, cy, char_w, 1);
    }
}

/* ============================================================
   Render keyboard selector (two rows)
   ============================================================ */
static void render_keyboard(void)
{
    int y_base = TOP_OFFSET + rows * char_h + 2;
    int y = y_base;
    int char_spacing = char_w + 2; /* small gap */
    int len1 = rb->strlen(keyboard_chars_row1);
    int len2 = rb->strlen(keyboard_chars_row2);

    /* Row 1 */
    int x_start = (SCREEN_W - len1 * char_spacing) / 2;
    for (int i = 0; i < len1; i++) {
        int px = x_start + i * char_spacing;
        char ch = keyboard_chars_row1[i];
        if (i == kb_index) {
            rb->lcd_set_foreground(COLOR_HIGHLIGHT_BG);
            rb->lcd_fillrect(px - 1, y - 1, char_w + 2, char_h + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }

    /* Row 2 */
    y += char_h + 2;
    x_start = (SCREEN_W - len2 * char_spacing) / 2;
    for (int i = 0; i < len2; i++) {
        int idx = len1 + i;
        int px = x_start + i * char_spacing;
        char ch = keyboard_chars_row2[i];
        if (idx == kb_index) {
            rb->lcd_set_foreground(COLOR_HIGHLIGHT_BG);
            rb->lcd_fillrect(px - 1, y - 1, char_w + 2, char_h + 2);
            draw_char_at(px, y, ch, COLOR_BG);
        } else {
            draw_char_at(px, y, ch, COLOR_TEXT);
        }
    }

    /* Label "KB:" positioned above the rows */
    rb->lcd_set_foreground(COLOR_LABEL);
    rb->lcd_setfont(fixed_font);
    rb->lcd_puts(0, 7, "KB: ");
}

/* ============================================================
   Status bar
   ============================================================ */
static void render_status(void)
{
    char buf[64];
    rb->lcd_set_foreground(COLOR_TEXT);
    rb->lcd_setfont(fixed_font);
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

    if (!init_font()) {
        return PLUGIN_ERROR;
    }

    for (int r = 0; r < MAX_ROWS; r++) {
        rb->memset(video[r], ' ', MAX_COLS);
        video[r][MAX_COLS] = '\0';
    }
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
        else if (btn == BUTTON_SCROLL_FWD || btn == BUTTON_SCROLL_FWD) {
            kb_index++;
            if (kb_index >= kb_total_len) kb_index = 0;
        } else if (btn == BUTTON_SCROLL_BACK || btn == BUTTON_SCROLL_BACK) {
            kb_index--;
            if (kb_index < 0) kb_index = kb_total_len - 1;
        } else if (btn == BUTTON_SELECT) {
            char ch = get_kb_char(kb_index);
            type_char(ch);
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
