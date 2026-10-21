# Lumen browser build (macOS / Linux; iOS/iPadOS: make PLAT=sim|ios|cat app, see CONTINUE-ios.md)
UNAME := $(shell uname)
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
  BREW ?= /opt/homebrew
  DEPS := openssl@3 brotli zstd freetype harfbuzz sdl3 v8 wgpu-native ffmpeg libpng jpeg-turbo webp giflib icu4c
  INC += $(foreach d,$(DEPS),-I$(BREW)/opt/$(d)/include) -I$(BREW)/opt/freetype/include/freetype2 -I$(BREW)/opt/harfbuzz/include/harfbuzz -I$(BREW)/include
  LIBDIRS := $(foreach d,$(DEPS),-L$(BREW)/opt/$(d)/lib)
  PLATLIBS := -framework Cocoa -framework QuartzCore -framework Metal -framework AVFoundation -framework CoreMedia -framework CoreVideo -framework ScreenCaptureKit
else
  # Linux: every dependency comes from Homebrew (V8/SDL3/wgpu-native bottles need its glibc + gcc runtime)
  BREW ?= /home/linuxbrew/.linuxbrew
  CC := $(BREW)/bin/gcc-16
  CXX := $(BREW)/bin/g++-16
  export PATH := $(BREW)/bin:$(PATH)
  DEPS := openssl@3 brotli zstd freetype harfbuzz sdl3 v8 wgpu-native ffmpeg libpng jpeg-turbo webp giflib icu4c fontconfig libx11
  INC += $(foreach d,$(DEPS),-I$(BREW)/opt/$(d)/include) -I$(BREW)/opt/freetype/include/freetype2 -I$(BREW)/opt/harfbuzz/include/harfbuzz -I$(BREW)/include
  LIBDIRS := -L$(BREW)/opt/v8/libexec -Wl,-rpath,$(BREW)/opt/v8/libexec $(foreach d,$(DEPS),-L$(BREW)/opt/$(d)/lib -Wl,-rpath,$(BREW)/opt/$(d)/lib) -L$(BREW)/lib -Wl,-rpath,$(BREW)/lib
  PLATLIBS := -lfontconfig -lX11 -lpthread -ldl -lm
endif
# iOS / iPadOS (same engine, UIKit shell). PLAT=sim (Simulator), ios (device), cat (Mac Catalyst).
# Deps come from ios/build-deps.sh, V8 (jitless) from ~/src/v8ios (see CONTINUE-ios.md).
PLAT ?=
ifneq ($(filter $(PLAT),sim ios cat),)
  IOS := 1
  JIT ?= 0
  JITSFX := $(if $(filter 1,$(JIT)),-jit)
  BUILD := build-$(PLAT)$(JITSFX)
  IOSDEPS ?= $(HOME)/ios-deps/$(PLAT)
  V8DIR ?= $(HOME)/src/v8ios/v8
  V8OUT ?= $(V8DIR)/out/$(if $(filter ios,$(PLAT)),ios-device,$(if $(filter sim,$(PLAT)),ios-simulator,catalyst))$(JITSFX)
  SDKNAME := $(if $(filter ios,$(PLAT)),iphoneos,$(if $(filter sim,$(PLAT)),iphonesimulator,macosx))
  TRIPLE := arm64-apple-ios16.0$(if $(filter sim,$(PLAT)),-simulator,$(if $(filter cat,$(PLAT)),-macabi,))
  SDK := $(shell xcrun --sdk $(SDKNAME) --show-sdk-path)
  CATSYS := $(if $(filter cat,$(PLAT)),-iframework $(SDK)/System/iOSSupport/System/Library/Frameworks -isystem $(SDK)/System/iOSSupport/usr/include)
  CC := $(shell xcrun --sdk $(SDKNAME) -f clang) -target $(TRIPLE) -isysroot $(SDK) $(CATSYS)
  CXX := $(shell xcrun --sdk $(SDKNAME) -f clang++) -target $(TRIPLE) -isysroot $(SDK) $(CATSYS)
  INC := $(if $(filter 1,$(JIT)),-DLUMEN_JIT) -Isrc -I$(IOSDEPS)/include -I$(IOSDEPS)/include/freetype2 -I$(IOSDEPS)/include/harfbuzz -I$(V8DIR)/include -I$(V8DIR)/third_party/icu/source/common
  LIBDIRS := -L$(IOSDEPS)/lib -L$(V8OUT)/obj $(if $(filter cat,$(PLAT)),-F$(SDK)/System/iOSSupport/System/Library/Frameworks -L$(SDK)/System/iOSSupport/usr/lib)
  PLATLIBS := -framework UIKit $(if $(filter cat,$(PLAT)),,-framework OpenGLES) -framework Foundation -framework CoreGraphics -framework QuartzCore -framework Metal -framework AVFoundation \
              -framework AudioToolbox -framework CoreAudio -framework CoreMedia -framework CoreVideo -framework VideoToolbox -framework CoreMotion \
              -framework GameController -framework CoreHaptics -framework Security -framework UniformTypeIdentifiers -framework CoreBluetooth \
              -framework CoreText -framework IOSurface -framework ReplayKit -framework Accelerate -lc++ -lbz2 -liconv
