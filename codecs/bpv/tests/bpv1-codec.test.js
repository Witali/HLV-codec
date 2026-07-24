"use strict";
const assert = require("node:assert/strict");
const codec = require("../src/bpv1-codec.js");

function pattern(values) {
  const out = new Uint8Array(4);
  for (let i = 0; i < 16; i += 1) {
    const value = values[i] & 3;
    const bit = i * 2;
    out[bit >> 3] |= value << (6 - (bit & 7));
  }
  return out;
}
function block(paletteIndex, localColors, values) { return { paletteIndex, localColors, pattern: pattern(values) }; }
const palette = Array.from({ length: 1024 }, (_, i) => ({ r: i & 255, g: 255 - (i & 255), b: (i ^ 0x55) & 255 }));
const a = block(17, [0,1,2,3], [0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3]);
const b = block(42, [4,5,6,7], [0,1,0,1,2,3,2,3,0,1,0,1,2,3,2,3]);
const c = block(63, [8,9,10,11], [0,0,1,1,0,0,1,1,2,2,3,3,2,2,3,3]);
const video = {
  width: 8, height: 4, palette,
  frames: [
    { blocks: [a,b] },
    { blocks: [a,b] },
    { blocks: [b,a] },
    { blocks: [c,a] },
  ],
};
const encoded = codec.encodeVideo(video, { keyframeInterval: 30, searchRadius: 2 });
const decoded = codec.decodeVideo(encoded.bytes);
assert.equal(decoded.frames.length, 4);
assert.equal(decoded.paletteCount, 64);
assert.equal(codec.constants.VERSION, 2);
assert.equal(codec.constants.PALETTE_COUNT, 64);
assert.deepEqual(Array.from(decoded.frames[3].blocks[0].pattern), Array.from(c.pattern));
assert.deepEqual(decoded.frames[3].blocks[0].localColors, c.localColors);
assert.ok(encoded.stats.modeCounts[codec.constants.MODE_SKIP] >= 2);
assert.ok(encoded.stats.modeCounts[codec.constants.MODE_MOTION] >= 2);
assert.ok(encoded.stats.modeCounts[codec.constants.MODE_PATTERN_DICT] >= 1);
assert.equal(codec.renderFrame(decoded, 0).length, 8 * 4 * 4);
console.log("BPV1 tests passed", encoded.stats);

// A tiny hand-built legacy v1 stream verifies backward decoding.
const legacy = [];
const u8 = (v) => legacy.push(v & 255);
const u16 = (v) => legacy.push(v & 255, (v >>> 8) & 255);
const u32 = (v) => legacy.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255);
legacy.push(0x42, 0x50, 0x56, 0x31); // BPV1
u8(1); u16(4); u16(4); u32(1); u16(12); u16(1); u16(24); u16(256); u16(256); u8(2); u8(0);
for (let i = 0; i < 256; i += 1) legacy.push(i, i, i);
u8(1); u32(10); u32(1); u8(0x80); // one RAW mode, MSB-first 100
legacy.push(15, 0, 5, 10, 15, 0x1b, 0x1b, 0xe4, 0xe4);
const legacyDecoded = codec.decodeVideo(Uint8Array.from(legacy));
assert.equal(legacyDecoded.paletteCount, 16);
assert.equal(legacyDecoded.frames[0].blocks[0].paletteIndex, 15);
console.log("BPV1 legacy v1 decode passed");
