#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Builds the libraries voice chat needs that devkitPro doesn't provide, once:
# OpenSSL 3 (libcrypto only), Cisco's MLS++ and Discord's libdave (DAVE, the
# end-to-end encryption Discord requires for voice). Sources are pinned to
# exact commits and checked against their SHA-256 before use. The result goes
# in .voice-deps/prefix (lib/libcrypto.a, lib/libdave.a and their headers).
# The Makefile runs this automatically; it needs cmake, ninja, perl and curl.
# Opus comes from devkitPro's portlibs (switch-libopus).
#
# The pins and the build recipe follow DiscordNX2's (MIT), which built these
# with the same toolchain.
set -eu
root=$(cd "$(dirname "$0")/../.." && pwd)
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$PATH"
cache="$root/.voice-deps"
for tool in curl tar perl make cmake ninja aarch64-none-elf-gcc; do
    command -v "$tool" >/dev/null || { echo "voice deps: $tool is missing (install cmake, ninja, perl and curl)"; exit 1; }
done
if command -v sha256sum >/dev/null; then sha() { sha256sum "$1" | cut -d' ' -f1; }; else sha() { shasum -a 256 "$1" | cut -d' ' -f1; }; fi
stamp=$(cat "$root/tools/voice/build_deps.sh" "$root/tools/voice/CMakeLists.txt" "$root/tools/voice/switch2.cmake" | (sha256sum 2>/dev/null || shasum -a 256) | cut -d' ' -f1)
if [ -f "$cache/ready" ] && [ "$(cat "$cache/ready")" = "$stamp" ] && [ -f "$cache/prefix/lib/libdave.a" ] && [ -f "$cache/prefix/lib/libcrypto.a" ]; then
    exit 0
fi
mkdir -p "$cache/downloads" "$cache/src" "$cache/prefix"
fetch() {  # name sha256 url
    file="$cache/downloads/$1"
    if [ ! -f "$file" ] || [ "$(sha "$file")" != "$2" ]; then
        curl --fail --location --retry 2 --proto '=https' --tlsv1.2 -o "$file.part" "$3"
        [ "$(sha "$file.part")" = "$2" ] || { echo "voice deps: checksum mismatch for $1"; exit 1; }
        mv "$file.part" "$file"
    fi
}
unpack() {
    if [ ! -f "$cache/src/$1/.extracted" ]; then
        rm -rf "$cache/src/$1"; mkdir -p "$cache/src/$1"
        tar -xzf "$cache/downloads/$1.tar.gz" --strip-components=1 -C "$cache/src/$1"
        touch "$cache/src/$1/.extracted"
    fi
}
echo "voice deps: building OpenSSL, MLS++ and libdave for the Switch 2 (first time only, a few minutes)"
fetch dave.tar.gz 7e1e3950e3b87bec716db21a7ded4521674988b97319e8a192045127e4d601a3 https://codeload.github.com/discord/libdave/tar.gz/b5463c443df74db75a713c775b09a847621e6d62
fetch mls.tar.gz 7a9d6318627e548903bc65c3dc5a4de4a90290983efea9a49af8561ed3f999f5 https://codeload.github.com/cisco/mlspp/tar.gz/1cc50a124a3bc4e143a787ec934280dc70c1034d
fetch openssl.tar.gz f54250b21795bb01240f3f9b42c384868d04460e339e39275aaaf93487f2d903 https://codeload.github.com/openssl/openssl/tar.gz/45e844fa2a14ec92d146bd8f5778ac130b6625fb
fetch json.hpp aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63 https://raw.githubusercontent.com/nlohmann/json/v3.12.0/single_include/nlohmann/json.hpp
for n in dave mls openssl; do unpack "$n"; done
mkdir -p "$cache/src/json/nlohmann" && cp "$cache/downloads/json.hpp" "$cache/src/json/nlohmann/json.hpp"
jobs=${VOICE_DEPS_JOBS:-8}
mkdir -p "$cache/openssl-build"
(
    cd "$cache/openssl-build"
    export CFLAGS="-O2 -DNO_SYSLOG -mcpu=cortex-a78c+crypto -mtp=soft -fPIE -D__SWITCH__ -D__SWITCH2__ -I$DEVKITPRO/libounce/include -ffunction-sections -fdata-sections"
    perl "$cache/src/openssl/Configure" gcc --cross-compile-prefix=aarch64-none-elf- --prefix="$cache/prefix" --libdir=lib \
        no-asm no-shared no-threads no-sock no-dso no-engine no-module no-tests no-apps no-autoload-config no-async \
        no-ui-console --with-rand-seed=none
    make -j"$jobs" build_libs
    make install_dev
) > "$cache/openssl-build.log" 2>&1 || { tail -n 40 "$cache/openssl-build.log"; exit 1; }
cmake -S "$root/tools/voice" -B "$cache/dave-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$root/tools/voice/switch2.cmake" -DVOICE_SOURCES="$cache/src" \
    -DOPENSSL_ROOT_DIR="$cache/prefix" -DOPENSSL_INCLUDE_DIR="$cache/prefix/include" \
    -DOPENSSL_CRYPTO_LIBRARY="$cache/prefix/lib/libcrypto.a" -DCMAKE_INSTALL_PREFIX="$cache/prefix" \
    > "$cache/dave-build.log" 2>&1 \
  && cmake --build "$cache/dave-build" -j"$jobs" >> "$cache/dave-build.log" 2>&1 \
  && cmake --install "$cache/dave-build" >> "$cache/dave-build.log" 2>&1 \
  || { tail -n 40 "$cache/dave-build.log"; exit 1; }
echo "$stamp" > "$cache/ready"
echo "voice deps: ready"
