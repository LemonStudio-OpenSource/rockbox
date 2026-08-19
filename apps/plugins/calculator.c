/***************************************************************************
 *  calculator.c - 简易整数计算器插件 (iPod Color)
 *  
 *  功能：四则运算，支持 long long，显示 × 和 ÷ (UTF-8)
 *  按键 (iPod Color)：
 *    - 上一曲/下一曲 (左/右)   : 切换数字/符号模式
 *    - 滚轮前/后               : 移动光标
 *    - SELECT (中间)           : 输入选中内容
 *    - PLAY (播放)             : 计算结果
 *    - MENU                    : 退出
 *  负数输入：按 '-' 自动变为 0- 形式
 ***************************************************************************/

#include "plugin.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* --------------------- 常量 ------------------------ */
#define MAX_EXPR 80
#define MAX_DISPLAY 160
#define DIGIT_COUNT 10
#define SYMBOL_COUNT 5

/* --------------------- 全局状态 -------------------- */
static char expr[MAX_EXPR];
static int expr_len;
static int mode;                     /* 0=数字, 1=符号 */
static int digit_pos;
static int symbol_pos;
static const char *symbol_display[] = {"+", "-", "×", "÷", "DEL"};

/* --------------------- 辅助函数声明 -------------------- */
static void append_char(char ch);
static void delete_char(void);
static void mode_toggle(void);
static void move_cursor(int direction);
static long long eval_expr(const char *expr_in, int *error);
static void process_select(void);
static void calculate(void);
static void build_display_string(const char *src, char *dst, size_t dst_size);
static void draw_screen(void);

/* --------------------- 实现 ------------------------ */

static void append_char(char ch)
{
    if (expr_len < MAX_EXPR - 1) {
        expr[expr_len++] = ch;
        expr[expr_len] = '\0';
    }
}

static void delete_char(void)
{
    if (expr_len > 0) {
        expr[--expr_len] = '\0';
    }
}

static void mode_toggle(void)
{
    mode = !mode;
}

static void move_cursor(int direction)
{
    if (mode == 0) {
        digit_pos += direction;
        if (digit_pos < 0) digit_pos = DIGIT_COUNT - 1;
        if (digit_pos >= DIGIT_COUNT) digit_pos = 0;
    } else {
        symbol_pos += direction;
        if (symbol_pos < 0) symbol_pos = SYMBOL_COUNT - 1;
        if (symbol_pos >= SYMBOL_COUNT) symbol_pos = 0;
    }
}

/* ---------- 表达式求值 (long long) ---------- */
static long long eval_expr(const char *expr_in, int *error)
{
    *error = 0;
    char buf[MAX_EXPR];
    strcpy(buf, expr_in);

    long long num_stack[MAX_EXPR];
    char op_stack[MAX_EXPR];
    int num_top = -1, op_top = -1;
    int i = 0;

    while (buf[i]) {
        if (isdigit(buf[i])) {
            long long num = 0;
            while (isdigit(buf[i])) {
                num = num * 10 + (buf[i] - '0');
                i++;
            }
            num_stack[++num_top] = num;
        }
        else if (buf[i] == '+' || buf[i] == '-' ||
                 buf[i] == '*' || buf[i] == '/') {
            int cur_prio = (buf[i] == '*' || buf[i] == '/') ? 2 : 1;
            while (op_top >= 0) {
                int top_prio = (op_stack[op_top] == '*' || op_stack[op_top] == '/') ? 2 : 1;
                if (top_prio >= cur_prio) {
                    char op = op_stack[op_top--];
                    long long b = num_stack[num_top--];
                    long long a = num_stack[num_top--];
                    long long res;
                    switch (op) {
                        case '+': res = a + b; break;
                        case '-': res = a - b; break;
                        case '*': res = a * b; break;
                        case '/':
                            if (b == 0) { *error = 1; return 0; }
                            res = a / b;
                            break;
                        default: *error = 1; return 0;
                    }
                    num_stack[++num_top] = res;
                } else break;
            }
            op_stack[++op_top] = buf[i];
            i++;
        } else {
            i++;
        }
    }

    while (op_top >= 0) {
        char op = op_stack[op_top--];
        long long b = num_stack[num_top--];
        long long a = num_stack[num_top--];
        long long res;
        switch (op) {
            case '+': res = a + b; break;
            case '-': res = a - b; break;
            case '*': res = a * b; break;
            case '/':
                if (b == 0) { *error = 1; return 0; }
                res = a / b;
                break;
            default: *error = 1; return 0;
        }
        num_stack[++num_top] = res;
    }

    if (num_top != 0) { *error = 1; return 0; }
    return num_stack[0];
}

