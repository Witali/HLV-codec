"use strict";
const assert = require("node:assert/strict");
const codec = require("../src/bpv1-codec.js");
const rd = require("../src/bpv1-rate-distortion.js");

function frame(width, height, shift, noise) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const o = (y * width + x) * 4;
      const q = ((x + shift) >> 2) & 1;
      rgba[o] = q ? 205 + noise : 35;
      rgba[o + 1] = q ? 75 : 145 + noise;
      rgba[o + 2] = q ? 45 : 215;
      rgba[o + 3] = 255;
    }
  }
  return rgba;
}

const video = {
  width: 16,
  height: 8,
  fpsNumerator: 12,
  frames: [
    frame(16, 8, 0, 0),
    frame(16, 8, 4, 0),
    frame(16, 8, 4, 2),
    frame(16, 8, 4, 3),
  ],
};
const common = {
  maximumSampleBlocks: 64,
  maximumPixelsPerCluster: 256,
  blockClusterIterations: 3,
  colorClusterIterations: 3,
  candidatePaletteCount: 3,
  keyframeInterval: 12,
  searchRadius: 2,
};
const losslessRateChoice = rd.compressRgbaVideoRateDistortion(video, { ...common, lambda: 0 });
const rateBiased = rd.compressRgbaVideoRateDistortion(video, { ...common, lambda: 64 });
assert.equal(rateBiased.rateDistortion.lambda, 64);
assert.equal(rateBiased.rateDistortion.candidatePaletteCount, 3);
assert.ok(Number.isFinite(rateBiased.rateDistortion.psnrDb));
assert.ok(rateBiased.bytes.length <= losslessRateChoice.bytes.length);
const decoded = codec.decodeVideo(rateBiased.bytes);
assert.equal(decoded.frames.length, video.frames.length);
assert.equal(codec.renderFrame(decoded, 0).length, video.width * video.height * 4);
console.log("BPV1 rate-distortion tests passed", {
  lambda0: losslessRateChoice.bytes.length,
  lambda64: rateBiased.bytes.length,
  psnr: rateBiased.rateDistortion.psnrDb,
});
