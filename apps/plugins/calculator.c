/***************************************************************************
 *  calculator.c - 简易整数计算器插件 (iPod Color 优化)
 *  
 *  功能：
 *    - 四则运算：加、减、乘、除
 *    - 支持长整数 (long long，范围约 ±9e18)
 *    - 负数输入：按 '-' 自动转为 0- 形式
 *    - 显示乘号 ×、除号 ÷ (UTF-8)
 *
 *  按键映射 (iPod Color)：
 *    - 上一曲 (左键) / 下一曲 (右键)  : 切换数字/符号模式
 *    - 滚轮向前/向后                 : 移动光标
 *    - SELECT (中间键)               : 输入当前选中的数字或符号
 *    - PLAY (播放键)                 : 计算结果
 *    - MENU (菜单键)                 : 退出插件
 *
 *  内存占用：栈数组较小，完全在 512KB 插件缓冲区以内。
 ***************************************************************************/

#include "plugin.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

/* --------------------- 常量定义 ------------------------ */
#define MAX_EXPR 80            /* 表达式字符串最大长度（内部存储 * / ） */
#define MAX_DISPLAY 160        /* 显示缓冲区大小（×÷ 各占 2 字节 UTF-8） */
#define DIGIT_COUNT 10         /* 数字 0~9 */
#define SYMBOL_COUNT 5         /* 运算符 + - × ÷ 和 DEL */

/* --------------------- 全局状态变量 -------------------- */
static char expr[MAX_EXPR];          /* 内部表达式，使用 * / */
static int expr_len;                 /* expr 有效长度 */
static int mode;                     /* 0=数字模式，1=符号模式 */
static int digit_pos;                /* 数字光标 (0~9) */
static int symbol_pos;               /* 符号光标 (0~4) */
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

/* --------------------- 函数实现 ------------------------ */

/* 向表达式尾部追加一个字符（内部字符） */
static void append_char(char ch)
{
    if (expr_len < MAX_EXPR - 1) {
        expr[expr_len++] = ch;
        expr[expr_len] = '\0';
    }
}

/* 删除最后一个字符 */
static void delete_char(void)
{
    if (expr_len > 0) {
        expr[--expr_len] = '\0';
    }
}

/* 切换数字/符号模式 */
static void mode_toggle(void)
{
    mode = !mode;
}

/* 移动光标 (direction: 1右, -1左) */
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

/* ---------- 表达式求值（调度场算法），支持 long long ----------
 * 将中缀表达式转后缀并计算，支持 + - * / (整数除法，向零取整)
 * 成功返回结果，失败时 *error = 1 并返回 0
 */
static long long eval_expr(const char *expr_in, int *error)
{
    *error = 0;

    char buf[MAX_EXPR];
    strcpy(buf, expr_in);

    /* 因为内部存储已经使用 * 和 /，无需再转换 × ÷ */

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
                            res = a / b;   /* 整数除法，向零取整 */
                            break;
                        default:  *error = 1; return 0;
                    }
                    num_stack[++num_top] = res;
                } else {
                    break;
                }
            }
            op_stack[++op_top] = buf[i];
            i++;
        } else {
            /* 忽略空格等无效字符（理论上不会出现） */
            i++;
        }
    }

    /* 剩余运算符出栈计算 */
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
            default:  *error = 1; return 0;
        }
        num_stack[++num_top] = res;
    }

    if (num_top != 0) {
        *error = 1;
        return 0;
    }
    return num_stack[0];
}

