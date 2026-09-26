#---------------------------------------------------------------------------------
# PKForgeNX — Makefile devkitPro (devkitA64 + libnx)
#
# Dépendances (dkp-pacman) :
#   sudo dkp-pacman -S switch-dev switch-sdl2 switch-mesa switch-libdrm_nouveau
# ImGui est vendorisé en sous-module git (lib/imgui, tag v1.91.9b) :
#   git submodule update --init --recursive
#
# Cibles : make | make clean | make run (nxlink, nécessite PKF_DEBUG=1 pour stdio)
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# Métadonnées NACP
#---------------------------------------------------------------------------------
APP_TITLE	:=	PKForgeNX
APP_AUTHOR	:=	PKForgeNX Team
APP_VERSION	:=	0.1.0
# Icône 256x256 JPEG. Si absente, on retombe sur l'icône par défaut de libnx.
ICON		:=	$(if $(wildcard $(TOPDIR)/assets/icon/icon.jpg),assets/icon/icon.jpg,)

#---------------------------------------------------------------------------------
# Arborescence
#---------------------------------------------------------------------------------
TARGET		:=	PKForgeNX
BUILD		:=	build
SOURCES		:=	source source/core source/crypto source/pkm source/ui
DATA		:=	data
INCLUDES	:=	include
ROMFS		:=	romfs

# ImGui : on ne compile que le cœur + les deux backends utilisés (SDL2 + OpenGL3).
# Les backends ne sont PAS ajoutés à SOURCES (le dossier en contient ~40).
IMGUI_DIR	:=	lib/imgui
IMGUI_SRC	:=	imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp \
			imgui_demo.cpp imgui_impl_sdl2.cpp imgui_impl_opengl3.cpp

#---------------------------------------------------------------------------------
# Génération de code
#---------------------------------------------------------------------------------
ARCH	:=	-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

# IMGUI_IMPL_OPENGL_ES3 : le backend inclut <GLES3/gl3.h> (switch-mesa) au lieu de
# son loader desktop (gl3w/dlopen) qui n'existe pas sur Horizon.
# IMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS : pas de fork/execvp sur Horizon (OpenInShell).
DEFINES	:=	-D__SWITCH__ -DIMGUI_IMPL_OPENGL_ES3 -DIMGUI_DISABLE_DEFAULT_SHELL_FUNCTIONS \
			-DPKF_VERSION=\"$(APP_VERSION)\"

ifneq ($(strip $(PKF_DEBUG)),)
DEFINES	+=	-DPKF_DEBUG=1
OPTFLAGS :=	-O0
else
OPTFLAGS :=	-O2
endif

# Flags SDL2 fournis par le .pc du portlib (chemins include/SDL2, _REENTRANT...).
SDL2_CFLAGS	:=	$(shell $(PORTLIBS)/bin/aarch64-none-elf-pkg-config --cflags sdl2 2>/dev/null)

CFLAGS	:=	-g -Wall -Wextra $(OPTFLAGS) -ffunction-sections -fdata-sections \
			$(ARCH) $(DEFINES)

CFLAGS	+=	$(INCLUDE) $(SDL2_CFLAGS)

# RTTI désactivé (taille binaire). Exceptions conservées : la STL (std::vector,
# std::string) signale l'OOM via std::bad_alloc, qu'on veut pouvoir attraper.
CXXFLAGS	:=	$(CFLAGS) -std=gnu++20 -fno-rtti

ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) \
			-Wl,--gc-sections -Wl,-Map,$(notdir $*.map)

# Ordre de link significatif (liaison statique, résolution gauche -> droite) :
#   SDL2 -> GLESv2 -> EGL -> glapi -> drm_nouveau -> nx
# On n'utilise pas `pkg-config --libs sdl2` car il omet -lGLESv2 et place -lglapi
# avant l'endroit où GLESv2 en a besoin.
LIBS	:=	-lSDL2 -lGLESv2 -lEGL -lglapi -ldrm_nouveau -lnx -lpthread

#---------------------------------------------------------------------------------
# Répertoires de bibliothèques (doivent contenir include/ et lib/)
#---------------------------------------------------------------------------------
LIBDIRS	:=	$(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

ifeq ($(wildcard $(CURDIR)/$(IMGUI_DIR)/imgui.h),)
$(error "ImGui introuvable dans $(IMGUI_DIR). Lancez : git submodule update --init --recursive")
endif

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir)) \
			$(CURDIR)/$(IMGUI_DIR) \
			$(CURDIR)/$(IMGUI_DIR)/backends

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))) $(IMGUI_SRC)
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CXX)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SRC	:=	$(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES		:=	$(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			-I$(CURDIR)/$(IMGUI_DIR) \
			-I$(CURDIR)/$(IMGUI_DIR)/backends \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(ICON)),)
	export APP_ICON := $(LIBNX)/default_icon.jpg
else
	export APP_ICON := $(TOPDIR)/$(ICON)
endif

export NROFLAGS += --icon=$(APP_ICON)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp

ifneq ($(ROMFS),)
	export NROFLAGS += --romfsdir=$(CURDIR)/$(ROMFS)
endif

.PHONY: $(BUILD) clean all run

#---------------------------------------------------------------------------------
all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf

#---------------------------------------------------------------------------------
# Envoi + exécution via nxlink (hbmenu : touche Y). -s redirige stdout/stderr.
run: all
	@nxlink -s $(TARGET).nro

#---------------------------------------------------------------------------------
else
.PHONY:	all

DEPENDS	:=	$(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# Cibles principales
#---------------------------------------------------------------------------------
all	:	$(OUTPUT).nro

$(OUTPUT).nro	:	$(OUTPUT).elf $(OUTPUT).nacp

$(OUTPUT).elf	:	$(OFILES)

$(OFILES_SRC)	:	$(HFILES_BIN)

# Le code tiers (ImGui) n'est pas soumis à nos warnings stricts.
$(addsuffix .o,$(basename $(IMGUI_SRC))) : CXXFLAGS += -w

#---------------------------------------------------------------------------------
# Données binaires embarquées (data/*.bin -> symboles + header)
#---------------------------------------------------------------------------------
%.bin.o	%_bin.h :	%.bin
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
