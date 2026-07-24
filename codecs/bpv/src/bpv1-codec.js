(function (root, factory) {
  "use strict";
  const api = factory();
  if (typeof module === "object" && module.exports) module.exports = api;
  root.Bpv1Codec = api;
})(typeof self !== "undefined" ? self : globalThis, function () {
  "use strict";

  const MAGIC = [0x42, 0x50, 0x56, 0x31]; // BPV1
  const VERSION = 2;
  const AUDIO_VERSION = 3;
  const LEGACY_VERSION = 1;
  const BLOCK_SIZE = 4;
  const LOCAL_COLORS = 4;
  const PALETTE_COUNT = 64;
  const LEGACY_PALETTE_COUNT = 16;
  const COLORS_PER_PALETTE = 16;
  const PATTERN_BYTES = 4; // 16 * 2 bits

  const MODE_SKIP = 0;
  const MODE_MOTION = 1;
  const MODE_BLOCK_DICT = 2;
  const MODE_PATTERN_DICT = 3;
  const MODE_RAW = 4;

  function encodeVideo(video, options) {
    const settings = options || {};
    validateVideo(video);
    const keyframeInterval = normalizeInt(settings.keyframeInterval, 30, 1, 65535);
    const maxBlockDictionary = normalizeInt(settings.maxBlockDictionary, 256, 1, 65535);
    const maxPatternDictionary = normalizeInt(settings.maxPatternDictionary, 256, 1, 65535);
    const searchRadius = normalizeInt(settings.searchRadius, 4, 0, 127);

    const blocksX = Math.ceil(video.width / BLOCK_SIZE);
    const blocksY = Math.ceil(video.height / BLOCK_SIZE);
    const blockCount = blocksX * blocksY;
    const palette = normalizePalette(video.palette);
    const output = [];

    pushBytes(output, MAGIC);
    pushU8(output, VERSION);
    pushU16(output, video.width);
    pushU16(output, video.height);
    pushU32(output, video.frames.length);
    pushU16(output, video.fpsNumerator || 25);
    pushU16(output, video.fpsDenominator || 1);
    pushU16(output, keyframeInterval);
    pushU16(output, maxBlockDictionary);
    pushU16(output, maxPatternDictionary);
    pushU8(output, searchRadius);
    pushU8(output, 0);
    for (const color of palette) {
      pushU8(output, color.r);
      pushU8(output, color.g);
      pushU8(output, color.b);
    }

    let previousBlocks = null;
    let blockDictionary = [];
    let patternDictionary = [];
    const stats = createStats(video.frames.length, blockCount);

    for (let frameIndex = 0; frameIndex < video.frames.length; frameIndex += 1) {
      const keyframe = frameIndex === 0 || frameIndex % keyframeInterval === 0;
      if (keyframe) {
        previousBlocks = null;
        blockDictionary = [];
        patternDictionary = [];
      }
      const frame = normalizeFrame(video.frames[frameIndex], video.width, video.height, blocksX, blocksY);
      const encodedBlocks = [];
      const modeBytes = [];

      for (let blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        const block = frame.blocks[blockIndex];
        let record = null;

        if (!keyframe && previousBlocks && equalBlock(block, previousBlocks[blockIndex])) {
          record = { mode: MODE_SKIP };
        }

        if (!record && !keyframe && previousBlocks && searchRadius > 0) {
          const motion = findMotion(block, previousBlocks, blockIndex, blocksX, blocksY, searchRadius);
          if (motion) record = { mode: MODE_MOTION, dx: motion.dx, dy: motion.dy };
        }

        if (!record) {
          const blockRef = findExactBlock(blockDictionary, block);
          if (blockRef >= 0) record = { mode: MODE_BLOCK_DICT, index: blockRef };
        }

        if (!record) {
          const patternRef = findExactPattern(patternDictionary, block.pattern);
          if (patternRef >= 0) {
            record = {
              mode: MODE_PATTERN_DICT,
              patternIndex: patternRef,
              paletteIndex: block.paletteIndex,
              localColors: block.localColors,
            };
          }
        }

        if (!record) {
          record = {
            mode: MODE_RAW,
            paletteIndex: block.paletteIndex,
            localColors: block.localColors,
            pattern: block.pattern,
          };
        }

        modeBytes.push(record.mode);
        writeRecord(encodedBlocks, record);
        stats.modeCounts[record.mode] += 1;

        if (record.mode === MODE_RAW) {
          addUniquePattern(patternDictionary, block.pattern, maxPatternDictionary);
          addUniqueBlock(blockDictionary, block, maxBlockDictionary);
        } else if (record.mode === MODE_PATTERN_DICT) {
          addUniqueBlock(blockDictionary, block, maxBlockDictionary);
        }
      }

      const packedModes = packModes(modeBytes);
      pushU8(output, keyframe ? 1 : 0);
      pushU32(output, packedModes.length + encodedBlocks.length);
      pushU32(output, packedModes.length);
      pushBytes(output, packedModes);
      pushBytes(output, encodedBlocks);
      previousBlocks = frame.blocks;
      stats.frameBytes.push(9 + packedModes.length + encodedBlocks.length);
    }

    const bytes = Uint8Array.from(output);
    stats.totalBytes = bytes.length;
    stats.bitsPerPixel = bytes.length * 8 / (video.width * video.height * video.frames.length);
    return { bytes, stats };
  }

  function decodeVideo(input) {
    const bytes = asUint8Array(input);
    let offset = 0;
    for (let i = 0; i < MAGIC.length; i += 1) {
      if (bytes[offset++] !== MAGIC[i]) throw new RangeError("Invalid BPV1 magic");
    }
    const version = readU8(bytes, offset); offset += 1;
    if (version !== AUDIO_VERSION &&
        version !== VERSION &&
        version !== LEGACY_VERSION) {
      throw new RangeError(`Unsupported BPV1 version: ${version}`);
    }
    const paletteCount = version === LEGACY_VERSION ? LEGACY_PALETTE_COUNT : PALETTE_COUNT;
    const width = readU16(bytes, offset); offset += 2;
    const height = readU16(bytes, offset); offset += 2;
    const frameCount = readU32(bytes, offset); offset += 4;
    const fpsNumerator = readU16(bytes, offset); offset += 2;
    const fpsDenominator = readU16(bytes, offset); offset += 2;
    const keyframeInterval = readU16(bytes, offset); offset += 2;
    const maxBlockDictionary = readU16(bytes, offset); offset += 2;
    const maxPatternDictionary = readU16(bytes, offset); offset += 2;
    const searchRadius = readU8(bytes, offset); offset += 1;
    const extension = readU8(bytes, offset); offset += 1;
    let audioCodec = 0;
    let audioSampleRate = 0;
    let audioChannels = 0;
    if (version === AUDIO_VERSION) {
      audioCodec = extension;
      audioSampleRate = readU16(bytes, offset); offset += 2;
      audioChannels = readU8(bytes, offset); offset += 1;
      const reserved = readU8(bytes, offset); offset += 1;
      if (reserved !== 0 ||
          !((audioCodec === 0 && audioSampleRate === 0 &&
             audioChannels === 0) ||
            (audioCodec === 1 && audioSampleRate > 0 &&
             audioChannels === 1))) {
        throw new RangeError("Unsupported BPV1 audio format");
      }
    } else if (extension !== 0) {
      throw new RangeError("Non-zero BPV1 reserved byte");
    }
    const palette = new Array(paletteCount * COLORS_PER_PALETTE);
    for (let i = 0; i < palette.length; i += 1) {
      palette[i] = { r: bytes[offset++], g: bytes[offset++], b: bytes[offset++] };
    }

    const blocksX = Math.ceil(width / BLOCK_SIZE);
    const blocksY = Math.ceil(height / BLOCK_SIZE);
    const blockCount = blocksX * blocksY;
    const frames = [];
    let previousBlocks = null;
    let blockDictionary = [];
    let patternDictionary = [];

    for (let frameIndex = 0; frameIndex < frameCount; frameIndex += 1) {
      const keyframe = readU8(bytes, offset) !== 0; offset += 1;
      const frameBytes = readU32(bytes, offset); offset += 4;
      const modeBytesLength = readU32(bytes, offset); offset += 4;
      const audioBytes = version === AUDIO_VERSION
        ? readU32(bytes, offset)
        : 0;
      if (version === AUDIO_VERSION) offset += 4;
      const frameEnd = offset + frameBytes;
      if (frameEnd > bytes.length) throw new RangeError("Truncated BPV1 frame");
      if (keyframe) {
        previousBlocks = null;
        blockDictionary = [];
        patternDictionary = [];
      }
      const modes = unpackModes(bytes.subarray(offset, offset + modeBytesLength), blockCount);
      offset += modeBytesLength;
      const blocks = new Array(blockCount);

      for (let blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        const mode = modes[blockIndex];
        let block;
        if (mode === MODE_SKIP) {
          if (!previousBlocks) throw new RangeError("SKIP in keyframe");
          block = cloneBlock(previousBlocks[blockIndex]);
        } else if (mode === MODE_MOTION) {
          if (!previousBlocks) throw new RangeError("MOTION in keyframe");
          const dx = readI8(bytes, offset++);
          const dy = readI8(bytes, offset++);
          const bx = blockIndex % blocksX;
          const by = Math.floor(blockIndex / blocksX);
          const sx = bx + dx;
          const sy = by + dy;
          if (sx < 0 || sy < 0 || sx >= blocksX || sy >= blocksY) throw new RangeError("Invalid motion vector");
          block = cloneBlock(previousBlocks[sy * blocksX + sx]);
        } else if (mode === MODE_BLOCK_DICT) {
          const index = readU16(bytes, offset); offset += 2;
          if (index >= blockDictionary.length) throw new RangeError("Invalid block dictionary index");
          block = cloneBlock(blockDictionary[index]);
        } else if (mode === MODE_PATTERN_DICT) {
          const patternIndex = readU16(bytes, offset); offset += 2;
          if (patternIndex >= patternDictionary.length) throw new RangeError("Invalid pattern dictionary index");
          const paletteIndex = readU8(bytes, offset++);
          const localColors = [bytes[offset++], bytes[offset++], bytes[offset++], bytes[offset++]];
          block = createBlock(paletteIndex, localColors, patternDictionary[patternIndex]);
          addUniqueBlock(blockDictionary, block, maxBlockDictionary);
        } else if (mode === MODE_RAW) {
          const paletteIndex = readU8(bytes, offset++);
          const localColors = [bytes[offset++], bytes[offset++], bytes[offset++], bytes[offset++]];
          const pattern = bytes.slice(offset, offset + PATTERN_BYTES); offset += PATTERN_BYTES;
          block = createBlock(paletteIndex, localColors, pattern);
          addUniquePattern(patternDictionary, pattern, maxPatternDictionary);
          addUniqueBlock(blockDictionary, block, maxBlockDictionary);
        } else {
          throw new RangeError(`Invalid BPV1 block mode: ${mode}`);
        }
        blocks[blockIndex] = block;
      }
      if (offset !== frameEnd) throw new RangeError("BPV1 frame size mismatch");
      const audioEnd = offset + audioBytes;
      if (audioEnd > bytes.length) throw new RangeError("Truncated BPV1 audio");
      const audio = bytes.slice(offset, audioEnd);
      offset = audioEnd;
      frames.push({ blocks, audio });
      previousBlocks = blocks;
    }
    if (offset !== bytes.length) throw new RangeError("Trailing BPV1 data");
    return {
      width, height, frames, palette, paletteCount,
      colorsPerPalette: COLORS_PER_PALETTE,
      fpsNumerator, fpsDenominator, keyframeInterval, searchRadius,
      audioCodec, audioSampleRate, audioChannels,
    };
  }

  function renderFrame(decoded, frameIndex) {
    const frame = decoded.frames[frameIndex];
    if (!frame) throw new RangeError("Frame index out of range");
    const out = new Uint8ClampedArray(decoded.width * decoded.height * 4);
    const blocksX = Math.ceil(decoded.width / BLOCK_SIZE);
    for (let y = 0; y < decoded.height; y += 1) {
      for (let x = 0; x < decoded.width; x += 1) {
        const blockIndex = Math.floor(y / BLOCK_SIZE) * blocksX + Math.floor(x / BLOCK_SIZE);
        const block = frame.blocks[blockIndex];
        const localPosition = (y & 3) * 4 + (x & 3);
        const localIndex = read2Bit(block.pattern, localPosition);
        const globalIndex = block.paletteIndex * COLORS_PER_PALETTE + block.localColors[localIndex];
        const color = decoded.palette[globalIndex];
        const o = (y * decoded.width + x) * 4;
        out[o] = color.r; out[o + 1] = color.g; out[o + 2] = color.b; out[o + 3] = 255;
      }
    }
    return out;
  }

  function normalizeFrame(frame, width, height, blocksX, blocksY) {
    if (!frame || !Array.isArray(frame.blocks)) throw new TypeError("Frame must contain blocks");
    if (frame.blocks.length !== blocksX * blocksY) throw new RangeError("Frame block count mismatch");
    return { blocks: frame.blocks.map(normalizeBlock) };
  }

  function normalizeBlock(block) {
    if (!block) throw new TypeError("Missing block");
    const paletteIndex = normalizeInt(block.paletteIndex, 0, 0, PALETTE_COUNT - 1);
    if (!block.localColors || block.localColors.length !== LOCAL_COLORS) throw new RangeError("localColors must have four entries");
    const localColors = Array.from(block.localColors, (value) => normalizeInt(value, 0, 0, COLORS_PER_PALETTE - 1));
    const pattern = asUint8Array(block.pattern);
    if (pattern.length !== PATTERN_BYTES) throw new RangeError("pattern must contain four bytes");
    return createBlock(paletteIndex, localColors, pattern);
  }

  function createBlock(paletteIndex, localColors, pattern) {
    return { paletteIndex, localColors: Array.from(localColors), pattern: Uint8Array.from(pattern) };
  }

  function writeRecord(output, record) {
    if (record.mode === MODE_SKIP) return;
    if (record.mode === MODE_MOTION) { pushI8(output, record.dx); pushI8(output, record.dy); return; }
    if (record.mode === MODE_BLOCK_DICT) { pushU16(output, record.index); return; }
    if (record.mode === MODE_PATTERN_DICT) {
      pushU16(output, record.patternIndex); pushU8(output, record.paletteIndex); pushBytes(output, record.localColors); return;
    }
    if (record.mode === MODE_RAW) {
      pushU8(output, record.paletteIndex); pushBytes(output, record.localColors); pushBytes(output, record.pattern); return;
    }
    throw new RangeError("Unsupported block mode");
  }

  function findMotion(block, previousBlocks, blockIndex, blocksX, blocksY, radius) {
    const bx = blockIndex % blocksX;
    const by = Math.floor(blockIndex / blocksX);
    for (let distance = 1; distance <= radius; distance += 1) {
      for (let dy = -distance; dy <= distance; dy += 1) {
        for (let dx = -distance; dx <= distance; dx += 1) {
          if (Math.max(Math.abs(dx), Math.abs(dy)) !== distance) continue;
          const sx = bx + dx, sy = by + dy;
          if (sx < 0 || sy < 0 || sx >= blocksX || sy >= blocksY) continue;
          if (equalBlock(block, previousBlocks[sy * blocksX + sx])) return { dx, dy };
        }
      }
    }
    return null;
  }

  function findExactBlock(dictionary, block) {
    for (let i = dictionary.length - 1; i >= 0; i -= 1) if (equalBlock(dictionary[i], block)) return i;
    return -1;
  }
  function findExactPattern(dictionary, pattern) {
    for (let i = dictionary.length - 1; i >= 0; i -= 1) if (equalBytes(dictionary[i], pattern)) return i;
    return -1;
  }
  function addUniqueBlock(dictionary, block, limit) {
    if (findExactBlock(dictionary, block) >= 0) return;
    if (dictionary.length >= limit) dictionary.shift();
    dictionary.push(cloneBlock(block));
  }
  function addUniquePattern(dictionary, pattern, limit) {
    if (findExactPattern(dictionary, pattern) >= 0) return;
    if (dictionary.length >= limit) dictionary.shift();
    dictionary.push(Uint8Array.from(pattern));
  }
  function cloneBlock(block) { return createBlock(block.paletteIndex, block.localColors, block.pattern); }
  function equalBlock(a, b) { return a.paletteIndex === b.paletteIndex && equalBytes(a.localColors, b.localColors) && equalBytes(a.pattern, b.pattern); }
  function equalBytes(a, b) { if (!a || !b || a.length !== b.length) return false; for (let i = 0; i < a.length; i += 1) if (a[i] !== b[i]) return false; return true; }

  function packModes(modes) {
    const out = new Uint8Array(Math.ceil(modes.length * 3 / 8));
    for (let i = 0; i < modes.length; i += 1) writeBits(out, i * 3, modes[i], 3);
    return out;
  }
  function unpackModes(bytes, count) {
    const out = new Uint8Array(count);
    for (let i = 0; i < count; i += 1) out[i] = readBits(bytes, i * 3, 3);
    return out;
  }
  function writeBits(bytes, bitOffset, value, bitCount) {
    for (let i = 0; i < bitCount; i += 1) {
      const bit = (value >> (bitCount - i - 1)) & 1;
      const target = bitOffset + i;
      bytes[target >> 3] |= bit << (7 - (target & 7));
    }
  }
  function readBits(bytes, bitOffset, bitCount) {
    let value = 0;
    for (let i = 0; i < bitCount; i += 1) {
      const target = bitOffset + i;
      value = (value << 1) | ((bytes[target >> 3] >> (7 - (target & 7))) & 1);
    }
    return value;
  }
  function read2Bit(pattern, position) { return readBits(pattern, position * 2, 2); }

  function normalizePalette(palette) {
    if (!Array.isArray(palette) || palette.length !== PALETTE_COUNT * COLORS_PER_PALETTE) throw new RangeError("palette must contain 1024 colors (64 palettes × 16 colors)");
    return palette.map((c) => ({ r: normalizeInt(c.r, 0, 0, 255), g: normalizeInt(c.g, 0, 0, 255), b: normalizeInt(c.b, 0, 0, 255) }));
  }
  function validateVideo(video) {
    if (!video || typeof video !== "object") throw new TypeError("Video must be an object");
    normalizeInt(video.width, null, 1, 65535); normalizeInt(video.height, null, 1, 65535);
    if (!Array.isArray(video.frames) || video.frames.length === 0) throw new RangeError("Video must contain frames");
  }
  function createStats(frameCount, blockCount) { return { frameCount, blockCount, modeCounts: [0,0,0,0,0], frameBytes: [], totalBytes: 0, bitsPerPixel: 0 }; }
  function normalizeInt(value, fallback, min, max) { const n = value === undefined || value === null ? fallback : Number(value); if (!Number.isInteger(n) || n < min || n > max) throw new RangeError(`Integer ${min}..${max} required`); return n; }
  function asUint8Array(value) { if (value instanceof Uint8Array) return value; if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength); if (value instanceof ArrayBuffer) return new Uint8Array(value); return Uint8Array.from(value || []); }
  function pushBytes(out, bytes) { for (const b of bytes) out.push(b & 255); }
  function pushU8(out, v) { out.push(v & 255); }
  function pushI8(out, v) { out.push(v & 255); }
  function pushU16(out, v) { out.push(v & 255, (v >>> 8) & 255); }
  function pushU32(out, v) { out.push(v & 255, (v >>> 8) & 255, (v >>> 16) & 255, (v >>> 24) & 255); }
  function readU8(bytes, o) { if (o >= bytes.length) throw new RangeError("Truncated BPV1"); return bytes[o]; }
  function readI8(bytes, o) { const v = readU8(bytes, o); return v >= 128 ? v - 256 : v; }
  function readU16(bytes, o) { if (o + 2 > bytes.length) throw new RangeError("Truncated BPV1"); return bytes[o] | bytes[o+1] << 8; }
  function readU32(bytes, o) { if (o + 4 > bytes.length) throw new RangeError("Truncated BPV1"); return (bytes[o] | bytes[o+1] << 8 | bytes[o+2] << 16 | bytes[o+3] << 24) >>> 0; }

  return { encodeVideo, decodeVideo, renderFrame, constants: { VERSION, AUDIO_VERSION, LEGACY_VERSION, MODE_SKIP, MODE_MOTION, MODE_BLOCK_DICT, MODE_PATTERN_DICT, MODE_RAW, BLOCK_SIZE, LOCAL_COLORS, PALETTE_COUNT, COLORS_PER_PALETTE } };
});
