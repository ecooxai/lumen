#!/bin/bash
# Cross-compiles Lumen's native dependencies for iOS / iPadOS (and Mac Catalyst).
# Usage: ios/build-deps.sh <plat: ios|sim|cat> [dep...]
#   ios = iphoneos arm64 (iPad devices, and Apple Silicon Macs as "Designed for iPad")
#   sim = iphonesimulator arm64, cat = Mac Catalyst arm64 (macabi)
# Env: DEPS_SRC (default ~/src/deps), PREFIX (default ~/ios-deps/<plat>), WGPU_DIR (unzipped wgpu-native iOS releases)
set -euo pipefail
PLAT=${1:?plat}; shift || true
MIN=${IOS_MIN:-16.0}
SRC=${DEPS_SRC:-~/src/deps}
PREFIX=${PREFIX:-~/ios-deps/$PLAT}
J=$(sysctl -n hw.ncpu)
case $PLAT in
  ios) SDKN=iphoneos; TRIPLE=arm64-apple-ios$MIN; OSSL=ios64-xcrun; CMSYS=iOS; MESYS=ios; WGPU=wgpu-ios-aarch64-release;;
  sim) SDKN=iphonesimulator; TRIPLE=arm64-apple-ios$MIN-simulator; OSSL=iossimulator-arm64-xcrun; CMSYS=iOS; MESYS=ios; WGPU=wgpu-ios-aarch64-simulator-release;;
  cat) SDKN=macosx; TRIPLE=arm64-apple-ios$MIN-macabi; OSSL=darwin64-arm64-cc; CMSYS=Darwin; MESYS=darwin; WGPU=;;
  *) echo "bad plat"; exit 1;;
esac
SDK=$(xcrun --sdk $SDKN --show-sdk-path)
CC="$(xcrun -f clang)"; CXX="$(xcrun -f clang++)"
TF="-target $TRIPLE -isysroot $SDK"
[ $PLAT = cat ] && TF="$TF -iframework $SDK/System/iOSSupport/System/Library/Frameworks -isystem $SDK/System/iOSSupport/usr/include"
export CFLAGS="$TF -O2" CXXFLAGS="$TF -O2" LDFLAGS="$TF"
mkdir -p $PREFIX/{lib,include,lib/pkgconfig} ~/ios-deps/build/$PLAT
B=~/ios-deps/build/$PLAT
CMK=(-DCMAKE_SYSTEM_NAME=$CMSYS -DCMAKE_OSX_SYSROOT=$SDK -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_C_COMPILER=$CC -DCMAKE_CXX_COMPILER=$CXX
     -DCMAKE_C_FLAGS="$TF" -DCMAKE_CXX_FLAGS="$TF" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX
     -DCMAKE_FIND_ROOT_PATH=$PREFIX -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
     -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_MACOSX_BUNDLE=OFF -G Ninja)
[ $CMSYS = iOS ] && CMK+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN)
cm() { local name=$1 dir=$2; shift 2; rm -rf $B/$name; cmake -S $dir -B $B/$name "${CMK[@]}" "$@" && cmake --build $B/$name -j$J && cmake --install $B/$name; }
meson_cross() {
  cat > $B/meson-cross.txt <<X
[binaries]
c = '$CC'
cpp = '$CXX'
objc = '$CC'
ar = '$(xcrun -f ar)'
strip = '$(xcrun -f strip)'
pkg-config = 'pkg-config'
[built-in options]
c_args = [$(for f in $TF; do printf "'%s', " $f; done)]
cpp_args = [$(for f in $TF; do printf "'%s', " $f; done)]
c_link_args = [$(for f in $TF; do printf "'%s', " $f; done)]
cpp_link_args = [$(for f in $TF; do printf "'%s', " $f; done)]
[host_machine]
system = '$MESYS'
subsystem = '$([ $PLAT = cat ] && echo macos || echo ios)'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'
X
}
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig PKG_CONFIG_PATH=

