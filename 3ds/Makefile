#---------------------------------------------------------------------------------
# Aeroverse for Nintendo 3DS (devkitARM + libctru, builds aeroverse.3dsx)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif

TOPDIR ?= $(CURDIR)
include $(DEVKITARM)/3ds_rules

TARGET		:=	aeroverse
BUILD		:=	build
SOURCES		:=	source source/webp/src/dec source/webp/src/dsp source/webp/src/utils
DATA		:=	data
INCLUDES	:=	include source/webp source/webp/src

APP_TITLE	:=	Aeroverse
APP_DESCRIPTION	:=	Aeroverse for 3DS
APP_AUTHOR	:=	Luk
ICON		:=	icon.png

ARCH	:=	-march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS	:=	-g -Wall -O2 -mword-relocations -ffunction-sections -Wno-unused-function -Wno-format-truncation -Wno-misleading-indentation $(ARCH)
CFLAGS	+=	$(INCLUDE) -D__3DS__

CXXFLAGS	:= $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++11
ASFLAGS	:=	-g $(ARCH)
LDFLAGS	=	-specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

# curl + mbedTLS from the 3DS portlibs; the WebP decoder (avatars) is built from source/webp
HAVE_LIB = $(if $(wildcard $(foreach d,$(PORTLIBS),$(d)/lib/lib$(1).a)),-l$(1))
CFLAGS	+= -DHAVE_WEBP
OPT_LIBS	:= $(foreach l,nghttp2 zstd brotlidec brotlicommon,$(call HAVE_LIB,$(l)))
LIBS	:= -Wl,--start-group $(IMG_LIBS) -lcurl $(OPT_LIBS) -lmbedtls -lmbedx509 -lmbedcrypto -lz -Wl,--end-group -lctru -lm

LIBDIRS	:= $(PORTLIBS) $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export TOPDIR	:=	$(CURDIR)

export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
			$(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR	:=	$(CURDIR)/$(BUILD)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
BINFILES	:=	$(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

export LD	:=	$(CC)

export OFILES_BIN	:=	$(addsuffix .o,$(BINFILES))
export OFILES_SOURCES	:=	$(CFILES:.c=.o)
export OFILES	:=	$(OFILES_BIN) $(OFILES_SOURCES)
export HFILES	:=	$(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
			$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
			-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export APP_ICON := $(TOPDIR)/$(ICON)
export _3DSXDEPS := $(OUTPUT).smdh
export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: all clean

all: $(BUILD)

$(BUILD):
	@mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(OUTPUT).smdh $(TARGET).elf

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

all: $(OUTPUT).3dsx

$(OUTPUT).3dsx	:	$(OUTPUT).elf $(_3DSXDEPS)

$(OFILES_SOURCES) : $(HFILES)

$(OUTPUT).elf	:	$(OFILES)

%.ttf.o	%_ttf.h :	%.ttf
	@echo $(notdir $<)
	@$(bin2o)

%.pem.o	%_pem.h :	%.pem
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPSDIR)/*.d

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
