#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
#

APPLE_I_SRCDIR := $(APPSDIR)/plugins/apple_i
APPLE_I_BUILDDIR := $(BUILDDIR)/apps/plugins/apple_i

ROCKS += $(APPLE_I_BUILDDIR)/apple_i.rock

APPLE_I_SRC := $(call preprocess, $(APPLE_I_SRCDIR)/SOURCES)
APPLE_I_OBJ := $(call c2obj, $(APPLE_I_SRC))

OTHER_SRC += $(APPLE_I_SRC)

$(APPLE_I_BUILDDIR)/apple_i.rock: $(APPLE_I_OBJ)