dep_openssl() {
  rm -rf $B/openssl && cp -r $SRC/openssl-3.5.4 $B/openssl && cd $B/openssl
  if [ $PLAT = cat ]; then CC=$CC CFLAGS="$TF" ./Configure $OSSL no-shared no-tests no-docs no-apps --prefix=$PREFIX --libdir=lib || return 1
  else CC=$CC ./Configure $OSSL no-shared no-tests no-docs no-apps -mios-version-min=$MIN --prefix=$PREFIX --libdir=lib || return 1; fi
  make -j$J build_libs && make install_dev
}
dep_brotli() { cm brotli $SRC/brotli-1.1.0 -DBROTLI_DISABLE_TESTS=ON -DBROTLI_BUNDLED_MODE=OFF; }
dep_zstd() { cm zstd $SRC/zstd-1.5.7/build/cmake -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_TESTS=OFF; }
dep_png() { cm png $SRC/libpng-1.6.50 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF; }
dep_jpeg() { cm jpeg $SRC/libjpeg-turbo-3.1.1 -DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF -DCMAKE_SYSTEM_PROCESSOR=aarch64; }
dep_webp() { cm webp $SRC/libwebp-1.6.0 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF; }
dep_gif() {
  rm -rf $B/gif && mkdir -p $B/gif && cd $SRC/giflib-5.2.2
  for f in dgif_lib egif_lib gifalloc gif_err gif_font gif_hash openbsd-reallocarray quantize; do $CC $CFLAGS -c $f.c -o $B/gif/$f.o; done
  rm -f $PREFIX/lib/libgif.a; xcrun ar rcs $PREFIX/lib/libgif.a $B/gif/*.o && cp gif_lib.h $PREFIX/include/
}
dep_freetype() { cm freetype $SRC/freetype-2.13.3 -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON; }
dep_harfbuzz() {
  meson_cross; rm -rf $B/harfbuzz
  meson setup $B/harfbuzz $SRC/harfbuzz-11.2.1 --cross-file $B/meson-cross.txt --prefix=$PREFIX --libdir=lib --default-library=static --buildtype=release \
    -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dcoretext=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled --wrap-mode=nofallback
  ninja -C $B/harfbuzz && ninja -C $B/harfbuzz install
}
dep_dav1d() {
  meson_cross; rm -rf $B/dav1d
  meson setup $B/dav1d $SRC/dav1d-1.5.1 --cross-file $B/meson-cross.txt --prefix=$PREFIX --libdir=lib --default-library=static --buildtype=release \
    -Denable_tools=false -Denable_tests=false -Denable_examples=false
  ninja -C $B/dav1d && ninja -C $B/dav1d install
}
dep_ffmpeg() {
  rm -rf $B/ffmpeg && mkdir -p $B/ffmpeg && cd $B/ffmpeg
  $SRC/ffmpeg-9.0.2/configure --prefix=$PREFIX --target-os=darwin --arch=aarch64 --enable-cross-compile --cc="$CC" --cxx="$CXX" \
    --sysroot=$SDK --extra-cflags="$TF" --extra-ldflags="$TF" --pkg-config=pkg-config --pkg-config-flags=--static \
    --enable-pic --disable-shared --enable-static --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
    --disable-network --disable-xlib --disable-securetransport --disable-sdl2 --disable-iconv --disable-lzma --disable-bzlib \
    --enable-videotoolbox --enable-audiotoolbox --enable-libdav1d
  make -j$J && make install
}
dep_sdl() { cm sdl $SRC/SDL3-3.4.16 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_TESTS=OFF; }
dep_wgpu() {
  if [ -z "$WGPU" ]; then   # Mac Catalyst: no prebuilt; build wgpu-native from source (rustup target add aarch64-apple-ios-macabi)
    local w=${WGPU_SRC:-$HOME/src/wgpu-native}
    (cd $w && PATH=$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/bin:$PATH RUSTUP_TOOLCHAIN=stable cargo build --release --target aarch64-apple-ios-macabi) || return 1
    cp $w/ffi/webgpu-headers/webgpu.h $w/ffi/wgpu.h $PREFIX/include/
    cp $w/target/aarch64-apple-ios-macabi/release/libwgpu_native.a $PREFIX/lib/
    return
  fi
  local d=${WGPU_DIR:-$HOME/src/wgpu-ios}/$WGPU
  cp $d/include/webgpu/*.h $PREFIX/include/ 2>/dev/null || cp -r $d/include/* $PREFIX/include/
  cp $d/lib/libwgpu_native.a $PREFIX/lib/
}
ALL="openssl brotli zstd png jpeg webp gif freetype harfbuzz dav1d ffmpeg sdl wgpu"
for d in ${@:-$ALL}; do echo "=== $d ($PLAT)"; ( dep_$d ) > $B/$d.log 2>&1 && echo "ok $d" || { echo "FAIL $d (see $B/$d.log)"; tail -30 $B/$d.log; exit 1; }; done
