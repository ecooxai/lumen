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
  # Pinned Metal first-frame fix built by tools/build-wgpu.py; explicit override wins.
  WGPU_LOCAL_PREFIX := $(CURDIR)/build-deps/wgpu/macos
  WGPU_PREFIX ?= $(if $(wildcard $(WGPU_LOCAL_PREFIX)/manifest.json),$(WGPU_LOCAL_PREFIX),$(BREW)/opt/wgpu-native)
  WGPU_LIBRARY := $(WGPU_PREFIX)/lib/libwgpu_native.dylib
  DEPS := openssl@3 brotli zstd freetype harfbuzz sdl3 v8 ffmpeg libpng jpeg-turbo webp giflib icu4c
  INC += -I$(WGPU_PREFIX)/include $(foreach d,$(DEPS),-I$(BREW)/opt/$(d)/include) -I$(BREW)/opt/freetype/include/freetype2 -I$(BREW)/opt/harfbuzz/include/harfbuzz -I$(BREW)/include
  LIBDIRS := -L$(WGPU_PREFIX)/lib $(foreach d,$(DEPS),-L$(BREW)/opt/$(d)/lib)
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
  # Prefer the reproducible project-local Apple toolchain when it is present.
  # Explicit IOSDEPS/V8DIR overrides still win; older home installations remain supported.
  IOSDEPS ?= $(if $(wildcard build-apple-toolchain/$(PLAT)/include/png.h),$(CURDIR)/build-apple-toolchain/$(PLAT),$(HOME)/ios-deps/$(PLAT))
  V8DIR ?= $(if $(wildcard build-apple-toolchain/v8/include/v8.h),$(CURDIR)/build-apple-toolchain/v8,$(HOME)/src/v8ios/v8)
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

# Real data-channel transport is optional until the pinned target dependencies exist.
# Build them with tools/build-rtc.py --platform macos|sim|ios. RTC=1 requires them.
RTC ?= auto
RTC_PLATFORM := $(if $(IOS),$(PLAT),$(if $(filter Darwin,$(UNAME)),macos,linux))
RTC_PREFIX ?= $(CURDIR)/build-deps/rtc/$(RTC_PLATFORM)
RTC_ARCHIVES := $(addprefix $(RTC_PREFIX)/lib/,libdatachannel.a libjuice.a libusrsctp.a)
RTC_COMPLETE := $(and $(wildcard $(RTC_PREFIX)/include/rtc/rtc.h),$(filter 3,$(words $(wildcard $(RTC_ARCHIVES)))))
ifeq ($(filter $(RTC),auto 0 1),)
  $(error RTC must be auto, 0, or 1)
endif
ifeq ($(RTC),1)
  ifeq ($(RTC_COMPLETE),)
    $(error RTC=1 requires the pinned native libraries in $(RTC_PREFIX))
  endif
endif
RTC_ENABLED := $(if $(filter 0,$(RTC)),0,$(if $(RTC_COMPLETE),1,0))
RTC_LIBS := $(if $(filter 1,$(RTC_ENABLED)),$(RTC_ARCHIVES))

# WebTransport remains explicit opt-in until real browser acceptance passes.
# Its target builder supplies pinned archive/header provenance and measured
# Rust native-static-libs flags. No platform framework list is guessed here.
WT ?= 0
WT_PLATFORM := $(if $(IOS),$(PLAT),$(if $(filter Darwin,$(UNAME)),macos,linux))
WT_PREFIX ?= $(CURDIR)/build-deps/webtransport/$(WT_PLATFORM)
WT_ARCHIVE := $(WT_PREFIX)/lib/liblumen_webtransport_native.a
WT_MANIFEST := $(WT_PREFIX)/manifest.json
WT_HEADER := $(WT_PREFIX)/include/lumen_wt.h
ifeq ($(filter $(WT),0 1),)
  $(error WT must be 0 or 1)
