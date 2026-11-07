// Mit csinál: egy SVG márkalogóból fehér, átlátszósági maszkos (A8) LVGL-képet készít C forrásként.
// Hogyan:     az SVG-t fehér háttérre rajzolja (resvg), a világosságot megfordítja (sötét = átlátszatlan),
//             a kontrasztot kissé növeli, és a kimenetet `lv_image_dsc_t`-ként írja ki. A kép színét
//             a kijelzőn az LVGL színezi (lv_obj_set_style_image_recolor), tehát fehér, vagy bármely paletta-szín lehet.
// Miért:      az LVGL nem rajzol SVG-t, a színes/színátmenetes logók pedig egyszínű alakzattá egyszerűsödnek.
// Használat:  node convert_logo.cjs src/opel.svg og_logo.c 200   (a harmadik argumentum a kép szélessége pixelben)
const fs = require('fs');
const { Resvg } = require('@resvg/resvg-js');

const [, , input, output, widthArg] = process.argv;
if (!input || !output) {
    console.error('Használat: node convert_logo.cjs <be.svg> <ki.c> [szélesség]');
    process.exit(1);
}
const width = parseInt(widthArg || '200', 10);

const svg = fs.readFileSync(input);
const img = new Resvg(svg, { fitTo: { mode: 'width', value: width }, background: '#ffffff' }).render();
const w = img.width, h = img.height, px = img.pixels;  // RGBA, sorfolytonos

const a8 = Buffer.alloc(w * h);
for (let i = 0; i < w * h; ++i) {
    const lum = (0.299 * px[4 * i] + 0.587 * px[4 * i + 1] + 0.114 * px[4 * i + 2]) / 255;
    const dark = 1 - lum;                        // 0 = fehér háttér, 1 = fekete
    a8[i] = Math.round(255 * Math.min(1, Math.pow(dark * 1.25, 0.8)));
}

const name = require('path').basename(output, '.c');
const rows = [];
for (let i = 0; i < a8.length; i += 24) rows.push('    ' + Array.from(a8.subarray(i, i + 24), b => '0x' + b.toString(16).padStart(2, '0')).join(', ') + ',');
const text = `// Generálta: convert_logo.cjs (${require('path').basename(input)}, ${w}x${h}, A8). Ne szerkeszd kézzel.
// A logó a márkatulajdonos védjegye; azonosítási célra szerepel (lásd README.md).
#include "lvgl.h"

static const uint8_t ${name}_map[] = {
${rows.join('\n')}
};

const lv_image_dsc_t ${name} = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_A8,
        .flags = 0,
        .w = ${w},
        .h = ${h},
        .stride = ${w},
        .reserved_2 = 0,
    },
    .data_size = sizeof(${name}_map),
    .data = ${name}_map,
};
`;
fs.writeFileSync(output, text);
console.log(`${output}: ${w}x${h}, ${a8.length} bájt`);
