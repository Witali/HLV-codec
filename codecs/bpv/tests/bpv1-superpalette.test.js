"use strict";

const assert = require("node:assert/strict");
const superpalette = require("../src/bpv1-superpalette.js");

const { ACTIVE_PALETTES, COLORS_PER_PALETTE, PALETTE_BYTES } =
  superpalette.constants;

function palette(seed, unusedOffset = 0) {
  const result = new Uint8Array(PALETTE_BYTES);
  for (let color = 0; color < COLORS_PER_PALETTE; color += 1) {
    const offset = color * 3;
    result[offset] = (seed + color * 7) & 255;
    result[offset + 1] = (seed * 3 + color * 11) & 255;
    result[offset + 2] = (seed * 5 + color * 13) & 255;
  }
  result[PALETTE_BYTES - 1] =
    (result[PALETTE_BYTES - 1] + unusedOffset) & 255;
  return result;
}

function source(gopIndex, slot, value, uses, unusedOffset = 0) {
  const colorUses = new Float64Array(COLORS_PER_PALETTE);
  colorUses[0] = uses;
  return {
    gopIndex,
    slot,
    palette: palette(value, unusedOffset),
    colorUses,
    pixelUses: uses,
    key: Buffer.from(palette(value, unusedOffset)).toString("hex"),
  };
}

const sources = [];
for (let gop = 0; gop < 2; gop += 1) {
  for (let slot = 0; slot < ACTIVE_PALETTES; slot += 1) {
    const group = slot < 48 ? 20 : 180;
    sources.push(source(gop, slot, group, 100 + slot, gop));
  }
}
const statistics = {
  fileBytes: 100000,
  activePaletteBytes: 2 * ACTIVE_PALETTES * PALETTE_BYTES,
  paletteUpdates: 2,
  totalRgbSamples: sources.reduce(
    (total, entry) => total + entry.pixelUses * 3,
    0,
  ),
  sources,
};

const first = superpalette.buildSuperpalette(statistics, 2);
const second = superpalette.buildSuperpalette(statistics, 2);
assert.deepEqual(
  first.map((entry) => Array.from(entry)),
  second.map((entry) => Array.from(entry)),
  "superpalette selection must be deterministic",
);

const evaluation = superpalette.evaluateSuperpalette(
  statistics,
  first,
  [60, 40],
);
assert.equal(evaluation.bankSize, 2);
assert.equal(evaluation.variants[0].targetPsnrDb, "exact");
assert.equal(evaluation.variants[0].literalPalettes, 0);
assert.equal(evaluation.variants[0].rgbSse, 0);
assert.equal(evaluation.variants[0].perturbationPsnrDb, "infinite");
assert.ok(evaluation.variants[0].paletteBytesSaved > 0);
assert.ok(evaluation.variants[0].estimatedFileBytes < statistics.fileBytes);
assert.equal(
  evaluation.variants[0].paletteSectionBytes,
  2 * PALETTE_BYTES + 2 * 8 + 2 * 8,
);

console.log("BPV1 superpalette analysis tests passed");
