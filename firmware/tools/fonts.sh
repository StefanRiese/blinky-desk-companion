#!/usr/bin/env bash
# Regenerates the UI fonts in components/ui/fonts/ with lv_font_conv. The generated C files
# are committed, so building the firmware needs neither this script nor Node.js.
#
# Needs: Node.js (npx), curl, unzip, sha256sum. Source fonts are downloaded once into
# tools/.cache/ and checked against their SHA-256:
#   Inter 4.1 (SIL Open Font License 1.1)          https://github.com/rsms/inter
#   Material Icons (Apache License 2.0)            https://github.com/google/material-design-icons
set -euo pipefail
cd "$(dirname "$0")/.."

CACHE=tools/.cache
OUT=components/ui/fonts
CONV=(npx --yes lv_font_conv@1.5.3 --bpp 4 --no-compress --format lvgl --lv-include lvgl.h)

INTER_URL=https://github.com/rsms/inter/releases/download/v4.1/Inter-4.1.zip
INTER_SHA=9883fdd4a49d4fb66bd8177ba6625ef9a64aa45899767dde3d36aa425756b11e
ICONS_URL=https://raw.githubusercontent.com/google/material-design-icons/f7bd4f25f3764883717c09a1fd867f560c9a9581/font/MaterialIcons-Regular.ttf
ICONS_SHA=ef149f08bdd2ff09a4e2c8573476b7b0f3fbb15b623954ade59899e7175bedda

fetch() { # url file sha
    if [ ! -f "$2" ]; then
        echo "downloading $(basename "$2")"
        curl -fsSL -o "$2.tmp" "$1"
        mv "$2.tmp" "$2"
    fi
    echo "$3  $2" | sha256sum -c --quiet || { echo "checksum mismatch: $2" >&2; exit 1; }
}

mkdir -p "$CACHE" "$OUT"
fetch "$INTER_URL" "$CACHE/Inter-4.1.zip" "$INTER_SHA"
fetch "$ICONS_URL" "$CACHE/MaterialIcons-Regular.ttf" "$ICONS_SHA"
unzip -o -j -q "$CACHE/Inter-4.1.zip" extras/ttf/Inter-Regular.ttf extras/ttf/Inter-Light.ttf -d "$CACHE"

# Text: Latin-1 (umlauts, °, ·) plus dashes, ellipsis, bullet, € and arrows. The two
# sizes that show what hosts and settings send (any text).
TEXT=0x20-0x7E,0xA0-0xFF,0x2013-0x2014,0x2022,0x2026,0x20AC,0x2190-0x2193
for size in 14 20; do
    "${CONV[@]}" --size "$size" --font "$CACHE/Inter-Regular.ttf" -r "$TEXT" \
        --lv-font-name "inter_$size" -o "$OUT/inter_$size.c"
done
# The larger sizes show only the firmware's own texts, so they carry just what those need
# (each glyph of all Latin-1 costs flash, about 75 KB together): 28 px ASCII ("Blinky",
# the sleepy "z"), 40 px the dashboard's temperature.
"${CONV[@]}" --size 28 --font "$CACHE/Inter-Regular.ttf" -r 0x20-0x7E \
    --lv-font-name inter_28 -o "$OUT/inter_28.c"
"${CONV[@]}" --size 40 --font "$CACHE/Inter-Regular.ttf" --symbols "0123456789.,-–°CF " \
    --lv-font-name inter_40 -o "$OUT/inter_40.c"
# Keep the line metrics of the full Latin-1 fonts (they come from the tallest glyphs, like Å
# and þ), so labels keep their height and the layouts don't move.
keep_metrics() { # file line_height base_line
    sed -i -E "s/(\.line_height = )[0-9]+,/\1$2,/; s/(\.base_line = )[0-9]+,/\1$3,/" "$1"
}
keep_metrics "$OUT/inter_28.c" 36 7
keep_metrics "$OUT/inter_40.c" 49 9

# Big digits for the clock and countdown (Inter Light, as in the emulator).
DIGITS="0123456789:.,-+% °"
for size in 72 96; do
    "${CONV[@]}" --size "$size" --font "$CACHE/Inter-Light.ttf" --symbols "$DIGITS" \
        --lv-font-name "inter_digits_$size" -o "$OUT/inter_digits_$size.c"
done

# Icons, by Material Icons codepoint. Keep in sync with include/ui/icons.h.
ICONS=0xE430,0xE42D,0xE2BD,0xE818,0xF1AD,0xEB3B,0xEBDB,0xEA46,0xF076,0xE798,0xEFD8
ICONS+=,0xE63E,0xE648,0xE1E0,0xEB2F,0xE855,0xE425,0xE1A4,0xE1A3,0xE19C,0xE7F4,0xE002
ICONS+=,0xE86C,0xE000,0xE88A,0xE87D,0xE050,0xE16F
for size in 24 48; do
    "${CONV[@]}" --size "$size" --font "$CACHE/MaterialIcons-Regular.ttf" -r "$ICONS" \
        --lv-font-name "icons_$size" -o "$OUT/icons_$size.c"
done

ls -l "$OUT"
