/***************************************************************************
 * Moon phase almanac for Rockbox
 *
 * A small astronomy almanac: lunar age, illumination, a rendered moon
 * disc, the next new/full moon dates and a month calendar with a tiny
 * moon icon in every day cell.
 *
 * Integer fixed point only, classic mean lunation:
 *   age = (JD - JD(2000-01-06 18:14 UT)) mod 29.530588853 days
 * JD is the Julian Day Number of the selected day at 12:00 UT, scaled
 * by 100000 (1e-5 day).  Synodic month 2953059 units, epoch
 * 245155026000 units.  Illumination = (1 - cos(phase angle)) / 2 with
 * Bhaskara I's fixed point sine (error well below 1 percent).
 *
 * GPLv2 or later.  No warranty.
 **************************************************************************/

#include "plugin.h"
#include "lib/pluginlib_actions.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* 1e-5 day units: 29.530588853 * 1e5 = 2953058.8853 -> 2953059 */
#define SYNODIC_X100000    2953059L
#define DAY_X100000        100000L
#define EPOCH_JD_X100000   245155026000LL  /* 2000-01-06 18:14 UT * 1e5    */
#define PHASE_WINDOW       70000L          /* +/- 0.7 day                 */

#define JDN_MIN            2415021L        /* 1900-01-01                  */
#define JDN_MAX            2524593L        /* 2199-12-31                  */

#define VIEW_MOON          0
#define VIEW_CALENDAR      1

enum
{
    PHASE_NEW = 0, PHASE_WAXING_CRESCENT, PHASE_FIRST_QUARTER,
    PHASE_WAXING_GIBBOUS, PHASE_FULL, PHASE_WANING_GIBBOUS,
    PHASE_LAST_QUARTER, PHASE_WANING_CRESCENT, PHASE_COUNT
};

struct moon_info
{
    int illum;          /* 0..100 percent                           */
    int cos10000;       /* cosine of phase angle, -10000..10000     */
    int waxing;         /* 1 while the lit fraction grows           */
    int phase;          /* PHASE_*                                  */
    int days_new;       /* days to the next new moon                */
    int days_full;      /* days to the next full moon               */
};

static const struct button_mapping *plugin_contexts[] = { pla_main_ctx };

static int32_t g_jdn;   /* selected day as a Julian Day Number          */
static int g_view;      /* VIEW_MOON or VIEW_CALENDAR                   */

static const char *const phase_names[PHASE_COUNT] =
{
    "NEW", "WAXING CRESCENT", "FIRST QUARTER", "WAXING GIBBOUS",
    "FULL", "WANING GIBBOUS", "LAST QUARTER", "WANING CRESCENT"
};

static const char *const month_names[12] =
{
    "January", "February", "March", "April", "May", "June", "July",
    "August", "September", "October", "November", "December"
};

static const char *const weekday_names[2][7] =
{
    { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" },
    { "S", "M", "T", "W", "T", "F", "S" }
};

/* ---- calendar helpers ------------------------------------------------- */

static int days_in_month(int year, int month)
{
    static const signed char dm[12] =
        { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

    if (month < 1 || month > 12)
        return 31;
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0)
                       || year % 400 == 0))
        return 29;
    return dm[month - 1];
}

/* Gregorian date -> Julian Day Number. */
static int32_t jdn_from_date(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;

    return (int32_t)(day + (153 * m + 2) / 5 + 365 * y + y / 4 - y / 100
                     + y / 400 - 32045);
}

/* Julian Day Number -> Gregorian date. */
static void date_from_jdn(int32_t jdn, int *year, int *month, int *day)
{
    int32_t a = jdn + 32044;
    int32_t b = (4 * a + 3) / 146097;
    int32_t c = a - (146097 * b) / 4;
    int32_t d = (4 * c + 3) / 1461;
    int32_t e = c - (1461 * d) / 4;
    int32_t m = (5 * e + 2) / 153;

    *day = (int)(e - (153 * m + 2) / 5 + 1);
    *month = (int)(m + 3 - 12 * (m / 10));
    *year = (int)(100 * b + d - 4800 + m / 10);
}