endif
CFLAGS := -std=c11 $(OPT) $(WARN) $(INC) -D_GNU_SOURCE -D_DARWIN_C_SOURCE
CXXFLAGS := -std=c++20 $(OPT) $(WARN) $(INC) -DV8_COMPRESS_POINTERS
BASELIBS := $(LIBDIRS) $(if $(IOS),,-licuuc) -lm
NETLIBS := $(BASELIBS) -lssl -lcrypto -lz -lbrotlidec $(if $(IOS),-lbrotlicommon) -lzstd

BASE_SRC := $(wildcard src/base/*.c)
NET_SRC := $(wildcard src/net/*.c)
CORE_SRC := $(BASE_SRC) $(NET_SRC) $(wildcard src/html/*.c src/dom/*.c src/css/*.c src/text/*.c src/layout/*.c src/paint/*.c src/media/*.c src/gpu/*.c src/js/*.c)
APP_SRC := $(wildcard src/app/*.c)
JS_SRC := $(wildcard src/js/*.cc)
ifneq ($(IOS),)
  APP_SRC += src/app/iosui.m
else ifeq ($(UNAME),Darwin)
  APP_SRC += src/app/macui.m
endif
OBJ = $(patsubst %,$(BUILD)/%.o,$(1))

all: $(BUILD)/lumen

# Test assertions must remain active even in release builds.
$(BUILD)/tests/%.c.o: CFLAGS += -UNDEBUG

$(BUILD)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -c $< -o $@
$(BUILD)/%.m.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(filter-out -std=c11,$(CFLAGS)) -fobjc-arc -MMD -c $< -o $@
$(BUILD)/%.cc.o: %.cc
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -c $< -o $@

V8LIBS := -lv8 -lv8_libplatform -lv8_libbase
ifneq ($(IOS),)
  V8LINK_RSP := $(BUILD)/v8-link.rsp
  V8LIBS := @$(V8LINK_RSP)
# GN's static monolith does not contain Temporal's Rust closure. Keep V8 and
# wgpu Rust unwind symbols separate without changing their original archives.
$(V8LINK_RSP): FORCE ios/prepare-v8-link.py $(V8OUT)/obj/libv8_monolith.a $(V8OUT)/obj/v8_monolith.ninja
	python3 ios/prepare-v8-link.py --v8-dir "$(V8DIR)" --v8-out "$(V8OUT)" --output "$@" --jobs 3
FORCE:
.PHONY: FORCE
$(addprefix $(BUILD)/,lumen jsrun test_js_device_capabilities test_grid_rows test_overflow_clip test_sticky_position test_media_lifecycle test_worker_lifecycle test_promise_rejections): $(V8LINK_RSP)
endif
LIBS := $(NETLIBS) -lfreetype -lharfbuzz -lSDL3 $(V8LIBS) -lwgpu_native \
        -lavcodec -lavformat -lavutil -lswscale -lswresample -lpng -ljpeg -lwebp -lwebpdemux -lsharpyuv -lgif $(if $(IOS),-ldav1d) $(PLATLIBS)

$(BUILD)/lumen: $(call OBJ,$(CORE_SRC) $(APP_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/fetch: $(call OBJ,tests/fetch.c $(BASE_SRC) $(NET_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)
$(BUILD)/test_url: $(call OBJ,tests/test_url.c $(BASE_SRC))
	$(CC) $(OPT) $^ -o $@ $(BASELIBS)

$(BUILD)/test_net: $(call OBJ,tests/test_net.c $(BASE_SRC) src/net/cookies.c src/net/hsts.c src/net/referrer.c src/net/csp.c)
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)

$(BUILD)/test_net_cancel: $(call OBJ,tests/test_net_cancel.c $(BASE_SRC) $(NET_SRC))
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS) -lpthread

$(BUILD)/test_grid_rows: $(call OBJ,tests/test_grid_rows.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

test: $(BUILD)/test_capture_policy $(BUILD)/test_gpu_present $(BUILD)/test_device_capabilities $(BUILD)/test_js_device_capabilities $(BUILD)/test_present_retry $(BUILD)/test_css_transforms $(BUILD)/test_url $(BUILD)/test_net $(BUILD)/test_net_cancel $(BUILD)/test_grid_rows $(BUILD)/test_css_viewport $(BUILD)/test_overflow_clip $(BUILD)/test_sticky_position $(BUILD)/test_app_viewport $(BUILD)/test_touch_contacts $(BUILD)/test_media_lifecycle $(BUILD)/test_worker_lifecycle $(BUILD)/test_promise_rejections $(BUILD)/test_control $(BUILD)/test_js_control_selection $(BUILD)/test_js_control_input $(BUILD)/test_app_control $(BUILD)/test_control_paint $(BUILD)/test_control_geometry $(BUILD)/test_video_punch $(BUILD)/test_history_origin $(BUILD)/test_app_window_queue $(BUILD)/test_composer_paint $(BUILD)/test_window_lifecycle $(BUILD)/test_app_windows
	$(BUILD)/test_capture_policy
	$(BUILD)/test_history_origin
	$(BUILD)/test_app_window_queue
	$(BUILD)/test_composer_paint
	$(BUILD)/test_window_lifecycle
	$(BUILD)/test_app_windows
	$(BUILD)/test_control
	$(BUILD)/test_js_control_selection
	$(BUILD)/test_js_control_input
	$(BUILD)/test_app_control
	$(BUILD)/test_control_paint
	$(BUILD)/test_control_geometry
	$(BUILD)/test_video_punch
	$(BUILD)/test_device_capabilities
	$(BUILD)/test_js_device_capabilities
	$(BUILD)/test_url
	$(BUILD)/test_net
	$(BUILD)/test_net_cancel
	$(BUILD)/test_grid_rows
	$(BUILD)/test_css_viewport
	$(BUILD)/test_css_transforms
	$(BUILD)/test_overflow_clip
	$(BUILD)/test_sticky_position
	$(BUILD)/test_app_viewport
	$(BUILD)/test_touch_contacts
	$(BUILD)/test_gpu_present
	$(BUILD)/test_present_retry
	$(BUILD)/test_media_lifecycle
	$(BUILD)/test_worker_lifecycle
	$(BUILD)/test_promise_rejections

# iOS app bundle: flat .app with executable/resources, locally ad-hoc signed.
# Device installation uses the explicit Xcode-managed signing/export options on `ipa`.
# Mac Catalyst uses the macOS layout (Contents/MacOS, Contents/Resources). JIT=1 links the JIT V8 and signs with
# get-task-allow (iOS: JIT once a debugger enables it) / allow-jit (Catalyst).
APP := $(BUILD)/Lumen.app
APPRES := $(if $(filter cat,$(PLAT)),$(APP)/Contents/Resources,$(APP))
APPBIN := $(if $(filter cat,$(PLAT)),$(APP)/Contents/MacOS/Lumen,$(APP)/Lumen)
APPPL := $(if $(filter cat,$(PLAT)),$(APP)/Contents/Info.plist,$(APP)/Info.plist)
# Simulator: AMFI rejects ad-hoc entitlements, and sim processes may JIT anyway
ENTS := $(if $(filter 1,$(JIT)),$(if $(filter cat,$(PLAT)),ios/jit-mac.entitlements,$(if $(filter ios,$(PLAT)),ios/jit-ios.entitlements)))
CACERT ?= $(firstword $(wildcard /opt/homebrew/etc/ca-certificates/cert.pem /opt/homebrew/etc/openssl@3/cert.pem /etc/ssl/cert.pem))
app: $(BUILD)/lumen ios/Info.plist
	@mkdir -p $(APPRES) $(dir $(APPBIN))
	cp $(BUILD)/lumen $(APPBIN)
	sed -e 's/@PLATFORM@/$(if $(filter sim,$(PLAT)),iPhoneSimulator,$(if $(filter cat,$(PLAT)),MacOSX,iPhoneOS))/' ios/Info.plist > $(APPPL)
	$(if $(filter cat,$(PLAT)),plutil -insert LSMinimumSystemVersion -string 13.0 $(APPPL))
	plutil -convert binary1 $(APPPL)
	cp $(V8OUT)/icudtl.dat $(APPRES)/icudtl.dat
	cp $(CACERT) $(APPRES)/cacert.pem
	sips -z 180 180 assets/icon/lumen-256.png --out $(APPRES)/AppIcon60x60@3x.png >/dev/null
	sips -z 167 167 assets/icon/lumen-256.png --out $(APPRES)/AppIcon83.5x83.5@2x~ipad.png >/dev/null
	sips -z 152 152 assets/icon/lumen-256.png --out $(APPRES)/AppIcon76x76@2x~ipad.png >/dev/null
	sips -z 120 120 assets/icon/lumen-256.png --out $(APPRES)/AppIcon60x60@2x.png >/dev/null
	codesign --force --sign - --timestamp=none $(if $(ENTS),--entitlements $(ENTS)) $(APP)
IPA := Lumen$(if $(filter 1,$(JIT)),-jit,-jitless).ipa
# These values travel through the environment, preserving identity/profile names
# with spaces without inserting their contents into a shell command.
IOS_SIGNING_IDENTITY ?=
IOS_PROVISIONING_PROFILE ?=
IOS_DEVICE_UDID ?=
export IOS_SIGNING_IDENTITY IOS_PROVISIONING_PROFILE IOS_DEVICE_UDID
ifeq ($(PLAT),ios)
ipa: app
	python3 ios/package-ipa.py --app "$(APP)" --output "$(BUILD)/$(IPA)" $(if $(filter 1,$(JIT)),--jit)
else
ipa:
	@echo "IPA packaging requires PLAT=ios; simulator and Catalyst binaries are separate targets." >&2
	@exit 2
endif
.PHONY: app ipa

clean:
	rm -rf $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
.PHONY: all clean test
$(BUILD)/parse: $(call OBJ,tests/parse.c $(BASE_SRC) $(NET_SRC) $(wildcard src/dom/*.c) src/html/html.c src/html/entities.c)
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)
CSS_TEST_SRC := $(BASE_SRC) $(NET_SRC) $(wildcard src/dom/*.c) src/html/html.c src/html/entities.c $(wildcard src/css/*.c)
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
$(BUILD)/src/js/worker.cc.o: CXXFLAGS += -fno-rtti
$(BUILD)/src/js/js.cc.o: CXXFLAGS += -I$(BUILD)/src/js
$(BUILD)/jsrun: $(call OBJ,tests/jsrun.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_css_viewport: $(call OBJ,tests/test_css_viewport.c $(CSS_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)

$(BUILD)/test_overflow_clip: $(call OBJ,tests/test_overflow_clip.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_media_lifecycle: $(call OBJ,tests/test_media_lifecycle.c $(filter-out src/media/media.c,$(CORE_SRC)) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/tests/test_worker_lifecycle.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_worker_lifecycle: $(call OBJ,tests/test_worker_lifecycle.cc $(CORE_SRC) $(filter-out src/js/worker.cc,$(JS_SRC)))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/tests/test_promise_rejections.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_promise_rejections: $(call OBJ,tests/test_promise_rejections.cc $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_app_viewport: $(call OBJ,tests/test_app_viewport.c)
	$(CC) $(OPT) $^ -o $@ -lm
$(BUILD)/test_touch_contacts: $(call OBJ,tests/test_touch_contacts.c)
	$(CC) $(OPT) $^ -o $@

$(BUILD)/test_css_transforms: $(call OBJ,tests/test_css_transforms.c $(CSS_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)

$(BUILD)/test_present_retry: $(call OBJ,tests/test_present_retry.c)
	$(CC) $(OPT) $^ -o $@ -lm

$(BUILD)/test_sticky_position: $(call OBJ,tests/test_sticky_position.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_device_capabilities: $(call OBJ,tests/test_device_capabilities.c $(CSS_TEST_SRC))
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS)

$(BUILD)/tests/test_js_device_capabilities.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_js_device_capabilities: $(call OBJ,tests/test_js_device_capabilities.cc $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Exercise real GPU presentation using deterministic surface failures.
$(BUILD)/test_gpu_present: $(call OBJ,tests/test_gpu_present.c)
	$(CC) $^ $(LIBDIRS) -lwgpu_native -lSDL3 $(PLATLIBS) -o $@

$(BUILD)/tests/test_js_control_selection.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_js_control_selection: $(call OBJ,tests/test_js_control_selection.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Canonical controls and video overlay regressions.
$(BUILD)/test_control: $(call OBJ,tests/test_control.c src/dom/control.c src/dom/dom.c src/base/util.c)
	$(CC) $(OPT) $^ -o $@ -lm -lpthread
$(BUILD)/test_composer_paint: $(call OBJ,tests/test_composer_paint.c $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/tests/test_history_origin.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_history_origin: $(call OBJ,tests/test_history_origin.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/tests/test_js_control_input.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_js_control_input: $(call OBJ,tests/test_js_control_input.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/test_control_paint: $(call OBJ,tests/test_control_paint.c $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/test_video_punch: $(call OBJ,tests/test_video_punch.c $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_app_control: $(call OBJ,tests/test_app_control.c $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_control_geometry: $(call OBJ,tests/test_control_geometry.c $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Native auxiliary contexts: V8 wire ownership and actual deferred tab lifecycle.
$(BUILD)/src/js/windows.cc.o: CXXFLAGS += -fno-rtti
$(BUILD)/test_app_window_queue: $(call OBJ,tests/test_app_window_queue.c)
	$(CC) $(OPT) $^ -o $@
$(BUILD)/tests/test_window_lifecycle.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_window_lifecycle: $(call OBJ,tests/test_window_lifecycle.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/tests/test_app_windows.c.o: CFLAGS += -I$(BUILD)/src/app
$(BUILD)/tests/test_app_windows.c.o: $(BUILD)/src/app/icon.inc
$(BUILD)/test_app_windows: $(call OBJ,tests/test_app_windows.c $(CORE_SRC) $(JS_SRC) $(filter-out src/app/main.c,$(APP_SRC))) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Explicit integration gate: requires a real native GPU adapter on the host.
.PHONY: test-webgpu
test-webgpu: $(BUILD)/jsrun
	python3 tests/test_webgpu_buffer_lifecycle.py --jsrun $(BUILD)/jsrun

# Media capability policy and native capture lifetime.
$(BUILD)/tests/test_capture_policy.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_capture_policy: $(call OBJ,tests/test_capture_policy.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
