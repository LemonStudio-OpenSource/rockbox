#             __________               __   ___.
#   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
#   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
#   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
#   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
#                     \/            \/     \/    \/            \/
#

ALTAIRSRCDIR := $(APPSDIR)/plugins/altair
ALTAIRBUILDDIR := $(BUILDDIR)/apps/plugins/altair

# 声明要生成的 .rock 文件
ROCKS += $(ALTAIRBUILDDIR)/altair.rock

# 处理 SOURCES 文件（支持条件编译）
ALTAIR_SRC := $(call preprocess, $(ALTAIRSRCDIR)/SOURCES)
# 将 .c 文件路径转为 .o 文件路径
ALTAIR_OBJ := $(call c2obj, $(ALTAIR_SRC))

# 加入依赖扫描（这样修改头文件后会自动重编译）
OTHER_SRC += $(ALTAIR_SRC)

# 链接生成最终的 .rock 文件
$(ALTAIRBUILDDIR)/altair.rock: $(ALTAIR_OBJ)