static void step_day(int delta)
{
    g_jdn += delta;
    if (g_jdn < JDN_MIN) g_jdn = JDN_MIN;
    if (g_jdn > JDN_MAX) g_jdn = JDN_MAX;
}

static void step_month(int delta)
{
    int year, month, day, dim;

    date_from_jdn(g_jdn, &year, &month, &day);
    month += delta;
    while (month < 1)
    {
        month += 12; year--;
    }
    while (month > 12)
    {
        month -= 12; year++;
    }
    dim = days_in_month(year, month);
    if (day > dim)
        day = dim;
    g_jdn = jdn_from_date(year, month, day);
    if (g_jdn < JDN_MIN) g_jdn = JDN_MIN;
    if (g_jdn > JDN_MAX) g_jdn = JDN_MAX;
}

/* ---- astronomy -------------------------------------------------------- */

/* Bhaskara I's sine approximation, x10 in 0..1800 (tenths of a degree).
 * Returns sin(x) * 10000.  The denominator is at least 3240000. */
static int sin_deg10(int x10)
{
    int32_t p = (int32_t)x10 * (1800 - x10);
    int64_t num = (int64_t)4 * (int64_t)p * 10000;
    int32_t den = 4050000 - p;

    return (int)(num / den);
}

/* cos(phase angle), phase angle in tenths of a degree. */
static int cos_deg10(int deg10)
{
    int x = deg10 + 900;

    if (x >= 3600)
        x -= 3600;
    if (x > 1800)
        return -sin_deg10(x - 1800);
    return sin_deg10(x);
}

static int32_t dist32(int32_t a, int32_t b)
{
    return (a > b) ? a - b : b - a;
}

/* Phase name from the lunar age (1e-5 day units) and illumination. */
static int phase_of(int32_t age, int illum)
{
    int32_t half = SYNODIC_X100000 / 2;
    int32_t quarter = SYNODIC_X100000 / 4;

    if (illum == 0)         /* 0 percent is always a new moon day     */
        return PHASE_NEW;
    if (illum >= 100)       /* and 100 percent is always a full moon  */
        return PHASE_FULL;
    if ((age <= half ? age : SYNODIC_X100000 - age) <= PHASE_WINDOW)
        return PHASE_NEW;
    if (dist32(age, half) <= PHASE_WINDOW)
        return PHASE_FULL;
    if (age <= half)
    {
        if (dist32(age, quarter) <= PHASE_WINDOW)
            return PHASE_FIRST_QUARTER;
        return (age < quarter) ? PHASE_WAXING_CRESCENT
                               : PHASE_WAXING_GIBBOUS;
    }
    if (dist32(age, quarter * 3) <= PHASE_WINDOW)
        return PHASE_LAST_QUARTER;
    return (age < quarter * 3) ? PHASE_WANING_GIBBOUS
                               : PHASE_WANING_CRESCENT;
}

/* Fill in illumination, phase name and the next new/full moons. */
static void moon_calc(int32_t jdn, struct moon_info *mi)
{
    int64_t age, rem;
    int deg10;
    /* sample the selected day at 12:00 UT */
    age = ((int64_t)jdn * DAY_X100000 - EPOCH_JD_X100000)
          % SYNODIC_X100000;
    if (age < 0) age += SYNODIC_X100000;
    deg10 = (int)((age * 3600 + SYNODIC_X100000 / 2) / SYNODIC_X100000);
    if (deg10 >= 3600) deg10 -= 3600;
    mi->cos10000 = cos_deg10(deg10);
    mi->illum = (10000 - mi->cos10000) / 200;
    mi->waxing = (deg10 < 1800) ? 1 : 0;
    mi->phase = phase_of((int32_t)age, mi->illum);

    /* round the remaining time to the nearest calendar day */
    rem = SYNODIC_X100000 - age;
    mi->days_new = (int)((rem + DAY_X100000 / 2) / DAY_X100000);
    if (age <= SYNODIC_X100000 / 2)
        rem = SYNODIC_X100000 / 2 - age;
    else
        rem = SYNODIC_X100000 + SYNODIC_X100000 / 2 - age;
    mi->days_full = (int)((rem + DAY_X100000 / 2) / DAY_X100000);
}

