#!/usr/bin/env bash
# Renders every Hoptics MT screen on a PC (no radio needed):
#   240x320 portrait (Heltec kits)              -> shots/*.ppm
#   320x240 landscape (T-Deck Plus, ThinkNode M9) -> shots_320x240/*.ppm (plus key-navigation shots)
# Needs: a device-ui checkout with patches/device-ui.patch applied and built once with CMake
#        (cmake -B build -S . -DCMAKE_C_FLAGS=-DRAM_SIZE=1432 && cmake --build build --target lvgl)
# HARNESS_SIZES picks the runs (default both), HARNESS_TMP where scratch files go (default /tmp).
set -euo pipefail
D=${DEVICE_UI:?set DEVICE_UI to your patched device-ui checkout}
L=$D/build/_deps/lvgl-src
P=$D/build/_deps/pngdec-src/src
DEFS="-DLV_CONF_INCLUDE_SIMPLE -DLV_LVGL_H_INCLUDE_SIMPLE -DLV_USE_LIBINPUT=0 -DLV_BUILD_TEST=0 -DRAM_SIZE=1432 -D__LINUX__ -DDEBUG_UNIT_TEST"
INC="-I$D/include -I$L -I$L/src -I$P"
SIZES=${HARNESS_SIZES:-240x320 320x240}
cd "$(dirname "$0")"
# stand-in street-map tiles (the harness has no internet)
T=${HARNESS_TMP:-/tmp}
[ -d "$T/tilesrc" ] || python3 make_test_tiles.py "$T/tilesrc" 38.695 -121.265 8 17 2
for f in adler32 crc32 infback inffast inflate inftrees zutil; do gcc -c -O2 $DEFS $INC $P/$f.c -o z_$f.o; done
g++ -std=c++20 -O1 $DEFS $INC -c $P/PNGdec.cpp -o PNGdec.o
for S in $SIZES; do
    # each panel size links the fonts generated for it, like the firmware does (VIEW_240x320 / VIEW_320x240),
    # and landscape stands in for the T-Deck Plus (board facts in HopticsBoard.h: no sensor slots, GPS)
    G=$D/generated/ui_$S
    BOARD=""
    [ "$S" = 320x240 ] && BOARD="-DT_DECK -DT_DECK_PLUS"
    for m in HopticsUI HopticsTiles HopticsReport; do
        g++ -std=c++20 -O1 $DEFS $BOARD $INC -c $D/source/graphics/TFT/$m.cpp -o ${m}_$S.o
    done
    for f in 12 14 16 20; do gcc -c -O1 $DEFS $INC -I$G $G/ui_font_montserrat_$f.c -o font${f}_$S.o; done
    BIN=harness; OUT=shots
    [ "$S" != 240x320 ] && BIN=harness_$S && OUT=shots_$S
    g++ -std=c++20 -O1 $DEFS $INC harness.cpp HopticsUI_$S.o HopticsTiles_$S.o HopticsReport_$S.o PNGdec.o z_*.o font*_$S.o \
        $L/../lvgl-build/liblvgl.a -lm -lpthread -o $BIN
    mkdir -p $OUT
    ./$BIN $S $OUT
done
