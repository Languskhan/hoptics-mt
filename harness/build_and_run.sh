#!/usr/bin/env bash
# Renders every Hoptics MT screen on a PC (no radio needed) into ../docs/screenshots/*.ppm
# Needs: a device-ui checkout with patches/device-ui.patch applied and built once with CMake
#        (cmake -B build -S . -DCMAKE_C_FLAGS=-DRAM_SIZE=1432 && cmake --build build --target lvgl)
set -euo pipefail
D=${DEVICE_UI:?set DEVICE_UI to your patched device-ui checkout}
L=$D/build/_deps/lvgl-src; G=$D/generated/ui_240x320
P=$D/build/_deps/pngdec-src/src
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_USE_LIBINPUT=0 -DLV_BUILD_TEST=0 -DRAM_SIZE=1432 -D__LINUX__ -DDEBUG_UNIT_TEST"
INC="-I$D/include -I$L -I$L/src -I$G -I$P"
cd "$(dirname "$0")"
for f in 12 14 16 20; do gcc -c -O1 $DEFS $INC $G/ui_font_montserrat_$f.c -o font$f.o; done
# stand-in street-map tiles (the harness has no internet)
[ -d /tmp/tilesrc ] || python3 make_test_tiles.py /tmp/tilesrc 38.695 -121.265 8 17 2
for f in adler32 crc32 infback inffast inflate inftrees zutil; do gcc -c -O2 $DEFS $INC $P/$f.c -o z_$f.o; done
g++ -std=c++20 -O1 $DEFS $INC -c $P/PNGdec.cpp -o PNGdec.o
g++ -std=c++20 -O1 $DEFS $INC -c $D/source/graphics/TFT/HopticsUI.cpp -o HopticsUI.o
g++ -std=c++20 -O1 $DEFS $INC -c $D/source/graphics/TFT/HopticsTiles.cpp -o HopticsTiles.o
g++ -std=c++20 -O1 $DEFS $INC -c $D/source/graphics/TFT/HopticsReport.cpp -o HopticsReport.o
g++ -std=c++20 -O1 $DEFS $INC harness.cpp HopticsUI.o HopticsTiles.o HopticsReport.o PNGdec.o z_*.o font*.o $L/../lvgl-build/liblvgl.a -lm -lpthread -o harness
./harness