endif
WT_ENABLED := $(WT)
ifeq ($(WT_ENABLED),1)
  ifeq ($(and $(wildcard $(WT_ARCHIVE)),$(wildcard $(WT_MANIFEST)),$(wildcard $(WT_HEADER))),)
    $(error WT=1 requires the pinned target backend in $(WT_PREFIX))
  endif
endif
WT_INPUTS := $(if $(filter 1,$(WT_ENABLED)),$(WT_ARCHIVE) $(WT_MANIFEST) $(WT_HEADER))
WT_LIBS := $(if $(filter 1,$(WT_ENABLED)),$(WT_ARCHIVE) @$(BUILD)/webtransport-link.rsp)

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

# A content-stable header invalidates the bridge when dependencies or RTC= change.
$(BUILD)/rtc-config.h: FORCE_RTC_CONFIG
	@mkdir -p $(dir $@)
	@printf '#define LUMEN_RTC_NATIVE %s\n' '$(RTC_ENABLED)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
FORCE_RTC_CONFIG:
.PHONY: FORCE_RTC_CONFIG
$(BUILD)/src/js/rtc_native.cc.o: $(BUILD)/rtc-config.h $(RTC_LIBS)
$(BUILD)/src/js/rtc_native.cc.o: CXXFLAGS += -I$(BUILD) -I$(RTC_PREFIX)/include -DRTC_STATIC -DRTC_ENABLE_MEDIA=0 -DRTC_ENABLE_WEBSOCKET=0 -fno-rtti

# Content-stable output also invalidates the bridge on a prefix/manifest change.
$(BUILD)/webtransport-config.h: FORCE_WT_CONFIG tools/prepare-webtransport-link.py $(WT_INPUTS)
	python3 tools/prepare-webtransport-link.py --enabled "$(WT_ENABLED)" --platform "$(WT_PLATFORM)" --prefix "$(WT_PREFIX)" --header "$@" --response "$(BUILD)/webtransport-link.rsp"
$(BUILD)/webtransport-link.rsp: $(BUILD)/webtransport-config.h
	@test -f "$@"
FORCE_WT_CONFIG:
.PHONY: FORCE_WT_CONFIG
$(BUILD)/src/js/webtransport_native.cc.o: $(BUILD)/webtransport-config.h $(WT_INPUTS)
$(BUILD)/src/js/webtransport_native.cc.o: CXXFLAGS += -I$(BUILD) -I$(WT_PREFIX)/include

# Relink GPU consumers when the selected library changes, including an
# explicit return to the Homebrew prefix. Do not apply macOS selection to iOS.
ifeq ($(UNAME),Darwin)
ifeq ($(IOS),)
$(BUILD)/wgpu-config.txt: FORCE_WGPU_CONFIG
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(WGPU_PREFIX)' > $@.tmp
	@cmp -s $@.tmp $@ || mv $@.tmp $@
	@rm -f $@.tmp
FORCE_WGPU_CONFIG:
.PHONY: FORCE_WGPU_CONFIG
$(BUILD)/src/gpu/gpu.c.o $(BUILD)/src/js/webgpu.cc.o $(BUILD)/tests/test_gpu_present.c.o: $(BUILD)/wgpu-config.txt $(WGPU_LIBRARY)
endif
endif

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
LIBS := $(WT_LIBS) $(RTC_LIBS) $(NETLIBS) -lfreetype -lharfbuzz -lSDL3 $(V8LIBS) -lwgpu_native \
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

$(BUILD)/test_host_map: $(call OBJ,tests/test_host_map.c $(BASE_SRC) $(NET_SRC))
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS) -lpthread

