# apps/plugins/lemon_computer_emu/lemon_computer_emu.make

LEMON_COMPUTER_EMU_SRCDIR := $(APPSDIR)/plugins/lemon_computer_emu
LEMON_COMPUTER_EMU_BUILDDIR := $(BUILDDIR)/apps/plugins/lemon_computer_emu

LEMON_COMPUTER_EMU_SRC := $(call preprocess, $(LEMON_COMPUTER_EMU_SRCDIR)/SOURCES)
LEMON_COMPUTER_EMU_OBJ := $(call c2obj, $(LEMON_COMPUTER_EMU_SRC))

OTHER_SRC += $(LEMON_COMPUTER_EMU_SRC)

ROCKS += $(LEMON_COMPUTER_EMU_BUILDDIR)/lemon_computer_emu.rock

$(LEMON_COMPUTER_EMU_BUILDDIR)/lemon_computer_emu.rock: $(LEMON_COMPUTER_EMU_OBJ)