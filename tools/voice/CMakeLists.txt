# SPDX-License-Identifier: MIT
# Builds MLS++ and libdave (Discord's DAVE end-to-end encryption) as one
# static library for the Switch 2, against the OpenSSL that build_deps.sh
# builds first. Adapted from DiscordNX2's (MIT).
cmake_minimum_required(VERSION 3.20)
project(ouncecord_voice_deps LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(OpenSSL 3.0 REQUIRED)
file(WRITE "${CMAKE_BINARY_DIR}/namespace.h" "#pragma once\n#define MLS_NAMESPACE mlspp\n")
set(MLS "${VOICE_SOURCES}/mls")
set(DAVE "${VOICE_SOURCES}/dave/cpp")
file(GLOB MLS_SRC "${MLS}/src/*.cpp" "${MLS}/lib/bytes/src/*.cpp" "${MLS}/lib/tls_syntax/src/*.cpp" "${MLS}/lib/hpke/src/*.cpp")
file(GLOB_RECURSE DAVE_SRC "${DAVE}/src/*.cpp")
list(FILTER DAVE_SRC EXCLUDE REGEX "bindings_wasm|boringssl_cryptor|persisted_key|_win\\.cpp|_apple\\.cpp")
list(APPEND DAVE_SRC "${DAVE}/src/mls/persisted_key_pair_null.cpp")
add_library(dave STATIC ${MLS_SRC} ${DAVE_SRC})
target_include_directories(dave PUBLIC "${DAVE}/includes" PRIVATE "${MLS}/include" "${DAVE}/src" "${MLS}/lib/bytes/include" "${MLS}/lib/tls_syntax/include" "${MLS}/lib/hpke/include" "${MLS}/third_party" "${CMAKE_BINARY_DIR}" "${VOICE_SOURCES}/json")
target_compile_definitions(dave PUBLIC DAVE_STATIC PRIVATE WITH_OPENSSL3 DISABLE_GREASE)
target_compile_options(dave PRIVATE -O2 -Wno-deprecated-declarations)
target_link_libraries(dave PUBLIC OpenSSL::Crypto)
install(TARGETS dave ARCHIVE DESTINATION lib)
install(DIRECTORY "${DAVE}/includes/" DESTINATION include)