/* ---------- 处理 SELECT 按键输入 ---------- */
static void process_select(void)
{
    if (mode == 0) {
        /* 输入数字 */
        char ch = '0' + digit_pos;
        append_char(ch);
    } else {
        /* 符号模式 */
        if (symbol_pos == 4) {
            /* DEL 删除 */
            delete_char();
        } else {
            char op;
            switch (symbol_pos) {
                case 0: op = '+'; break;
                case 1: op = '-'; break;
                case 2: op = '*'; break;   /* 内部存储用 * */
                case 3: op = '/'; break;   /* 内部存储用 / */
                default: return;
            }

            /* 负数输入规则：表达式为空时输入 '-' 自动变为 "0-" */
            if (expr_len == 0 && op == '-') {
                expr[expr_len++] = '0';
                expr[expr_len++] = '-';
                expr[expr_len] = '\0';
                return;
            }

            /* 表达式不能以运算符开头（除上述负数规则外） */
            if (expr_len == 0) return;

            /* 连续运算符替换（最后一个运算符被替换为新运算符） */
            char last = expr[expr_len - 1];
            if (last == '+' || last == '-' || last == '*' || last == '/') {
                expr[expr_len - 1] = op;
                return;
            }

            /* 正常追加运算符 */
            append_char(op);
        }
    }
}

/* ---------- 按下 PLAY 执行计算 ---------- */
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

    /* 将 long long 结果转为字符串，使用 %lld */
    rb->snprintf(expr, MAX_EXPR, "%lld", result);
    expr_len = strlen(expr);
}

/* ---------- 构建显示字符串：将 * / 替换为 × ÷ (UTF-8) ----------
 * src: 内部表达式 (含 * /)
 * dst: 输出缓冲区，必须足够大 (最多 2*MAX_EXPR)
 */
static void build_display_string(const char *src, char *dst, size_t dst_size)
{
    size_t i = 0, j = 0;
    while (src[i] && j < dst_size - 4) {  /* 留足空间 */
        char ch = src[i];
        if (ch == '*') {
            /* × 的 UTF-8 编码: \xC3\x97 */
            dst[j++] = '\xC3';
            dst[j++] = '\x97';
        } else if (ch == '/') {
            /* ÷ 的 UTF-8 编码: \xC3\xB7 */
            dst[j++] = '\xC3';
            dst[j++] = '\xB7';
        } else {
            dst[j++] = ch;
        }
        i++;
    }
    dst[j] = '\0';
}

/* ---------- 绘制屏幕 UI ---------- */
static void draw_screen(void)
{
    rb->lcd_clear_display();
    rb->lcd_set_foreground(LCD_BLACK);

    /* ---- 显示框 ---- */
    int display_y = 10;
    int display_height = 30;
    rb->lcd_drawrect(0, display_y, LCD_WIDTH, display_height);

    /* 将内部表达式转为显示格式（× ÷） */
    char display_buf[MAX_DISPLAY];
    build_display_string(expr, display_buf, sizeof(display_buf));

    rb->lcd_putsxy(5, display_y + 2, display_buf);

    /* ---- 数字行 (0~9) ---- */
    int digit_y = display_y + display_height + 20;
    int digit_x = 10;
    int digit_spacing = 20;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        char d[2] = { '0' + i, '\0' };
        rb->lcd_putsxy(digit_x + i * digit_spacing, digit_y, d);
    }
    /* 数字光标 */
    if (mode == 0) {
        int cx = digit_x + digit_pos * digit_spacing;
        int cy = digit_y + 12;
        rb->lcd_putsxy(cx, cy, "^");
    }

    /* ---- 符号行 (+ - × ÷ DEL) ---- */
    int symbol_y = digit_y + 25;
    int symbol_x = 10;
    int symbol_spacing = 30;
    for (int i = 0; i < SYMBOL_COUNT; i++) {
        rb->lcd_putsxy(symbol_x + i * symbol_spacing, symbol_y, symbol_display[i]);
    }
    /* 符号光标 */
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

    /* 初始化 */
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

            /* 移动光标：滚轮或方向键 */
            case BUTTON_RIGHT:   /* 但我们已用 RIGHT 切换模式，所以这里不能用，改用滚轮 */
            case BUTTON_SCROLL_FWD:
                move_cursor(1);
                draw_screen();
                break;

            case BUTTON_LEFT:
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