#if LCD_DEPTH > 1
#if LCD_DEPTH >= 8
#define MOON_DARK LCD_DARKGRAY      /* colour: dim grey on black          */
#else
#define MOON_DARK LCD_LIGHTGRAY     /* greyscale: light grey on white     */
#endif
#define MOON_LIT  LCD_DEFAULT_FG
#endif

static void put_text(int x, int y, const char *text)
{
    rb->lcd_putsxy(x, y, (const unsigned char *)text);
}

static int font_height(void)
{
    int w = 6;
    int h = 8;
    rb->lcd_getstringsize((const unsigned char *)"A", &w, &h);
    return (h > 0) ? h : 8;
}

static int text_width(const char *text)
{
    int w = 0;
    rb->lcd_getstringsize((const unsigned char *)text, &w, NULL);
    return w;
}

/* Integer square root for the scanline half width of the disc. */
static int isqrt_i(int value)
{
    int lo = 0;
    int hi = 128;
    int mid;
    while (lo < hi)
    {
        mid = (lo + hi + 1) / 2;
        if (mid * mid <= value)
            lo = mid;
        else
            hi = mid - 1;
    }
    return lo;
}

/* One horizontal run of the moon.  On colour/greyscale the caller has set
 * the foreground colour; on 1 bit LCDs the dark side is a 50 percent
 * checkerboard so that the disc stays visible. */
static void moon_hspan(int x, int y, int width, int dark)
{
    if (width <= 0)
        return;
#if LCD_DEPTH > 1
    (void)dark;
    rb->lcd_fillrect(x, y, width, 1);
#else
    if (dark)
    {
        int i;
        for (i = 0; i < width; i++)
        {
            if (((x + i + y) & 1) == 0)
                rb->lcd_drawpixel(x + i, y);
        }
    }
    else
    {
        rb->lcd_fillrect(x, y, width, 1);
    }
#endif
}

/* Draw a moon disc.  The terminator is an ellipse: on each scanline its
 * offset from the centre is cos(phase) times that scanline's half width.
 * Waxing means the lit limb is on the right, waning on the left. */
static void draw_disc(int cx, int cy, int r, int illum, int cos10000,
                      int waxing)
{
    int dy, t;
    if (r < 1 || r > 80) return;
#if LCD_DEPTH > 1
    rb->lcd_set_foreground(MOON_DARK);
#endif
    for (dy = -r; dy <= r; dy++)
    {
        int w = isqrt_i(r * r - dy * dy);
        int dark_x, dark_w;
        t = (int)(((int32_t)cos10000 * w) / 10000);
        dark_x = waxing ? -w : -t + 1;
        dark_w = w + t;
        if (illum <= 0) { dark_x = -w; dark_w = 2 * w + 1; }
        if (illum >= 100) dark_w = 0;
        moon_hspan(cx + dark_x, cy + dy, dark_w, 1);
    }
#if LCD_DEPTH > 1
    rb->lcd_set_foreground(MOON_LIT);
#endif
    for (dy = -r; dy <= r && illum > 0; dy++)
    {
        int w = isqrt_i(r * r - dy * dy);
        int lit_x, lit_w;
        t = (int)(((int32_t)cos10000 * w) / 10000);
        lit_x = waxing ? t : -w;
        lit_w = w - t + 1;
        if (illum >= 100) { lit_x = -w; lit_w = 2 * w + 1; }
        moon_hspan(cx + lit_x, cy + dy, lit_w, 0);
    }
#if LCD_DEPTH > 1
    rb->lcd_set_foreground(LCD_DEFAULT_FG);
#endif
}

