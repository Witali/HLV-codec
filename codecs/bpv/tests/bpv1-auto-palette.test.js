"use strict";
const assert = require("node:assert/strict");
const codec = require("../src/bpv1-codec.js");
const auto = require("../src/bpv1-auto-palette.js");

function frame(width, height, left, right, shift = 0) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const source = ((x + shift) % width) < width / 2 ? left : right;
      const o = (y * width + x) * 4;
      rgba[o] = source[0]; rgba[o + 1] = source[1]; rgba[o + 2] = source[2]; rgba[o + 3] = 255;
    }
  }
  return rgba;
}

const video = {
  width: 8,
  height: 8,
  fpsNumerator: 12,
  frames: [
    frame(8, 8, [225, 30, 25], [25, 55, 220], 0),
    frame(8, 8, [225, 30, 25], [25, 55, 220], 4),
    frame(8, 8, [225, 30, 25], [25, 55, 220], 4),
  ],
};
const encoded = auto.compressRgbaVideo(video, {
  maximumSampleBlocks: 64,
  maximumPixelsPerCluster: 256,
  blockClusterIterations: 4,
  colorClusterIterations: 4,
  keyframeInterval: 12,
  searchRadius: 2,
});
assert.equal(encoded.palette.length, 1024);
assert.equal(encoded.training.colorSpace, "oklab");
assert.equal(encoded.frames.length, 3);
assert.ok(encoded.stats.modeCounts[codec.constants.MODE_SKIP] > 0);
assert.ok(encoded.stats.modeCounts[codec.constants.MODE_MOTION] > 0);
const decoded = codec.decodeVideo(encoded.bytes);
assert.equal(decoded.paletteCount, 64);
assert.equal(codec.renderFrame(decoded, 0).length, 8 * 8 * 4);

const green = auto.buildAutomaticPalettes({
  width: 4,
  height: 4,
  fpsNumerator: 1,
  fpsDenominator: 1,
  frames: [frame(4, 4, [15, 220, 35], [15, 220, 35])],
}, { maximumSampleBlocks: 64, maximumPixelsPerCluster: 256, blockClusterIterations: 2, colorClusterIterations: 2 });
const average = green.palette.reduce((sum, color) => [sum[0] + color.r, sum[1] + color.g, sum[2] + color.b], [0, 0, 0]).map((value) => value / green.palette.length);
assert.ok(average[1] > average[0] * 3);
assert.ok(average[1] > average[2] * 3);
console.log("BPV1 automatic palette tests passed", encoded.stats);
