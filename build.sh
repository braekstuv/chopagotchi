#!/bin/sh
# Builds Chopagotchi.asi with MinGW-w64. Run inside a container or any system
# that has x86_64-w64-mingw32-g++ and x86_64-w64-mingw32-dlltool.
set -eu
cd "$(dirname "$0")"
x86_64-w64-mingw32-dlltool -d ScriptHookV.def -l libScriptHookV.a
x86_64-w64-mingw32-g++ -O2 -std=c++17 -Wall -Wextra -shared -static \
  -o Chopagotchi.asi Chopagotchi.cpp -L. -lScriptHookV
x86_64-w64-mingw32-strip Chopagotchi.asi
rm -f libScriptHookV.a
x86_64-w64-mingw32-objdump -p Chopagotchi.asi | grep -A12 'DLL Name: ScriptHookV.dll'