/* ---- moon view -------------------------------------------------------- */

static void draw_moon_view(void)
{
    struct moon_info mi;
    char buf[48];
    int year, month, day, ny, nm, nd;
    int fh, tw, top, bottom, radius, cx, cy, by;

    date_from_jdn(g_jdn, &year, &month, &day);
    moon_calc(g_jdn, &mi);
    fh = font_height();

    rb->lcd_clear_display();

    rb->snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    put_text(2, 1, buf);
    rb->snprintf(buf, sizeof(buf), "%d%%", mi.illum);
    tw = text_width(buf);
    put_text(LCD_WIDTH - tw - 2, 1, buf);
    put_text(2, 1 + fh, phase_names[mi.phase]);

    /* room for the disc between the status text and the four bottom lines */
    top = 2 * fh + 4;
    bottom = LCD_HEIGHT - 4 * fh - 6;
    radius = (bottom - top) / 2 - 2;
    if (radius > (LCD_WIDTH - 10) / 2) radius = (LCD_WIDTH - 10) / 2;
    if (radius > 80) radius = 80;
    if (radius < 3) radius = 0;

    if (radius > 0)
    {
        cx = LCD_WIDTH / 2;
        cy = (top + bottom) / 2;
        draw_disc(cx, cy, radius, mi.illum, mi.cos10000, mi.waxing);
    }

    by = bottom + 3;
    date_from_jdn(g_jdn + mi.days_new, &ny, &nm, &nd);
    rb->snprintf(buf, sizeof(buf), "New %04d-%02d-%02d %dd",
                 ny, nm, nd, mi.days_new);
    put_text(2, by, buf);
    date_from_jdn(g_jdn + mi.days_full, &ny, &nm, &nd);
    rb->snprintf(buf, sizeof(buf), "Full %04d-%02d-%02d %dd",
                 ny, nm, nd, mi.days_full);
    put_text(2, by + fh, buf);
    put_text(2, by + 2 * fh, "L/R day  U/D month");
    put_text(2, by + 3 * fh, "SEL cal  EXIT quit");
}

/* ---- calendar view ---------------------------------------------------- */

static void draw_calendar_view(void)
{
    struct moon_info mi;
    char buf[48];
    int year, month, day, fh, tw;
    int cell_w, cell_h, grid_x, grid_top, grid_bottom, hints_y;
    int first_jdn, first_wday, ndays, show_icons;
    int i, daynum, col, row, x, gy;

    date_from_jdn(g_jdn, &year, &month, &day);
    moon_calc(g_jdn, &mi);
    fh = font_height();

    rb->lcd_clear_display();

    rb->snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
    put_text(2, 1, buf);
    rb->snprintf(buf, sizeof(buf), "%d%%", mi.illum);
    tw = text_width(buf);
    put_text(LCD_WIDTH - tw - 2, 1, buf);
    put_text(2, 1 + fh, phase_names[mi.phase]);

    rb->snprintf(buf, sizeof(buf), "%s %04d", month_names[month - 1], year);
    tw = text_width(buf);
    put_text((LCD_WIDTH - tw) / 2, 1 + 2 * fh, buf);

    cell_w = (LCD_WIDTH - 4) / 7;
    if (cell_w < 7) cell_w = 7;
    if (cell_w > 46) cell_w = 46;
    grid_x = (LCD_WIDTH - 7 * cell_w) / 2;

    hints_y = LCD_HEIGHT - 2 * fh - 2;
    grid_top = 2 + 4 * fh;
    grid_bottom = hints_y - 3;
    cell_h = (grid_bottom - grid_top) / 6;
    if (cell_h < 1) cell_h = 1;
    if (cell_h > 30) cell_h = 30;
    show_icons = (cell_h >= fh + 8 && cell_w >= 9);

    first_jdn = jdn_from_date(year, month, 1);
    first_wday = (int)((first_jdn + 1) % 7);
    ndays = days_in_month(year, month);

    for (col = 0; col < 7; col++)
    {
        const char *name = weekday_names[cell_w >= 20 ? 0 : 1][col];
        tw = text_width(name);
        put_text(grid_x + col * cell_w + (cell_w - tw) / 2,
                 1 + 3 * fh, name);
    }

    for (i = 0; i < first_wday + ndays; i++)
    {
        struct moon_info ci;
        int32_t cj;

        daynum = i - first_wday + 1;
        col = i % 7;
        row = i / 7;
        if (row >= 6)
            break;

        x = grid_x + col * cell_w;
        gy = grid_top + row * cell_h;
        cj = first_jdn + (daynum - 1);
        moon_calc(cj, &ci);

        rb->snprintf(buf, sizeof(buf), "%d", daynum);
        tw = text_width(buf);
        put_text(x + (cell_w - tw) / 2, gy + 1, buf);

        if (show_icons)
        {
            int icon_x = x + (cell_w - 5) / 2;
            int icon_y = gy + fh + 1;
            draw_disc(icon_x + 2, icon_y + 2, 2,
                      ci.illum, ci.cos10000, ci.waxing);
        }

        if (cj == g_jdn && cell_w > 4 && cell_h > 4)
            rb->lcd_drawrect(x + 1, gy + 1, cell_w - 2, cell_h - 2);
    }

    put_text(2, hints_y, "L/R month  U/D day");
    put_text(2, hints_y + fh, "SEL moon  EXIT quit");
}

