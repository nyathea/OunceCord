#!/bin/sh
# SPDX-License-Identifier: MIT
#
# Writes the build date and git commit (for Settings > About) to the header
# given, only when they changed, so a build with nothing new to compile
# doesn't recompile anything.
#   build_info.sh <header> <source checkout>
out="$1"
repo="${2:-.}"
commit=$(git -C "$repo" rev-parse --short HEAD 2>/dev/null)
if [ -z "$commit" ]; then
    commit="Unknown"
elif [ -n "$(git -C "$repo" status --porcelain --untracked-files=no 2>/dev/null)" ]; then
    commit="$commit (modified)"  # built with changes that aren't committed
fi
text="#define BUILD_DATE \"$(date '+%Y-%m-%d %H:%M')\"
#define BUILD_COMMIT \"$commit\""
if [ ! -f "$out" ] || [ "$(cat "$out")" != "$text" ]; then
    printf '%s\n' "$text" > "$out"
fi
