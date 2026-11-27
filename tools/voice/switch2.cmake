# SPDX-License-Identifier: MIT
# CMake toolchain for building the voice libraries for the Switch 2 (see
# build_deps.sh). Adapted from DiscordNX2's (MIT).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_C_COMPILER "$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-gcc")
set(CMAKE_CXX_COMPILER "$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-g++")
set(CMAKE_AR "$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-ar")
set(CMAKE_RANLIB "$ENV{DEVKITPRO}/devkitA64/bin/aarch64-none-elf-ranlib")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(_flags "-mcpu=cortex-a78c+crypto -mtp=soft -fPIE -D__SWITCH__ -D__SWITCH2__ -I$ENV{DEVKITPRO}/libounce/include -ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT "${_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_flags}")
