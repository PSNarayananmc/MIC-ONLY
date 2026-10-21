#!/bin/bash
# VoiceCraft Studio build environment (source this file: . /home/z/my-project/scripts/env.sh)
export VOICECRAFT_PREFIX="$HOME/opt"
export LD_LIBRARY_PATH="$VOICECRAFT_PREFIX/usr/lib/x86_64-linux-gnu:$VOICECRAFT_PREFIX/lib:$LD_LIBRARY_PATH"
export PKG_CONFIG_PATH="$VOICECRAFT_PREFIX/usr/lib/x86_64-linux-gnu/pkgconfig:$VOICECRAFT_PREFIX/lib/pkgconfig:$VOICECRAFT_PREFIX/usr/share/pkgconfig:$PKG_CONFIG_PATH"
export CMAKE_PREFIX_PATH="$VOICECRAFT_PREFIX/usr"
export PATH="$VOICECRAFT_PREFIX/usr/bin:$VOICECRAFT_PREFIX/usr/lib/qt5/bin:$PATH"
export QT_SELECT=qt5
# GCC linker path for the extracted user-space libs (CMake drops -L from imported pkg-config targets)
export LIBRARY_PATH="$VOICECRAFT_PREFIX/usr/lib/x86_64-linux-gnu:$VOICECRAFT_PREFIX/lib:$LIBRARY_PATH"
# Qt must find its own plugins relative to the extracted prefix
export QT_PLUGIN_PATH="$VOICECRAFT_PREFIX/usr/lib/x86_64-linux-gnu/qt5/plugins"
# Offscreen rendering for headless GUI tests
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
export LD_LIBRARY_PATH="$VOICECRAFT_PREFIX/usr/lib/pulse-17.0+dfsg1/modules:$VOICECRAFT_PREFIX/usr/lib/x86_64-linux-gnu/pulseaudio:$LD_LIBRARY_PATH"
