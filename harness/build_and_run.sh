#!/usr/bin/env bash
# Renders every Hoptics MT screen on a PC (no radio needed) into ../docs/screenshots/*.ppm
# Needs: a device-ui checkout with patches/device-ui.patch applied and built once with CMake
#        (cmake -B build -S . -DCMAKE_C_FLAGS=-DRAM_SIZE=1432 && cmake --build build --target lvgl)
set -euo pipefail
D=${DEVICE_UI:?set DEVICE_UI to your patched device-ui checkout}
L=$D/build/_deps/lvgl-src; G=$D/generated/ui_240x320
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_USE_LIBINPUT=0 -DLV_BUILD_TEST=0 -DRAM_SIZE=1432"
INC="-I$D/include -I$L -I$L/src -I$G"
cd "$(dirname "$0")"
for f in 12 14 16 20; do gcc -c -O1 $DEFS $INC $G/ui_font_montserrat_$f.c -o font$f.o; done
g++ -std=c++20 -O1 $DEFS $INC -c $D/source/graphics/TFT/HopticsUI.cpp -o HopticsUI.o
g++ -std=c++20 -O1 $DEFS $INC harness.cpp HopticsUI.o font*.o $L/../lvgl-build/liblvgl.a -lm -lpthread -o harness
./harness
