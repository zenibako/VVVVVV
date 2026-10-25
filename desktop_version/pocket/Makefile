# VVVVVV for openfpgaOS (Analogue Pocket).
#
#   make deps        fetch the pinned SDK and SDL3 headers into .deps
#   make             build build/app.elf (in the SDK container by default;
#                    USE_SDK_CONTAINER=0 uses a host riscv64-unknown-elf gcc)
#   make package     assemble the SD card tree in build/sd (no game data)
#   make test        build and run the host checks for the compat headers
#   make host        build the game for this computer with the same compat
#                    headers, on desktop SDL3 (SDL3_PREFIX), to debug the
#                    game logic off the device
#   make host-render the host build drawn by the port's renderer
#   make render-test compare the port's renderer with desktop SDL3's
#                    software renderer (SDL3_PREFIX)
#
# The game sources build unchanged from ../src with the C++98 flags the
# desktop CMake build uses.  The SDK has no C++ standard library, so
# compat/cxx supplies the small part of it the game needs, and sdl3/
# implements the SDL3 calls the game makes on top of the of_* API.

APP          := vvvvvv
POCKET_ROOT  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
GAME_ROOT    := $(abspath $(POCKET_ROOT)/..)
THIRD_PARTY  := $(abspath $(GAME_ROOT)/../third_party)
SDK_ROOT     ?= $(POCKET_ROOT)/.deps/openfpgaos-sdk
SDL3_ROOT    ?= $(POCKET_ROOT)/.deps/sdl3
SDK_DIR      := $(SDK_ROOT)/src/sdk
OBJ_DIR      := $(POCKET_ROOT)/build/obj
BUILD_DIR    := $(POCKET_ROOT)/build
SDK_CONTAINER := $(POCKET_ROOT)/scripts/sdk-container.sh

GAME_CXX_SRCS := $(addprefix $(GAME_ROOT)/src/, \
	BinaryBlob.cpp BlockV.cpp ButtonGlyphs.cpp CustomLevels.cpp CWrappers.cpp \
	Editor.cpp Ent.cpp Entity.cpp FileSystemUtils.cpp Finalclass.cpp Font.cpp \
	FontBidi.cpp Game.cpp Graphics.cpp GraphicsResources.cpp GraphicsUtil.cpp \
	IMERender.cpp Input.cpp KeyPoll.cpp Labclass.cpp LevelDebugger.cpp \
	Localization.cpp LocalizationMaint.cpp LocalizationStorage.cpp Logic.cpp \
	Map.cpp Music.cpp Otherlevel.cpp preloader.cpp Render.cpp RenderFixed.cpp \
	RoomnameTranslator.cpp Screen.cpp Script.cpp Scripts.cpp Spacestation2.cpp \
	TerminalScripts.cpp Textbox.cpp Tower.cpp UtilityClass.cpp WarpClass.cpp \
	XMLUtils.cpp main.cpp)

# Network.c without STEAM_NETWORK/GOG_NETWORK compiles to stubs.
GAME_C_SRCS := $(addprefix $(GAME_ROOT)/src/, \
	DeferCallbacks.c GlitchrunnerMode.c Network.c Textbook.c ThirdPartyDeps.c \
	UTF8.c VFormat.c Vlogging.c Xoshiro.c lodepng_wrapper.c)

PHYSFS_SRCS := $(addprefix $(THIRD_PARTY)/physfs/, \
	extras/physfssdl3.c src/physfs.c src/physfs_archiver_dir.c \
	src/physfs_archiver_unpacked.c src/physfs_archiver_zip.c \
	src/physfs_byteorder.c src/physfs_unicode.c src/physfs_platform_sdl3.c)

DEP_C_SRCS := $(PHYSFS_SRCS) \
	$(THIRD_PARTY)/c-hashmap/map.c \
	$(THIRD_PARTY)/SheenBidi/Source/SheenBidi.c

