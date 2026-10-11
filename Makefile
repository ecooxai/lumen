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
  DEPS := openssl@3 brotli zstd freetype harfbuzz sdl3 v8 wgpu-native ffmpeg libpng jpeg-turbo webp giflib icu4c
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
BASELIBS := $(LIBDIRS) -licuuc
NETLIBS := $(BASELIBS) -lssl -lcrypto -lz -lbrotlidec -lzstd

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
	$(CC) $(OPT) $^ -o $@ $(BASELIBS)

$(BUILD)/test_download: $(call OBJ,tests/test_download.c src/net/download.c)
	$(CC) $(OPT) $^ -o $@

test: $(BUILD)/test_url $(BUILD)/test_download
	$(BUILD)/test_url
	$(BUILD)/test_download

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
	$(CC) $(OPT) $^ -o $@ $(BASELIBS) -lfreetype -lharfbuzz
LAYOUT_TEST_SRC := $(CSS_TEST_SRC) src/text/font.c $(wildcard src/layout/*.c)
$(BUILD)/layout: $(call OBJ,tests/layout.c $(LAYOUT_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS) -lfreetype -lharfbuzz
RENDER_TEST_SRC := $(LAYOUT_TEST_SRC) $(wildcard src/paint/*.c)
$(BUILD)/render: $(call OBJ,tests/render.c $(RENDER_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS) -lfreetype -lharfbuzz -lpng -ljpeg -lwebp -lgif

$(BUILD)/src/app/icon.inc: assets/icon/lumen-256.png
	@mkdir -p $(dir $@)
	xxd -i < $< > $@
$(BUILD)/src/app/main.c.o: $(BUILD)/src/app/icon.inc
$(BUILD)/src/app/main.c.o: CFLAGS += -I$(BUILD)/src/app

PRELUDE_JS := $(sort $(wildcard src/js/prelude/*.js))
$(BUILD)/src/js/prelude.inc: $(PRELUDE_JS)
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $(PRELUDE_JS); printf ')JS"'; } > $@
$(BUILD)/src/js/js.cc.o: $(BUILD)/src/js/prelude.inc
$(BUILD)/src/js/js.cc.o: CXXFLAGS += -I$(BUILD)/src/js
$(BUILD)/jsrun: $(call OBJ,tests/jsrun.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $^ -o $@ $(LIBS)

# ---- Android: same engine sources, linked into liblumen.so loaded by SDL's Java activity ----
# make android ABI=x86_64|arm64-v8a  (deps: android/build-deps.sh, V8: ~/src/v8/build.sh)
ANDROID_NDK ?= $(HOME)/android-sdk/ndk/28.2.13676358
ABI ?= x86_64
ANDROID_API ?= 26
ADEPS ?= $(HOME)/android-deps/$(ABI)
V8SRC ?= $(HOME)/src/v8/v8
ifeq ($(ABI),arm64-v8a)
  ATRIPLE := aarch64-linux-android
  V8CPU := arm64
else
  ATRIPLE := x86_64-linux-android
  V8CPU := x64
endif
V8OUT ?= $(V8SRC)/out/android-$(V8CPU)
ATC := $(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin
ABUILD := build-android/$(ABI)
AINC := -Isrc -I$(ADEPS)/include -I$(ADEPS)/include/freetype2 -I$(ADEPS)/include/harfbuzz -I$(V8SRC)/include -I$(V8SRC)/third_party/icu/source/common
AOPT := -O2 -g -DNDEBUG -fPIC -ffunction-sections -fdata-sections
ACFLAGS := -std=c11 $(AOPT) $(WARN) $(AINC) -D_GNU_SOURCE 
ACXXFLAGS := -std=c++20 $(AOPT) $(WARN) $(AINC) -DV8_COMPRESS_POINTERS -DV8_31BIT_SMIS_ON_64BIT_ARCH
ACC := $(ATC)/$(ATRIPLE)$(ANDROID_API)-clang
ACXX := $(ATC)/$(ATRIPLE)$(ANDROID_API)-clang++
AOBJ = $(patsubst %,$(ABUILD)/%.o,$(1))
ALIBS := -L$(ADEPS)/lib -lSDL3 -lfreetype -lharfbuzz -lavformat -lavcodec -lswscale -lswresample -lavutil \
         -lpng16 -ljpeg -lwebpdemux -lwebp -lsharpyuv -lgif -lssl -lcrypto -lbrotlidec -lbrotlicommon -lzstd \
         $(V8OUT)/obj/libv8_monolith.a -lwgpu_native -lz -lm -ldl -llog -landroid -lEGL -lGLESv2 -lOpenSLES -laaudio -lmediandk -lcamera2ndk
AJNILIBS := android/app/src/main/jniLibs/$(ABI)

$(ABUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(ACC) $(ACFLAGS) -MMD -c $< -o $@
$(ABUILD)/%.cc.o: %.cc
	@mkdir -p $(dir $@)
	$(ACXX) $(ACXXFLAGS) -MMD -c $< -o $@
$(ABUILD)/liblumen.so: $(call AOBJ,$(CORE_SRC) $(APP_SRC) $(JS_SRC) $(wildcard src/platform/android/*.c))
	$(ACXX) -shared -Wl,--gc-sections -Wl,-z,max-page-size=16384 -Wl,--no-undefined $^ -o $@ $(ALIBS) -static-libstdc++
android: $(ABUILD)/liblumen.so
	@mkdir -p $(AJNILIBS)
	cp $(ABUILD)/liblumen.so $(ADEPS)/lib/libwgpu_native.so $(AJNILIBS)/
	$(ATC)/llvm-strip --strip-debug $(AJNILIBS)/liblumen.so
apk: android
	cd android && ./gradlew --no-daemon assembleDebug
.PHONY: android apk
$(ABUILD)/src/app/main.c.o: $(BUILD)/src/app/icon.inc
$(ABUILD)/src/app/main.c.o: ACFLAGS += -I$(BUILD)/src/app
$(ABUILD)/src/js/js.cc.o: $(BUILD)/src/js/prelude.inc
$(ABUILD)/src/js/js.cc.o: ACXXFLAGS += -I$(BUILD)/src/js
