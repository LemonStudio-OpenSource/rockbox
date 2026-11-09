/***************************************************************************
 * cjk_font.h - load the bundled GNU Unifont for CJK text in plugins.
 *
 * Rockbox ships 16-GNU-Unifont.fnt in /.rockbox/fonts but does not load it
 * for plugins.  These tiny helpers load it (with a couple of CJK fallbacks),
 * make it the active LCD font and unload it again on exit.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 ***************************************************************************/
#ifndef RB_CJK_FONT_H
#define RB_CJK_FONT_H

#include "plugin.h"

static const char *const rb_cjk_fonts[] =
{
    ROCKBOX_DIR "/fonts/16-GNU-Unifont.fnt",
    ROCKBOX_DIR "/fonts/16-WenQangYi-Unibit.fnt",
    ROCKBOX_DIR "/fonts/12-WenQangYi-Song.fnt",
    ROCKBOX_DIR "/fonts/12-Sazanami-Mincho.fnt",
    ROCKBOX_DIR "/fonts/10-Sazanami-Mincho.fnt",
};

/* Load the first usable CJK font and make it current.  Returns the font id
 * (>= 0) or -1 when none could be loaded; FONT_UI is kept in that case. */
static inline int rb_cjk_load(void)
{
    int i;
    for (i = 0; i < (int)ARRAYLEN(rb_cjk_fonts); i++)
    {
        int id = rb->font_load(rb_cjk_fonts[i]);
        if (id >= 0)
        {
            rb->lcd_setfont(id);
            return id;
        }
    }
    rb->lcd_setfont(FONT_UI);
    return -1;
}

static inline void rb_cjk_unload(int font_id)
{
    rb->lcd_setfont(FONT_UI);
    if (font_id >= 0)
        rb->font_unload(font_id);
}

#endif /* RB_CJK_FONT_H */