/* ---- plugin entry point ----------------------------------------------- */

enum plugin_status plugin_start(const void *parameter)
{
    struct tm *now;
    int year = 2026, month = 1, day = 1;
    int action;

    (void)parameter;
    /* default to today; without a valid RTC fall back to a fixed date that
     * can still be navigated with the keys */
    now = rb->get_time();
    if (now != NULL
        && now->tm_year + 1900 >= 1971 && now->tm_year + 1900 <= 2199
        && now->tm_mon >= 0 && now->tm_mon < 12
        && now->tm_mday >= 1 && now->tm_mday <= 31)
    {
        year = now->tm_year + 1900;
        month = now->tm_mon + 1;
        day = now->tm_mday;
    }

    g_jdn = jdn_from_date(year, month, day);
    g_view = VIEW_MOON;
#if LCD_DEPTH > 1
    rb->lcd_set_backdrop(NULL);
    rb->lcd_set_foreground(LCD_DEFAULT_FG);
    rb->lcd_set_background(LCD_DEFAULT_BG);
#endif
    rb->lcd_set_drawmode(DRMODE_SOLID);
    while (1)
    {
        if (g_view == VIEW_MOON)
            draw_moon_view();
        else
            draw_calendar_view();
        rb->lcd_update();

        action = pluginlib_getaction(TIMEOUT_BLOCK, plugin_contexts,
                                     ARRAYLEN(plugin_contexts));

        switch (action)
        {
        case PLA_EXIT: case PLA_CANCEL:
            return PLUGIN_OK;
        case PLA_SELECT:
            g_view = (g_view == VIEW_MOON) ? VIEW_CALENDAR : VIEW_MOON;
            break;
        case PLA_LEFT: case PLA_LEFT_REPEAT:
        case PLA_RIGHT: case PLA_RIGHT_REPEAT:
        {
            int d = (action == PLA_LEFT || action == PLA_LEFT_REPEAT) ? -1 : 1;
            if (g_view == VIEW_MOON) step_day(d); else step_month(d);
            break;
        }
        case PLA_UP: case PLA_UP_REPEAT:
        case PLA_DOWN: case PLA_DOWN_REPEAT:
        {
            int d = (action == PLA_UP || action == PLA_UP_REPEAT) ? -1 : 1;
            if (g_view == VIEW_MOON) step_month(d); else step_day(d);
            break;
        }
        default:
            break;
        }
    }
}
