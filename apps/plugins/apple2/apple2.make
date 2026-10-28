#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> >  <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___|  /\____/__/\_ \
#                     \/            \/     \/    \/            \/

APPLE2_SRCDIR := $(APPSDIR)/plugins/apple2
APPLE2_BUILDDIR := $(BUILDDIR)/apps/plugins/apple2

ROCKS += $(APPLE2_BUILDDIR)/apple2.rock

APPLE2_SRC := $(call preprocess, $(APPLE2_SRCDIR)/SOURCES)
APPLE2_OBJ := $(call c2obj, $(APPLE2_SRC))

OTHER_SRC += $(APPLE2_SRC)

$(APPLE2_BUILDDIR)/apple2.rock: $(APPLE2_OBJ)