$(BUILD)/test_grid_rows: $(call OBJ,tests/test_grid_rows.c $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

test: $(BUILD)/test_geolocation_contract $(BUILD)/test_capture_policy $(BUILD)/test_gpu_present $(BUILD)/test_device_capabilities $(BUILD)/test_js_device_capabilities $(BUILD)/test_present_retry $(BUILD)/test_css_transforms $(BUILD)/test_url $(BUILD)/test_net $(BUILD)/test_net_cancel $(BUILD)/test_grid_rows $(BUILD)/test_css_viewport $(BUILD)/test_overflow_clip $(BUILD)/test_sticky_position $(BUILD)/test_app_viewport $(BUILD)/test_touch_contacts $(BUILD)/test_media_lifecycle $(BUILD)/test_worker_lifecycle $(BUILD)/test_promise_rejections $(BUILD)/test_control $(BUILD)/test_js_control_selection $(BUILD)/test_js_control_input $(BUILD)/test_app_control $(BUILD)/test_control_paint $(BUILD)/test_control_geometry $(BUILD)/test_video_punch $(BUILD)/test_history_origin $(BUILD)/test_app_window_queue $(BUILD)/test_composer_paint $(BUILD)/test_window_lifecycle $(BUILD)/test_app_windows $(BUILD)/test_form_dialog $(BUILD)/test_css_computed_keywords $(BUILD)/test_presentation_tokens $(BUILD)/test_video_quality $(BUILD)/test_video_quality_js $(BUILD)/test_dialog_intrinsic_height $(BUILD)/test_host_map $(BUILD)/test_url_dom
	$(BUILD)/test_presentation_tokens
	$(BUILD)/test_video_quality
	$(BUILD)/test_video_quality_js
	$(BUILD)/test_dialog_intrinsic_height
	$(BUILD)/test_form_dialog
	$(BUILD)/test_css_computed_keywords
	$(BUILD)/test_geolocation_contract
	$(BUILD)/test_capture_policy
	$(BUILD)/test_history_origin
	$(BUILD)/test_url_dom
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
	$(BUILD)/test_host_map
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
# V8 serializer delegates must use the no-RTTI contract of the V8 library.
$(BUILD)/src/js/frames.cc.o $(BUILD)/src/js/jsnative.cc.o: CXXFLAGS += -fno-rtti
$(BUILD)/src/js/frames.cc.o $(BUILD)/src/js/jsnative.cc.o: Makefile
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
$(BUILD)/tests/test_url_dom.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_history_origin: $(call OBJ,tests/test_history_origin.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_url_dom: $(call OBJ,tests/test_url_dom.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
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

# Observable geolocation cache, timeout, permission and cancellation behavior.
$(BUILD)/tests/test_geolocation_contract.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_geolocation_contract: $(call OBJ,tests/test_geolocation_contract.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Explicit native microphone graph gate using generated PCM, not physical hardware.
.PHONY: test-audio-capture
test-audio-capture: $(BUILD)/jsrun
	node tests/test_audio_capture_graph.mjs
	python3 tests/test_native_microphone.py --jsrun $(BUILD)/jsrun

# Deterministic native-bridge authorization/first-frame regression; not physical capture.
.PHONY: test-camera-authorization
test-camera-authorization:
	node tests/test_camera_authorization.mjs

$(BUILD)/tests/test_webtransport_native.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_webtransport_native: $(call OBJ,tests/test_webtransport_native.cc $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/tests/test_rtc_native.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_rtc_native: $(call OBJ,tests/test_rtc_native.cc $(CORE_SRC) $(JS_SRC))
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Actual V8/DOM dialog submission and CSS computed-value regressions.
$(BUILD)/tests/test_form_dialog.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_form_dialog: $(call OBJ,tests/test_form_dialog.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/test_css_computed_keywords: $(call OBJ,tests/test_css_computed_keywords.c $(CSS_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS)

# Actual media compositor accounting and intrinsic modal sizing regressions.
$(BUILD)/test_presentation_tokens: tests/test_presentation_tokens.c src/paint/presentation.h
	$(CC) $(CFLAGS) -UNDEBUG $< -o $@

$(BUILD)/test_video_quality: $(call OBJ,tests/test_video_quality.c $(filter-out src/media/media.c,$(CORE_SRC)) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/tests/test_video_quality_js.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_video_quality_js: $(call OBJ,tests/test_video_quality_js.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_dialog_intrinsic_height: $(call OBJ,tests/test_dialog_intrinsic_height.c $(LAYOUT_TEST_SRC))
	$(CC) $(OPT) $^ -o $@ $(NETLIBS) -lfreetype -lharfbuzz

# Native Beacon delivery and keepalive ownership regressions.
$(BUILD)/tests/test_beacon.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_beacon: $(call OBJ,tests/test_beacon.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

$(BUILD)/test_net_keepalive: $(call OBJ,tests/test_net_keepalive.c $(BASE_SRC) $(NET_SRC))
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS) -lpthread

# Public query/body regression gate in a real V8 window realm.
$(BUILD)/tests/test_url_params.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_url_params: $(call OBJ,tests/test_url_params.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# A single private URLSearchParams implementation is compiled into both realms.
$(BUILD)/src/js/url_search_params.inc: src/js/prelude/06z-urlsearchparams.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o: $(BUILD)/src/js/url_search_params.inc
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o: CXXFLAGS += -I$(BUILD)/src/js

# Public URL/DOMException Web IDL checks in actual window/child/worker realms.
$(BUILD)/tests/test_url_idl.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG -I$(BUILD)/src/js
$(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/url_search_params.inc
$(BUILD)/test_url_idl: $(call OBJ,tests/test_url_idl.cc $(CORE_SRC) $(filter-out src/js/worker.cc,$(JS_SRC))) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Shared interface factories and private native state in window and worker.
$(BUILD)/src/js/dom_exception.inc: src/js/prelude/06x-domexception.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/url_interface.inc: src/js/prelude/06y-urlinterface.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/dom_exception.inc $(BUILD)/src/js/url_interface.inc
$(BUILD)/src/js/jsnative.cc.o $(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: src/js/idl_state.h

# Actual dedicated-worker secure-context inheritance and message delivery.
$(BUILD)/tests/test_worker_security.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_worker_security: $(call OBJ,tests/test_worker_security.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Public platform-brand serialization in real window, child and worker realms.
$(BUILD)/tests/test_clone_brand.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_clone_brand: $(call OBJ,tests/test_clone_brand.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Shared private serialization recognition and intrinsic exception factories.
$(BUILD)/src/js/jsnative.cc.o $(BUILD)/src/js/frames.cc.o $(BUILD)/src/js/windows.cc.o $(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: src/js/clone_brand.h src/js/idl_state.h

# Public FileReader behavior in actual window, parsed-child and worker realms.
$(BUILD)/tests/test_file_reader.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_file_reader: $(call OBJ,tests/test_file_reader.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# One FileReader factory and immutable decoding tables for all JavaScript realms.
$(BUILD)/src/js/file_reader.inc: src/js/prelude/06w-filereader.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/file_reader.inc
$(BUILD)/src/js/jsnative.cc.o $(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: src/js/blob_state.h src/js/file_reader.h src/base/encoding.h
$(BUILD)/src/base/encoding.c.o: src/base/encoding_decode_data.inc
$(BUILD)/tests/test_encoding.c.o: CFLAGS += -UNDEBUG
$(BUILD)/test_encoding: $(call OBJ,tests/test_encoding.c src/base/encoding.c src/base/util.c)
	$(CC) $(OPT) $(filter %.o,$^) -o $@ -lm

# Actual planned form tasks, named iframe GET/POST and request ownership.
$(BUILD)/tests/test_form_navigation.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_form_navigation: $(call OBJ,tests/test_form_navigation.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -Wl,-export_dynamic -o $@ $(LIBS)

# Public HTTP document encoding/HTML URL boundary and shared scalar encoder.
$(BUILD)/tests/test_document_encoding.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_document_encoding: $(call OBJ,tests/test_document_encoding.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)
$(BUILD)/tests/test_url_encoding.c.o $(BUILD)/tests/test_encoding_encoder.c.o: CFLAGS += -UNDEBUG
$(BUILD)/test_url_encoding: $(call OBJ,tests/test_url_encoding.c $(BASE_SRC))
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(BASELIBS)
$(BUILD)/test_encoding_encoder: $(call OBJ,tests/test_encoding_encoder.c src/base/encoding.c src/base/util.c)
	$(CC) $(OPT) $(filter %.o,$^) -o $@ -lm

# Shared reverse pointer indexes supplement the existing forward decoder data.
$(BUILD)/src/base/encoding.c.o: src/base/encoding_encode_index.inc

# Synchronous iframe removing steps with retained native realm ownership.
$(BUILD)/tests/test_frame_removal.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_frame_removal: $(call OBJ,tests/test_frame_removal.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Byte-response documents and streaming HTML input preprocessing.
$(BUILD)/test_parser_input: $(call OBJ,tests/test_parser_input.c $(BASE_SRC) $(NET_SRC) $(wildcard src/dom/*.c) src/html/html.c src/html/entities.c)
	$(CC) $(OPT) $(filter %.o,$^) -o $@ $(NETLIBS)

# Raw HTTP form serialization with the shared Encoding Standard encoder.
$(BUILD)/tests/test_form_encoding.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_form_encoding: $(call OBJ,tests/test_form_encoding.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -Wl,-export_dynamic -o $@ $(LIBS)

# Public Blob/File behavior in actual window, parsed-child and worker realms.
$(BUILD)/tests/test_blob_file.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_blob_file: $(call OBJ,tests/test_blob_file.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# One immutable Blob/File factory is compiled into window and worker realms.
$(BUILD)/src/js/blob_file.inc: src/js/prelude/06v-blob.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/blob_file.inc

# Actual asynchronous V8 foreground work uses a bounded active-work deadline.
$(BUILD)/tests/test_wasm_async_deadline.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_wasm_async_deadline: $(call OBJ,tests/test_wasm_async_deadline.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Exact-pixel renderer optimization checks, including the unculled oracle.
$(BUILD)/tests/test_render_overdraw.c.o: src/paint/raster.c src/app/chrome_paint.h
$(BUILD)/test_render_overdraw: $(call OBJ,tests/test_render_overdraw.c $(filter-out src/paint/raster.c,$(CORE_SRC)) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Deep metadata cloning must retain lazy native wrappers for plain descendants.
$(BUILD)/tests/test_clone_metadata_allocation.cc.o: CXXFLAGS += -fno-rtti -UNDEBUG
$(BUILD)/test_clone_metadata_allocation: $(call OBJ,tests/test_clone_metadata_allocation.cc $(CORE_SRC) $(JS_SRC)) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# Exercise actual native wheel routing, counting expensive layout-hit calls.
$(BUILD)/tests/test_app_wheel.c.o: CFLAGS += -I$(BUILD)/src/app
$(BUILD)/tests/test_app_wheel.c.o: $(BUILD)/src/app/icon.inc src/app/main.c
$(BUILD)/test_app_wheel: $(call OBJ,tests/test_app_wheel.c $(CORE_SRC) $(JS_SRC) $(filter-out src/app/main.c,$(APP_SRC))) $(V8LINK_RSP)
	$(CXX) $(OPT) $(filter %.o,$^) -o $@ $(LIBS)

# One Fetch Headers implementation is shared by window and worker contexts.
$(BUILD)/src/js/headers.inc: src/js/prelude/06y-headers.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/headers.inc

# One Encoding Standard interface factory and checked decoder in all realms.
$(BUILD)/src/js/text_codec.inc: src/js/prelude/06u-textcodec.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/text_codec.inc
$(BUILD)/src/js/jsnative.cc.o $(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: src/js/text_codec.h src/base/encoding.h

# One stream and incremental Encoding Stream runtime for both JavaScript hosts.
$(BUILD)/src/js/web_streams.inc: src/js/prelude/06t-streams.js
	@mkdir -p $(dir $@)
	{ printf 'R"JS('; cat $<; printf ')JS"'; } > $@
$(BUILD)/src/js/worker.cc.o $(BUILD)/tests/test_worker_lifecycle.cc.o $(BUILD)/tests/test_url_idl.cc.o: $(BUILD)/src/js/web_streams.inc