/* ---------- 处理 SELECT ---------- */
static void process_select(void)
{
    if (mode == 0) {
        char ch = '0' + digit_pos;
        append_char(ch);
    } else {
        if (symbol_pos == 4) {
            delete_char();
        } else {
            char op;
            switch (symbol_pos) {
                case 0: op = '+'; break;
                case 1: op = '-'; break;
                case 2: op = '*'; break;
                case 3: op = '/'; break;
                default: return;
            }

            /* 负数规则：空表达式输入 '-' 变为 "0-" */
            if (expr_len == 0 && op == '-') {
                expr[expr_len++] = '0';
                expr[expr_len++] = '-';
                expr[expr_len] = '\0';
                return;
            }
            if (expr_len == 0) return;

            char last = expr[expr_len - 1];
            if (last == '+' || last == '-' || last == '*' || last == '/') {
                expr[expr_len - 1] = op;
                return;
            }
            append_char(op);
        }
    }
}

/* ---------- 计算 ---------- */
static void calculate(void)
{
    if (expr_len == 0) return;
    int error = 0;
    long long result = eval_expr(expr, &error);
    if (error) {
        strcpy(expr, "Error");
        expr_len = strlen(expr);
        return;
    }
    rb->snprintf(expr, MAX_EXPR, "%lld", result);
    expr_len = strlen(expr);
}

/* ---------- 构建显示字符串 (将 * / 转换为 × ÷) ---------- */
static void build_display_string(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 4) {
        char ch = src[i];
        if (ch == '*') {
            dst[j++] = '\xC3';
            dst[j++] = '\x97';   /* × */
        } else if (ch == '/') {
            dst[j++] = '\xC3';
            dst[j++] = '\xB7';   /* ÷ */
        } else {
            dst[j++] = ch;
        }
        i++;
    }
    dst[j] = '\0';
}

/* ---------- 绘制屏幕 ---------- */
static void draw_screen(void)
{
    rb->lcd_clear_display();
    rb->lcd_set_foreground(LCD_BLACK);

    /* 显示框 */
    int display_y = 10;
    int display_height = 30;
    rb->lcd_drawrect(0, display_y, LCD_WIDTH, display_height);

    char display_buf[MAX_DISPLAY];
    build_display_string(expr, display_buf, sizeof(display_buf));
    rb->lcd_putsxy(5, display_y + 2, display_buf);

    /* 数字行 */
    int digit_y = display_y + display_height + 20;
    int digit_x = 10;
    int digit_spacing = 20;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        char d[2] = { '0' + i, '\0' };
        rb->lcd_putsxy(digit_x + i * digit_spacing, digit_y, d);
    }
    if (mode == 0) {
        int cx = digit_x + digit_pos * digit_spacing;
        int cy = digit_y + 12;
        rb->lcd_putsxy(cx, cy, "^");
    }

    /* 符号行 */
    int symbol_y = digit_y + 25;
    int symbol_x = 10;
    int symbol_spacing = 30;
    for (int i = 0; i < SYMBOL_COUNT; i++) {
        rb->lcd_putsxy(symbol_x + i * symbol_spacing, symbol_y, symbol_display[i]);
    }
    if (mode == 1) {
        int cx = symbol_x + symbol_pos * symbol_spacing;
        int cy = symbol_y + 12;
        rb->lcd_putsxy(cx, cy, "^");
    }

    rb->lcd_update();
}

/* ---------- 插件入口 ---------- */
enum plugin_status plugin_start(const void *parameter)
{
    (void)parameter;

    expr[0] = '\0';
    expr_len = 0;
    mode = 0;
    digit_pos = 0;
    symbol_pos = 0;

    int button;
    bool quit = false;

    draw_screen();

    while (!quit) {
        button = rb->button_get(true);

        switch (button) {
            case BUTTON_MENU:
                quit = true;
                break;

            case BUTTON_PLAY:
                calculate();
                draw_screen();
                break;

            /* 切换模式：上一曲/下一曲 (iPod 左/右) */
            case BUTTON_LEFT:   /* 上一曲 */
            case BUTTON_RIGHT:  /* 下一曲 */
                mode_toggle();
                draw_screen();
                break;

            case BUTTON_SELECT:
                process_select();
                draw_screen();
                break;

            /* 移动光标：仅用滚轮（左右键已用于切换模式） */
            case BUTTON_SCROLL_FWD:
                move_cursor(1);
                draw_screen();
                break;

            case BUTTON_SCROLL_BACK:
                move_cursor(-1);
                draw_screen();
                break;

            default:
                if (rb->default_event_handler(button) == SYS_USB_CONNECTED)
                    return PLUGIN_USB_CONNECTED;
                break;
        }
    }

    return PLUGIN_OK;
}
