.SUFFIXES:
.DEFAULT_GOAL := all
ifneq ($(MAKECMDGOALS),check)
ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO is not set; export DEVKITPRO=/opt/devkitpro)
endif
TOPDIR ?= $(CURDIR)
include $(DEVKITPRO)/libnx/switch_rules
endif

TARGET := granny_nx
BUILD := .cache/build
SOURCES := source
INCLUDES := include
ROMFS := romfs
APP_TITLE := Granny
APP_AUTHOR := xFlippy
APP_VERSION := 1.0.2
export SOURCE_DATE_EPOCH ?= 1792022400
ARCH := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
CFLAGS := -g -Oz -flto -Wall -Wextra -Wno-unused-function -ffunction-sections -fdata-sections $(ARCH) -DGRNYX_HAVE_VORBIS
CFLAGS += $(INCLUDE) -D__SWITCH__ `$(PREFIX)pkg-config --cflags freetype2`
CFLAGS += -ffile-prefix-map=$(TOPDIR)=. -fmacro-prefix-map=$(TOPDIR)=.
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS := -g $(ARCH)
LDFLAGS := -specs=$(DEVKITPRO)/libnx/switch.specs -g -Oz -flto=auto $(ARCH) -Wl,--gc-sections -Wl,-Map,$(TARGET).map
LDFLAGS += -Wl,--wrap=_malloc_r -Wl,--wrap=_free_r -Wl,--wrap=_realloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_memalign_r -Wl,--wrap=_malloc_usable_size_r -Wl,--wrap=__libnx_exception_entry
LDFLAGS += -Wl,--wrap=_write_r
LIBS := -Wl,--start-group -lEGL -lGLESv2 -lglapi -ldrm_nouveau -lvorbisfile -lvorbis -logg `$(PREFIX)pkg-config --libs freetype2` -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -lz -Wl,--end-group -lstdc++ -lnx -lm
LIBDIRS := $(TOPDIR)/.cache/mesa-switch $(TOPDIR)/.cache/curl-switch $(PORTLIBS) $(LIBNX)

ifneq ($(notdir $(BUILD)),$(notdir $(CURDIR)))
export OUTPUT := $(CURDIR)/$(BUILD)/$(TARGET)
export TOPDIR := $(CURDIR)
export VPATH := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)
CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
export LD := $(CC)
export OFILES := $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(SFILES:.s=.o)
export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) $(foreach dir,$(LIBDIRS),-I$(dir)/include) -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)
export NROFLAGS := --icon=$(CURDIR)/icon.jpg --nacp=$(CURDIR)/$(BUILD)/$(TARGET).nacp --romfsdir=$(CURDIR)/$(ROMFS)
export ROMFS_FILES := $(shell find $(CURDIR)/$(ROMFS) -type f)

.PHONY: all clean dependencies check package
all: dependencies

dependencies:
	@bash tools/mesa/build_switch.sh
	+@bash tools/libcurl/build_switch.sh
	@mkdir -p $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile
	@python3 tools/check.py --nro $(OUTPUT).nro
	@cp $(OUTPUT).nro $(CURDIR)/$(TARGET).nro

check:
	@python3 tools/check.py

package: all
	@python3 tools/check.py --nro $(TARGET).nro

clean:
	rm -rf $(BUILD) $(TARGET).nro
else
LDFLAGS += -Wl,--build-id=0x$(shell python3 $(TOPDIR)/tools/build_id.py)
DEPENDS := $(OFILES:.o=.d)
.PHONY: all
all: $(OUTPUT).nro
$(OUTPUT).nro: $(OUTPUT).elf $(OUTPUT).nacp $(ROMFS_FILES) $(TOPDIR)/Makefile $(TOPDIR)/icon.jpg
$(OUTPUT).nacp: $(TOPDIR)/Makefile
$(OUTPUT).elf: $(OFILES) $(TOPDIR)/.cache/mesa-switch/lib/libEGL.a $(TOPDIR)/.cache/mesa-switch/lib/libGLESv2.a $(TOPDIR)/.cache/mesa-switch/lib/libglapi.a $(TOPDIR)/.cache/curl-switch/lib/libcurl.a $(TOPDIR)/tools/build_id.py
$(OFILES): $(TOPDIR)/Makefile
runtime_java.o: $(DEPSDIR)/android_api.inc
runtime_assets.o: $(DEPSDIR)/framework_resources_data.inc
runtime_assets.o: $(DEPSDIR)/core_library_dex.inc $(DEPSDIR)/aosp_libcore_dex.inc
$(DEPSDIR)/android_api.inc: $(TOPDIR)/assets/android_api.txt $(TOPDIR)/tools/embed_data.py
	python3 $(TOPDIR)/tools/embed_data.py $< $@ --symbol android_api
$(DEPSDIR)/framework_resources_data.inc: $(TOPDIR)/assets/framework.arsc $(TOPDIR)/tools/embed_data.py
	python3 $(TOPDIR)/tools/embed_data.py $< $@ --symbol framework_table
$(DEPSDIR)/core_library_dex.inc: $(TOPDIR)/assets/core_library_dex.dex $(TOPDIR)/tools/embed_data.py $(TOPDIR)/Makefile
	python3 $(TOPDIR)/tools/embed_data.py $< $@ --symbol grnyx_core_library_dex
$(DEPSDIR)/aosp_libcore_dex.inc: $(TOPDIR)/assets/aosp_libcore_dex.dex $(TOPDIR)/tools/embed_data.py $(TOPDIR)/Makefile
	python3 $(TOPDIR)/tools/embed_data.py $< $@ --symbol grnyx_aosp_libcore_dex
runtime_database.o: CFLAGS += -w
-include $(DEPENDS)
endif