POCKET_C_SRCS := $(wildcard $(POCKET_ROOT)/src/*.c $(POCKET_ROOT)/sdl3/*.c)
POCKET_CXX_SRCS := $(POCKET_ROOT)/compat/cxx/compat.cpp $(wildcard $(POCKET_ROOT)/src/*.cpp)

SRCS := $(GAME_C_SRCS) $(DEP_C_SRCS) $(POCKET_C_SRCS)
SRCS_CXX := $(GAME_CXX_SRCS) $(THIRD_PARTY)/tinyxml2/tinyxml2.cpp \
	$(POCKET_CXX_SRCS) $(SDK_DIR)/of_cxxabi.cpp

# g++ predefines _GNU_SOURCE on Linux, which the game relies on (PATH_MAX).
POCKET_DEFS := -DVVV_POCKET -D_GNU_SOURCE \
	-DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK \
	-DPHYSFS_SUPPORTS_DEFAULT=0 -DPHYSFS_SUPPORTS_ZIP=1 \
	-DPHYSFS_PLATFORM_SDL3=1 \
	-DSB_CONFIG_UNITY
# SHOW_FPS=1 draws the presented frame rate in the top right corner.
ifeq ($(SHOW_FPS),1)
POCKET_DEFS += -DPOCKET_SHOW_FPS
endif
# BENCH=1 builds the scripted benchmark (src/pocket_bench.h). It times
# frames itself, without the profile above and its cost.
ifeq ($(BENCH),1)
POCKET_DEFS += -DPOCKET_BENCH
endif
POCKET_INCLUDES := -I$(POCKET_ROOT)/src -I$(SDL3_ROOT)/include \
	-I$(THIRD_PARTY)/tinyxml2 -I$(THIRD_PARTY)/physfs/src \
	-I$(THIRD_PARTY)/physfs/extras -I$(THIRD_PARTY)/lodepng \
	-I$(THIRD_PARTY) -I$(THIRD_PARTY)/SheenBidi/Headers

CFLAGS += -std=gnu99 $(POCKET_DEFS) $(POCKET_INCLUDES)
CXXFLAGS += -std=c++98 -nostdinc++ -I$(POCKET_ROOT)/compat/cxx $(POCKET_DEFS) $(POCKET_INCLUDES)

# The port implements SDL3 itself; the SDK's SDL2 shim defines many of the
# same names, so build an empty object in its place.
override OF_SDL2_SRC := $(POCKET_ROOT)/sdl3/no_sdl2.c

.DEFAULT_GOAL := all

# deps, test and host work before the SDK has been fetched.
ifneq ($(wildcard $(SDK_DIR)/sdk.mk),)
include $(SDK_DIR)/sdk.mk

# Profiling's shared layout is used by C and C++ translation units.
$(foreach src,$(SRCS) $(SRCS_CXX),$(call sdk_app_object,$(src))): $(POCKET_ROOT)/src/pocket_bench_profile.h

# The SDK's C++ ABI shim is C++11.
$(call sdk_app_object,$(SDK_DIR)/of_cxxabi.cpp): CXXFLAGS += -std=gnu++17
else
$(BUILD_DIR)/app.elf:
	@echo "The openfpgaOS SDK is missing; run 'make deps' first." >&2
	@exit 1
endif

all: $(BUILD_DIR)/app.elf
	@$(SIZE) $< 2>/dev/null || true

deps:
	bash $(POCKET_ROOT)/scripts/fetch-deps.sh

# The SD card tree in build/sd, without game data.
package: $(BUILD_DIR)/app.elf
	BENCH=$(BENCH) bash $(POCKET_ROOT)/scripts/package.sh

# The game's button fonts with Pocket glyphs added; the output is committed.
button-fonts:
	python3 $(POCKET_ROOT)/scripts/make-button-fonts.py $(GAME_ROOT)/fonts $(POCKET_ROOT)/fonts

TEST_BIN := $(BUILD_DIR)/host/compat_test
SAVEFS_TEST_BIN := $(BUILD_DIR)/host/savefs_test
AUDIO_TEST_BIN := $(BUILD_DIR)/host/audio_test
BENCH_STARTUP_TEST_BIN := $(BUILD_DIR)/host/bench_startup_test
BENCH_SAVEFS_TEST_BIN := $(BUILD_DIR)/host/bench_savefs_test
BENCH_REPORT_TEST_BIN := $(BUILD_DIR)/host/bench_report_test
BENCH_PROFILE_TEST_BIN := $(BUILD_DIR)/host/bench_profile_test
BENCH_PROFILE_C_TEST_BIN := $(BUILD_DIR)/host/bench_profile_c_test
test: $(TEST_BIN) $(SAVEFS_TEST_BIN) $(AUDIO_TEST_BIN) $(BENCH_STARTUP_TEST_BIN) $(BENCH_SAVEFS_TEST_BIN) $(BENCH_REPORT_TEST_BIN) $(BENCH_PROFILE_TEST_BIN) $(BENCH_PROFILE_C_TEST_BIN)
	$(TEST_BIN)
	$(SAVEFS_TEST_BIN)
	$(AUDIO_TEST_BIN)
	$(BENCH_STARTUP_TEST_BIN)
	$(BENCH_SAVEFS_TEST_BIN)
	$(BENCH_REPORT_TEST_BIN)
	$(BENCH_PROFILE_TEST_BIN)
	$(BENCH_PROFILE_C_TEST_BIN)

$(BENCH_PROFILE_C_TEST_BIN): $(POCKET_ROOT)/tests/bench_profile_c_test.c $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -Wall -Wextra -g $(HOST_SANITIZE) -DPOCKET_BENCH -DOF_PC \
		-I$(SDK_DIR)/include $< -o $@

$(BENCH_PROFILE_TEST_BIN): $(POCKET_ROOT)/tests/bench_profile_test.cpp $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++98 -Wall -Wextra -g $(HOST_SANITIZE) -DPOCKET_BENCH -DOF_PC \
		-I$(SDK_DIR)/include $< -o $@

$(BENCH_REPORT_TEST_BIN): $(POCKET_ROOT)/tests/bench_report_test.cpp $(POCKET_ROOT)/src/pocket_bench_report.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++98 -Wall -Wextra -g $(HOST_SANITIZE) $< -o $@

$(BENCH_SAVEFS_TEST_BIN): $(POCKET_ROOT)/tests/bench_savefs_test.c \
		$(addprefix $(POCKET_ROOT)/sdl3/, savefs.c savefs.h io.c stdinc.c)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -Wall -Wextra -g $(HOST_SANITIZE) -D_GNU_SOURCE -DPOCKET_BENCH \
		-DPOCKET_SAVE_FILE='"$(BUILD_DIR)/host/bench_savefs_test.sav"' \
		-I$(SDL3_ROOT)/include -I$(POCKET_ROOT)/sdl3 \
		$(POCKET_ROOT)/tests/bench_savefs_test.c $(POCKET_ROOT)/sdl3/io.c \
		$(POCKET_ROOT)/sdl3/stdinc.c -lm -o $@

$(BENCH_STARTUP_TEST_BIN): $(POCKET_ROOT)/tests/bench_startup_test.cpp \
		$(POCKET_ROOT)/src/pocket_bench_startup.cpp $(POCKET_ROOT)/src/pocket_bench.h \
		$(GAME_ROOT)/src/Localization.h $(wildcard $(POCKET_ROOT)/compat/cxx/*)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++98 -nostdinc++ -fno-exceptions -fno-rtti -Wall -Wextra -g \
		$(HOST_SANITIZE) -DPOCKET_BENCH -I$(POCKET_ROOT)/compat/cxx \
		$(POCKET_ROOT)/tests/bench_startup_test.cpp $(POCKET_ROOT)/src/pocket_bench_startup.cpp \
		$(POCKET_ROOT)/compat/cxx/compat.cpp -o $@

# The sound player against a stand-in for the OS audio output.
$(AUDIO_TEST_BIN): $(POCKET_ROOT)/tests/audio_test.c $(POCKET_ROOT)/tests/stub/of_audio.h \
		$(addprefix $(POCKET_ROOT)/src/, pocket_audio.c pocket_audio.h music_pack.h) \
		$(addprefix $(POCKET_ROOT)/sdl3/, io.c savefs.c savefs.h stdinc.c)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -Wall -Wextra -g $(HOST_SANITIZE) -D_GNU_SOURCE \
		-DPOCKET_MUSIC_PACK='"$(BUILD_DIR)/host/audio_test.bin"' \
		-DPOCKET_SAVE_FILE='"$(BUILD_DIR)/host/audio_test.sav"' \
		-I$(POCKET_ROOT)/tests/stub -I$(POCKET_ROOT)/src -I$(SDL3_ROOT)/include -I$(POCKET_ROOT)/sdl3 \
		$(POCKET_ROOT)/tests/audio_test.c $(POCKET_ROOT)/src/pocket_audio.c \
		$(addprefix $(POCKET_ROOT)/sdl3/, io.c savefs.c stdinc.c) -lm -o $@

# The save archive runs against the port's own SDL3 I/O layer.
$(SAVEFS_TEST_BIN): $(POCKET_ROOT)/tests/savefs_test.c $(addprefix $(POCKET_ROOT)/sdl3/, savefs.c savefs.h io.c stdinc.c)
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -Wall -Wextra -g $(HOST_SANITIZE) -D_GNU_SOURCE \
		-DPOCKET_SAVE_FILE='"$(BUILD_DIR)/host/savefs_test.sav"' \
		-I$(SDL3_ROOT)/include -I$(POCKET_ROOT)/sdl3 \
		$(POCKET_ROOT)/tests/savefs_test.c $(POCKET_ROOT)/sdl3/io.c $(POCKET_ROOT)/sdl3/stdinc.c -lm -o $@

$(TEST_BIN): $(POCKET_ROOT)/tests/compat_test.cpp $(POCKET_ROOT)/compat/cxx/compat.cpp $(wildcard $(POCKET_ROOT)/compat/cxx/*)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++98 -nostdinc++ -fno-exceptions -fno-rtti -Wall -Wextra -g \
		$(HOST_SANITIZE) -I$(POCKET_ROOT)/compat/cxx \
		$(POCKET_ROOT)/tests/compat_test.cpp $(POCKET_ROOT)/compat/cxx/compat.cpp -o $@

HOST_CC ?= cc
HOST_CXX ?= c++
HOST_SANITIZE ?= -fsanitize=address,undefined

# Host build: same game sources and compat headers, desktop SDL3 and FAudio.
SDL3_PREFIX ?= /usr/local
HOST_DIR := $(BUILD_DIR)/host
HOST_BIN := $(HOST_DIR)/vvvvvv
# PhysFS uses its SDL3 platform backend here too, which works the same on
# Linux and macOS.
HOST_C_SRCS := $(GAME_C_SRCS) $(PHYSFS_SRCS) \
	$(THIRD_PARTY)/c-hashmap/map.c $(THIRD_PARTY)/SheenBidi/Source/SheenBidi.c \
	$(addprefix $(THIRD_PARTY)/FAudio/src/, FAudio.c FAudio_internal.c \
		FAudio_internal_simd.c FAudio_operationset.c FAudio_platform_sdl3.c)
HOST_CXX_SRCS := $(GAME_CXX_SRCS) $(THIRD_PARTY)/tinyxml2/tinyxml2.cpp \
	$(POCKET_ROOT)/compat/cxx/compat.cpp
HOST_DEFS := -D_GNU_SOURCE -DLODEPNG_NO_COMPILE_ALLOCATORS -DLODEPNG_NO_COMPILE_DISK \
	-DPHYSFS_SUPPORTS_DEFAULT=0 -DPHYSFS_SUPPORTS_ZIP=1 -DPHYSFS_PLATFORM_SDL3=1 \
	-DSB_CONFIG_UNITY -DFAUDIO_SDL3_PLATFORM
HOST_INCLUDES := -I$(SDL3_PREFIX)/include $(filter-out -I$(SDL3_ROOT)/include,$(POCKET_INCLUDES)) \
	-I$(THIRD_PARTY)/FAudio/include -I$(THIRD_PARTY)/FAudio/src
# Opt-in accelerated host check of the real benchmark route and game logic.
# Run with private assets and a private -basedir; these timings are synthetic.
ifeq ($(HOST_BENCH_SMOKE),1)
HOST_DEFS += -DPOCKET_BENCH -DOF_PC
HOST_INCLUDES += -I$(SDK_DIR)/include
HOST_CXX_SRCS += $(POCKET_ROOT)/src/pocket_bench_startup.cpp $(POCKET_ROOT)/tests/bench_smoke.cpp
endif
host_object = $(HOST_DIR)/obj/$(subst ../,__parent__/,$(basename $(1))).o
HOST_OBJS := $(foreach src,$(HOST_C_SRCS) $(HOST_CXX_SRCS),$(call host_object,$(src)))
ifeq ($(HOST_BENCH_SMOKE),1)
# The font timer is part of the Pocket cache, which also runs on host SDL.
$(call host_object,$(GAME_ROOT)/src/Font.cpp): HOST_DEFS += -DVVV_POCKET
$(call host_object,$(POCKET_ROOT)/tests/bench_smoke.cpp): \
	$(POCKET_ROOT)/src/pocket_bench.cpp $(POCKET_ROOT)/src/pocket_bench.h $(POCKET_ROOT)/src/pocket_bench_report.h \
	$(POCKET_ROOT)/src/pocket_bench_profile.h $(POCKET_ROOT)/tests/clock_pixels.h $(POCKET_ROOT)/tests/background_pixels.h $(GAME_ROOT)/src/Font.h
endif

define HOST_C_RULE
$(call host_object,$(1)): $(1) $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $$(dir $$@)
	$$(HOST_CC) -std=gnu99 -g -O1 $$(HOST_SANITIZE) $$(HOST_DEFS) $$(HOST_INCLUDES) -c -o $$@ $$<
endef
$(foreach src,$(HOST_C_SRCS),$(eval $(call HOST_C_RULE,$(src))))

define HOST_CXX_RULE
$(call host_object,$(1)): $(1) $(wildcard $(POCKET_ROOT)/compat/cxx/*) $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $$(dir $$@)
	$$(HOST_CXX) -std=c++98 -nostdinc++ -I$(POCKET_ROOT)/compat/cxx -fno-exceptions -fno-rtti \
		-g -O1 $$(HOST_SANITIZE) $$(HOST_DEFS) $$(HOST_INCLUDES) -c -o $$@ $$<
endef
$(foreach src,$(HOST_CXX_SRCS),$(eval $(call HOST_CXX_RULE,$(src))))

# Linking with the C driver and only libsupc++ (the ABI support that
# of_cxxabi.cpp stands in for on the device) keeps libstdc++ out.
ifeq ($(shell uname -s),Darwin)
# Apple's toolchain has libc++abi in place of libsupc++, and links SDL3
# as the shared library Homebrew installs.
HOST_LIBS ?= -L$(SDL3_PREFIX)/lib -Wl,-rpath,$(SDL3_PREFIX)/lib -lSDL3 -lc++abi -lm
else
HOST_LIBS ?= $(SDL3_PREFIX)/lib/libSDL3.a -lsupc++ -lm -lpthread -ldl
endif

$(HOST_BIN): $(HOST_OBJS)
	$(HOST_CC) $(HOST_SANITIZE) -o $@ $^ $(HOST_LIBS)

host: $(HOST_BIN)

# The renderer against desktop SDL3's software renderer (tests/render_test.c).
# The port's renderer is built with its SDL calls given a PR_ prefix so both
# link into one program.
RENDER_TEST_BIN := $(HOST_DIR)/render_test
RENDER_SRCS := $(addprefix $(POCKET_ROOT)/sdl3/, render.c surface.c)
RENDER_RENAME := $(HOST_DIR)/render_rename.h
RENDER_TEST_CFLAGS = -std=gnu99 -Wall -Wextra -g $(HOST_SANITIZE) \
	-I$(SDL3_PREFIX)/include -I$(POCKET_ROOT)/tests -I$(POCKET_ROOT)/sdl3

$(RENDER_RENAME): $(RENDER_SRCS)
	@mkdir -p $(dir $@)
	sed -nE 's/^[A-Za-z].*[ *](SDL_[A-Za-z]+)\(.*/#define \1 PR_\1/p' $^ | sort -u > $@

$(RENDER_TEST_BIN): $(addprefix $(POCKET_ROOT)/tests/, render_test.c render_ops.c render_test.h) \
		$(RENDER_SRCS) $(POCKET_ROOT)/sdl3/render_internal.h $(RENDER_RENAME)
	@mkdir -p $(HOST_DIR)/render_test_obj
	$(HOST_CC) $(RENDER_TEST_CFLAGS) -DRENDER_OPS=render_ops_sdl \
		-c $(POCKET_ROOT)/tests/render_ops.c -o $(HOST_DIR)/render_test_obj/ops_sdl.o
	$(HOST_CC) $(RENDER_TEST_CFLAGS) -DRENDER_OPS=render_ops_pocket -include $(RENDER_RENAME) \
		-c $(POCKET_ROOT)/tests/render_ops.c -o $(HOST_DIR)/render_test_obj/ops_pocket.o
	$(HOST_CC) $(RENDER_TEST_CFLAGS) -include $(RENDER_RENAME) \
		-c $(POCKET_ROOT)/sdl3/render.c -o $(HOST_DIR)/render_test_obj/render.o
	$(HOST_CC) $(RENDER_TEST_CFLAGS) -include $(RENDER_RENAME) \
		-c $(POCKET_ROOT)/sdl3/surface.c -o $(HOST_DIR)/render_test_obj/surface.o
	$(HOST_CC) $(RENDER_TEST_CFLAGS) -c $(POCKET_ROOT)/tests/render_test.c -o $(HOST_DIR)/render_test_obj/render_test.o
	$(HOST_CC) $(HOST_SANITIZE) -o $@ $(addprefix $(HOST_DIR)/render_test_obj/, \
		render_test.o ops_sdl.o ops_pocket.o render.o surface.o) $(HOST_LIBS)

render-test: $(RENDER_TEST_BIN)
	$(RENDER_TEST_BIN)

# The host game drawn by the port's renderer instead of desktop SDL3's, to
# compare whole frames with `make host`; host/present.c shows them.
HOST_RENDER_DIR := $(HOST_DIR)/render
HOST_RENDER_BIN := $(HOST_RENDER_DIR)/vvvvvv
host_render_object = $(HOST_RENDER_DIR)/obj/$(subst ../,__parent__/,$(basename $(1))).o
HOST_RENDER_C_SRCS := $(HOST_C_SRCS) $(RENDER_SRCS)
HOST_RENDER_OBJS := $(foreach src,$(HOST_RENDER_C_SRCS) $(HOST_CXX_SRCS),$(call host_render_object,$(src)))
ifeq ($(HOST_BENCH_SMOKE),1)
$(call host_render_object,$(GAME_ROOT)/src/Font.cpp): HOST_DEFS += -DVVV_POCKET
$(call host_render_object,$(GAME_ROOT)/src/Graphics.cpp): HOST_DEFS += -DVVV_POCKET
$(call host_render_object,$(POCKET_ROOT)/tests/bench_smoke.cpp): HOST_DEFS += -DPOCKET_SMOKE_RENDER
$(call host_render_object,$(POCKET_ROOT)/tests/bench_smoke.cpp): \
	$(POCKET_ROOT)/src/pocket_bench.cpp $(POCKET_ROOT)/src/pocket_bench.h $(POCKET_ROOT)/src/pocket_bench_report.h \
	$(POCKET_ROOT)/src/pocket_bench_profile.h $(POCKET_ROOT)/tests/clock_pixels.h $(POCKET_ROOT)/tests/background_pixels.h $(GAME_ROOT)/src/Font.h
else
HOST_RENDER_OBJS += $(HOST_RENDER_DIR)/obj/present.o
endif
HOST_RENDER_FLAGS = -include $(RENDER_RENAME) -I$(POCKET_ROOT)/sdl3

define HOST_RENDER_C_RULE
$(call host_render_object,$(1)): $(1) $(RENDER_RENAME) $(POCKET_ROOT)/sdl3/render_internal.h $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $$(dir $$@)
	$$(HOST_CC) -std=gnu99 -g -O1 $$(HOST_SANITIZE) $$(HOST_DEFS) $$(HOST_RENDER_FLAGS) $$(HOST_INCLUDES) -c -o $$@ $$<
endef
$(foreach src,$(HOST_RENDER_C_SRCS),$(eval $(call HOST_RENDER_C_RULE,$(src))))

define HOST_RENDER_CXX_RULE
$(call host_render_object,$(1)): $(1) $(RENDER_RENAME) $(wildcard $(POCKET_ROOT)/compat/cxx/*) $(POCKET_ROOT)/src/pocket_bench_profile.h
	@mkdir -p $$(dir $$@)
	$$(HOST_CXX) -std=c++98 -nostdinc++ -I$(POCKET_ROOT)/compat/cxx -fno-exceptions -fno-rtti \
		-g -O1 $$(HOST_SANITIZE) $$(HOST_DEFS) $$(HOST_RENDER_FLAGS) $$(HOST_INCLUDES) -c -o $$@ $$<
endef
$(foreach src,$(HOST_CXX_SRCS),$(eval $(call HOST_RENDER_CXX_RULE,$(src))))

$(HOST_RENDER_DIR)/obj/present.o: $(POCKET_ROOT)/host/present.c $(POCKET_ROOT)/sdl3/render_internal.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=gnu99 -g -O1 $(HOST_SANITIZE) -I$(SDL3_PREFIX)/include -I$(POCKET_ROOT)/sdl3 -c -o $@ $<

$(HOST_RENDER_BIN): $(HOST_RENDER_OBJS)
	$(HOST_CC) $(HOST_SANITIZE) -o $@ $^ $(HOST_LIBS)

host-render: $(HOST_RENDER_BIN)

clean: sdk-clean
	rm -rf $(BUILD_DIR)

.PHONY: all deps package button-fonts test host render-test host-render clean
