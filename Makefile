# Lumen browser build (macOS / Linux)
UNAME := $(shell uname)
BREW ?= /opt/homebrew
BUILD ?= build
MODE ?= release
CC ?= clang
CXX ?= clang++
ifeq ($(MODE),debug)
  OPT := -O0 -g -fsanitize=address,undefined
else
  OPT := -O2 -g -DNDEBUG
endif
WARN := -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
INC := -Isrc
ifeq ($(UNAME),Darwin)
  DEPS := openssl@3 brotli zstd freetype harfbuzz sdl3 v8 wgpu-native ffmpeg libpng jpeg-turbo webp giflib
  INC += $(foreach d,$(DEPS),-I$(BREW)/opt/$(d)/include) -I$(BREW)/opt/freetype/include/freetype2 -I$(BREW)/opt/harfbuzz/include/harfbuzz -I$(BREW)/include
  LIBDIRS := $(foreach d,$(DEPS),-L$(BREW)/opt/$(d)/lib)
  PLATLIBS := -framework Cocoa -framework QuartzCore -framework Metal -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework ScreenCaptureKit
else
  INC += -I/usr/include/freetype2 -I/usr/include/harfbuzz -I/usr/include/SDL3
  LIBDIRS :=
  PLATLIBS := -lpthread -ldl -lm
endif
CFLAGS := -std=c11 $(OPT) $(WARN) $(INC) -D_GNU_SOURCE -D_DARWIN_C_SOURCE
CXXFLAGS := -std=c++20 $(OPT) $(WARN) $(INC) -DV8_COMPRESS_POINTERS
NETLIBS := $(LIBDIRS) -lssl -lcrypto -lz -lbrotlidec -lzstd

BASE_SRC := $(wildcard src/base/*.c)
NET_SRC := $(wildcard src/net/*.c)
CORE_SRC := $(BASE_SRC) $(NET_SRC) $(wildcard src/html/*.c src/dom/*.c src/css/*.c src/text/*.c src/layout/*.c src/paint/*.c src/media/*.c src/gpu/*.c src/js/*.c)
APP_SRC := $(wildcard src/app/*.c)
JS_SRC := $(wildcard src/js/*.cc)
ifeq ($(UNAME),Darwin)
  APP_SRC += $(wildcard src/app/*.m)
endif
OBJ = $(patsubst %,$(BUILD)/%.o,$(1))

all: $(BUILD)/lumen

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@
$(BUILD)/%.m.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c11,$(CFLAGS)) -fobjc-arc -MMD -c $< -o $@
$(BUILD)/%.cc.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

LIBS := $(NETLIBS) -lfreetype -lharfbuzz -lSDL3 -lv8 -lv8_libplatform -lv8_libbase -lwgpu_native \
        -lavcodec -lavformat -lavutil -lswscale -lswresample -lpng -ljpeg -lwebp -lwebpdemux -lgif $(PLATLIBS)

$(BUILD)/lumen: $(call OBJ,$(CORE_SRC) $(APP_SRC) $(JS_SRC))
	$(CXX) $(OPT) $^ -o $@ $(LIBS)

$(BUILD)/fetch: $(call OBJ,tests/fetch.c $(BASE_SRC) $(NET_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)
$(BUILD)/test_url: $(call OBJ,tests/test_url.c $(BASE_SRC))
	$(CC) $(OPT) $^ -o $@

test: $(BUILD)/test_url
	$(BUILD)/test_url

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
.PHONY: all clean test
$(BUILD)/parse: $(call OBJ,tests/parse.c $(BASE_SRC) $(NET_SRC) src/dom/dom.c src/html/html.c src/html/entities.c)
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)
CSS_TEST_SRC := $(BASE_SRC) $(NET_SRC) src/dom/dom.c src/html/html.c src/html/entities.c $(wildcard src/css/*.c)
$(BUILD)/style: $(call OBJ,tests/style.c $(CSS_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)
$(BUILD)/text: $(call OBJ,tests/text.c $(BASE_SRC) src/text/font.c)
	$(CC) $(OPT) $^ -o $@ $(LIBDIRS) -lfreetype -lharfbuzz
LAYOUT_TEST_SRC := $(CSS_TEST_SRC) src/text/font.c $(wildcard src/layout/*.c)
$(BUILD)/layout: $(call OBJ,tests/layout.c $(LAYOUT_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS) -lfreetype -lharfbuzz
RENDER_TEST_SRC := $(LAYOUT_TEST_SRC) $(wildcard src/paint/*.c)
$(BUILD)/render: $(call OBJ,tests/render.c $(RENDER_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS) -lfreetype -lharfbuzz -lpng -ljpeg -lwebp -lgif

PRELUDE_JS := $(sort $(wildcard src/js/prelude/*.js))
$(BUILD)/src/js/prelude.inc: $(PRELUDE_JS)
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $(PRELUDE_JS); printf ')JS"'; } > $@
$(BUILD)/src/js/js.cc.o: $(BUILD)/src/js/prelude.inc
$(BUILD)/src/js/js.cc.o: CXXFLAGS += -I$(BUILD)/src/js
$(BUILD)/jsrun: $(call OBJ,tests/jsrun.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $^ -o $@ $(LIBS)
