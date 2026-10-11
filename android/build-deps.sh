#!/bin/bash
# Cross-compiles Lumen's native dependencies for Android.
# Usage: android/build-deps.sh <abi: x86_64|arm64-v8a> [dep...]
# Env: ANDROID_NDK (default ~/android-sdk/ndk/<newest>), DEPS_SRC (default ~/src/deps), PREFIX (default ~/android-deps/<abi>)
set -euo pipefail
ABI=${1:?abi}; shift || true
API=${ANDROID_API:-26}
NDK=${ANDROID_NDK:-$(ls -d ~/android-sdk/ndk/* | sort -V | tail -1)}
SRC=${DEPS_SRC:-~/src/deps}
PREFIX=${PREFIX:-~/android-deps/$ABI}
J=$(nproc)
case $ABI in
  x86_64) TRIPLE=x86_64-linux-android; OSSL=android-x86_64; FFARCH=x86_64; RUST=x86_64;;
  arm64-v8a) TRIPLE=aarch64-linux-android; OSSL=android-arm64; FFARCH=aarch64; RUST=aarch64;;
  *) echo "bad abi"; exit 1;;
esac
TC=$NDK/toolchains/llvm/prebuilt/linux-x86_64
export PATH=$TC/bin:$PATH
CC="$TC/bin/$TRIPLE$API-clang"; CXX="$TC/bin/$TRIPLE$API-clang++"
mkdir -p $PREFIX/{lib,include} ~/android-deps/build/$ABI
B=~/android-deps/build/$ABI
CMK=(-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake -DANDROID_ABI=$ABI -DANDROID_PLATFORM=android-$API
     -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$PREFIX -DCMAKE_PREFIX_PATH=$PREFIX -DCMAKE_FIND_ROOT_PATH=$PREFIX
     -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON -G Ninja)
cm() { local name=$1 dir=$2; shift 2; rm -rf $B/$name; cmake -S $dir -B $B/$name "${CMK[@]}" "$@" && cmake --build $B/$name -j$J && cmake --install $B/$name; }

dep_openssl() {
  rm -rf $B/openssl && cp -r $SRC/openssl-3.5.4 $B/openssl && cd $B/openssl
  ANDROID_NDK_ROOT=$NDK ./Configure $OSSL -D__ANDROID_API__=$API no-shared no-tests no-docs --prefix=$PREFIX --libdir=lib
  make -j$J build_libs && make install_dev
}
dep_brotli() { cm brotli $SRC/brotli-1.1.0 -DBROTLI_DISABLE_TESTS=ON; }
dep_zstd() { cm zstd $SRC/zstd-1.5.7/build/cmake -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_TESTS=OFF; }
dep_png() { cm png $SRC/libpng-1.6.50 -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF; }
dep_jpeg() { cm jpeg $SRC/libjpeg-turbo-3.1.1 -DENABLE_SHARED=OFF -DWITH_TURBOJPEG=OFF; }
dep_webp() { cm webp $SRC/libwebp-1.6.0 -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF; }
dep_gif() {
  rm -rf $B/gif && mkdir -p $B/gif && cd $SRC/giflib-5.2.2
  for f in dgif_lib egif_lib gifalloc gif_err gif_font gif_hash openbsd-reallocarray quantize; do $CC -O2 -fPIC -c $f.c -o $B/gif/$f.o; done
  $TC/bin/llvm-ar rcs $PREFIX/lib/libgif.a $B/gif/*.o && cp gif_lib.h $PREFIX/include/
}
dep_freetype() { cm freetype $SRC/freetype-2.13.3 -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=OFF -DFT_REQUIRE_PNG=ON -DFT_REQUIRE_ZLIB=ON; }
dep_harfbuzz() {
  cat > $B/meson-cross.txt <<X
[binaries]
c = '$CC'
cpp = '$CXX'
ar = '$TC/bin/llvm-ar'
strip = '$TC/bin/llvm-strip'
pkg-config = 'false'
[host_machine]
system = 'android'
cpu_family = '$RUST'
cpu = '$RUST'
endian = 'little'
X
  rm -rf $B/harfbuzz
  meson setup $B/harfbuzz $SRC/harfbuzz-11.2.1 --cross-file $B/meson-cross.txt --prefix=$PREFIX --libdir=lib --default-library=static --buildtype=release \
    -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled \
    -Dc_args="-I$PREFIX/include/freetype2" -Dcpp_args="-I$PREFIX/include/freetype2" -Dc_link_args="-L$PREFIX/lib" -Dcpp_link_args="-L$PREFIX/lib" --wrap-mode=nofallback || true
  # meson's freetype lookup needs pkg-config; fall back to a pc file we write
  if [ ! -f $B/harfbuzz/build.ninja ]; then
    mkdir -p $PREFIX/lib/pkgconfig
    printf 'prefix=%s\nName: freetype2\nDescription: ft\nVersion: 26.2.20\nLibs: -L%s/lib -lfreetype -lpng16 -lz\nCflags: -I%s/include/freetype2\n' $PREFIX $PREFIX $PREFIX > $PREFIX/lib/pkgconfig/freetype2.pc
    sed -i "s#pkg-config = 'false'#pkg-config = 'pkg-config'#" $B/meson-cross.txt
    PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig meson setup $B/harfbuzz $SRC/harfbuzz-11.2.1 --cross-file $B/meson-cross.txt --prefix=$PREFIX --libdir=lib --default-library=static --buildtype=release \
      -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled -Dicu=disabled -Dtests=disabled -Ddocs=disabled -Dutilities=disabled -Dintrospection=disabled --wrap-mode=nofallback
  fi
  ninja -C $B/harfbuzz && ninja -C $B/harfbuzz install
}
dep_ffmpeg() {
  rm -rf $B/ffmpeg && mkdir -p $B/ffmpeg && cd $B/ffmpeg
  local asm=""; [ $ABI = x86_64 ] && asm="--disable-asm"   # x86 inline asm (cabac) is not PIC; NDK needs PIC for liblumen.so
  $SRC/ffmpeg-9.0.2/configure --prefix=$PREFIX --target-os=android --arch=$FFARCH --enable-cross-compile --cc=$CC --cxx=$CXX \
    --ar=$TC/bin/llvm-ar --nm=$TC/bin/llvm-nm --ranlib=$TC/bin/llvm-ranlib --strip=$TC/bin/llvm-strip \
    --enable-pic --disable-shared --enable-static --disable-programs --disable-doc --disable-avdevice --disable-avfilter \
    --disable-network --enable-jni --enable-mediacodec --disable-vulkan $asm --extra-cflags="-O2"
  make -j$J && make install
}
dep_sdl() { cm sdl $SRC/SDL3-3.4.18 -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF -DSDL_VULKAN=ON; }
dep_wgpu() {
  local z=$SRC/wgpu-android-$RUST.zip; rm -rf $B/wgpu && mkdir -p $B/wgpu && cd $B/wgpu && unzip -q $z
  cp include/webgpu/*.h $PREFIX/include/ && cp lib/libwgpu_native.so $PREFIX/lib/
}
ALL="openssl brotli zstd png jpeg webp gif freetype harfbuzz ffmpeg sdl wgpu"
for d in ${@:-$ALL}; do echo "=== $d ($ABI)"; ( dep_$d ) > $B/$d.log 2>&1 && echo "ok $d" || { echo "FAIL $d (see $B/$d.log)"; tail -20 $B/$d.log; exit 1; }; done
