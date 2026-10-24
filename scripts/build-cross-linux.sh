#!/usr/bin/env bash
# LLVM-MinGW UCRT cross-build; no system-wide installation is required.
set -euo pipefail
: "${LLVM_MINGW_ROOT:?Set LLVM_MINGW_ROOT to the extracted LLVM-MinGW directory}"
project_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
toolchain_root="$(cd -- "$LLVM_MINGW_ROOT" && pwd)"
build_root="$project_root/build-cross"
mkdir -p "$build_root/tmp"
# These are used for their standard loader/temp-directory purposes.
export LD_LIBRARY_PATH="$toolchain_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export TMPDIR="$build_root/tmp"
compiler="$toolchain_root/bin/x86_64-w64-mingw32-clang++"
flags=(-std=c++17 -Os -Wall -Wextra -Wpedantic -Werror -ffunction-sections -fdata-sections
       -DUNICODE -D_UNICODE -DNOMINMAX -DWIN32_LEAN_AND_MEAN
       -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A00000B)
"$compiler" "${flags[@]}" -c "$project_root/src/main.cpp" -o "$build_root/main.o"
"$compiler" "${flags[@]}" -c "$project_root/src/audio_engine.cpp" -o "$build_root/audio_engine.o"
"$toolchain_root/bin/llvm-rc" "/I$project_root/src" "/FO$build_root/app.res" "$project_root/src/app.rc"
"$compiler" -static -municode -mwindows \
  -Wl,--gc-sections,--dynamicbase,--nxcompat,--high-entropy-va,--no-insert-timestamp,--strip-all \
  "$build_root/main.o" "$build_root/audio_engine.o" "$build_root/app.res" \
  -lole32 -luuid -lmmdevapi -lavrt -lcomctl32 -lcomdlg32 -lshell32 -luser32 -lgdi32 \
  -o "$build_root/PortableMixer.exe"
"$compiler" -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -static \
  "-I$project_root/src" "$project_root/tests/core_tests.cpp" -o "$build_root/mixer_core_tests.exe"
printf 'Built Windows x64 GUI: %s\n' "$build_root/PortableMixer.exe"
printf 'Built Windows core tests (not executed here): %s\n' "$build_root/mixer_core_tests.exe"
