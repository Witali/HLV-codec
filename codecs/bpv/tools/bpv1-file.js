"use strict";

const MAGIC = [0x42, 0x50, 0x56, 0x31];
const VERSION = 2;
const AUDIO_VERSION = 3;
const ACTIVE_PALETTE_VERSION = 4;
const LEGACY_VERSION = 1;
const AUDIO_NONE = 0;
const AUDIO_PCM_U8 = 1;
const PALETTE_COUNT = 64;
const LEGACY_PALETTE_COUNT = 16;
const COLORS_PER_PALETTE = 16;
const BLOCK_SIZE = 4;
const RECORD_BYTES = 9;
const PATTERN_OFFSET = 5;

const MODE_SKIP = 0;
const MODE_MOTION = 1;
const MODE_BLOCK_DICT = 2;
const MODE_PATTERN_DICT = 3;
const MODE_RAW = 4;
const MODE_NAMES = ["skip", "motion", "blockDictionary", "patternDictionary", "raw"];

function asUint8Array(value) {
  if (value instanceof Uint8Array) return value;
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  return Uint8Array.from(value || []);
}

function parseHeader(input) {
  const bytes = asUint8Array(input);
  let offset = 0;
  for (const byte of MAGIC) {
    if (readU8(bytes, offset++) !== byte) throw new RangeError("Invalid BPV1 magic");
  }
  const version = readU8(bytes, offset); offset += 1;
  if (version !== ACTIVE_PALETTE_VERSION &&
      version !== AUDIO_VERSION &&
      version !== VERSION &&
      version !== LEGACY_VERSION) {
    throw new RangeError(`Unsupported BPV1 version: ${version}`);
  }
  const paletteCount = version === LEGACY_VERSION
    ? LEGACY_PALETTE_COUNT
    : PALETTE_COUNT;
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
  let audioCodec = AUDIO_NONE;
  let audioSampleRate = 0;
  let audioChannels = 0;
  let reserved = extension;
  if (version >= AUDIO_VERSION) {
    audioCodec = extension;
    audioSampleRate = readU16(bytes, offset); offset += 2;
    audioChannels = readU8(bytes, offset); offset += 1;
    reserved = readU8(bytes, offset); offset += 1;
    if (!((audioCodec === AUDIO_NONE &&
           audioSampleRate === 0 && audioChannels === 0) ||
          (audioCodec === AUDIO_PCM_U8 &&
           audioSampleRate > 0 && audioChannels === 1))) {
      throw new RangeError("Unsupported BPV1 audio format");
    }
  }
  if (reserved !== 0) throw new RangeError("Non-zero BPV1 reserved byte");
  if (width === 0 || height === 0) throw new RangeError("BPV1 dimensions must be non-zero");
  if (frameCount === 0) throw new RangeError("BPV1 stream contains no frames");
  if (fpsNumerator === 0 || fpsDenominator === 0) {
    throw new RangeError("BPV1 frame rate must be non-zero");
  }
  if (keyframeInterval === 0 ||
      maxBlockDictionary === 0 ||
      maxPatternDictionary === 0) {
    throw new RangeError("Invalid zero BPV1 coding parameter");
  }
  const paletteBytes = paletteCount * COLORS_PER_PALETTE * 3;
  let palette = new Uint8Array(paletteBytes);
  if (version !== ACTIVE_PALETTE_VERSION) {
    requireBytes(bytes, offset, paletteBytes, "palette");
    palette = bytes.slice(offset, offset + paletteBytes);
    offset += paletteBytes;
  }
  return {
    bytes,
    version,
    width,
    height,
    frameCount,
    fpsNumerator,
    fpsDenominator,
    keyframeInterval,
    maxBlockDictionary,
    maxPatternDictionary,
    searchRadius,
    reserved,
    audioCodec,
    audioSampleRate,
    audioChannels,
    paletteCount,
    colorsPerPalette: COLORS_PER_PALETTE,
    palette,
    frameDataOffset: offset,
  };
}

function walkFrames(input, onFrame) {
  const header = parseHeader(input);
  const bytes = header.bytes;
  const blocksX = Math.ceil(header.width / BLOCK_SIZE);
  const blocksY = Math.ceil(header.height / BLOCK_SIZE);
  const blockCount = blocksX * blocksY;
  const requiredModeBytes = Math.ceil(blockCount * 3 / 8);
  let previous = null;
  let blockDictionary = [];
  let patternDictionary = [];
  let offset = header.frameDataOffset;
  const modeCounts = new Array(MODE_NAMES.length).fill(0);
  let minimumFrameBytes = Infinity;
  let maximumFrameBytes = 0;
  let totalFrameBytes = 0;
  let totalAudioBytes = 0;
  let keyframes = 0;
  let paletteUpdates = 0;
  let activePalette = header.palette;

  for (let frameIndex = 0; frameIndex < header.frameCount; frameIndex += 1) {
    const frameHeaderBytes = header.version >= AUDIO_VERSION ? 13 : 9;
    requireBytes(bytes, offset, frameHeaderBytes, `frame ${frameIndex} header`);
    const keyframe = readU8(bytes, offset) !== 0; offset += 1;
    const frameBytes = readU32(bytes, offset); offset += 4;
    const modeBytesLength = readU32(bytes, offset); offset += 4;
    const audioBytes = header.version >= AUDIO_VERSION
      ? readU32(bytes, offset)
      : 0;
    if (header.version >= AUDIO_VERSION) offset += 4;
    const maximumAudioBytes = header.audioCodec === AUDIO_PCM_U8
      ? Math.ceil(
        header.audioSampleRate * header.fpsDenominator /
        header.fpsNumerator,
      ) * header.audioChannels
      : 0;
    if (audioBytes > maximumAudioBytes) {
      throw new RangeError(`Invalid BPV1 audio length in frame ${frameIndex}`);
    }
    const frameStart = offset;
    const frameEnd = frameStart + frameBytes;
    if (frameEnd > bytes.length || frameEnd < frameStart) {
      throw new RangeError(`Truncated BPV1 frame ${frameIndex}`);
    }
    if (modeBytesLength !== requiredModeBytes) {
      throw new RangeError(
        `Unexpected BPV1 mode-map length in frame ${frameIndex}: ` +
        `${modeBytesLength}, expected ${requiredModeBytes}`,
      );
    }
    if (modeBytesLength > frameBytes) {
      throw new RangeError(`Mode map exceeds BPV1 frame ${frameIndex}`);
    }
    if (frameIndex === 0 && !keyframe) {
      throw new RangeError("The first BPV1 frame must be a keyframe");
    }
    if (header.version === ACTIVE_PALETTE_VERSION && keyframe) {
      const paletteBytes =
        header.paletteCount * COLORS_PER_PALETTE * 3;
      requireFrameBytes(offset, paletteBytes, frameEnd, frameIndex);
      activePalette = bytes.slice(offset, offset + paletteBytes);
      header.palette = activePalette;
      offset += paletteBytes;
      paletteUpdates += 1;
    }
    if (modeBytesLength > frameEnd - offset) {
      throw new RangeError(`Mode map exceeds BPV1 frame ${frameIndex}`);
    }
    const modes = bytes.subarray(offset, offset + modeBytesLength);
    offset += modeBytesLength;

    if (keyframe) {
      previous = null;
      blockDictionary = [];
      patternDictionary = [];
      keyframes += 1;
    }
    const blocks = new Uint8Array(blockCount * RECORD_BYTES);

    for (let blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
      const mode = readBits(modes, blockIndex * 3, 3);
      if (mode >= MODE_NAMES.length) {
        throw new RangeError(`Invalid BPV1 mode ${mode} at ${frameIndex}:${blockIndex}`);
      }
      modeCounts[mode] += 1;
      const destination = blockIndex * RECORD_BYTES;

      if (mode === MODE_SKIP) {
        if (!previous) throw new RangeError(`SKIP without reference at ${frameIndex}:${blockIndex}`);
        copyRecord(previous, destination, blocks, destination);
      } else if (mode === MODE_MOTION) {
        if (!previous) throw new RangeError(`MOTION without reference at ${frameIndex}:${blockIndex}`);
        requireFrameBytes(offset, 2, frameEnd, frameIndex);
        const dx = readI8(bytes, offset);
        const dy = readI8(bytes, offset + 1);
        offset += 2;
        const bx = blockIndex % blocksX;
        const by = Math.floor(blockIndex / blocksX);
        const sourceX = bx + dx;
        const sourceY = by + dy;
        if (sourceX < 0 || sourceY < 0 || sourceX >= blocksX || sourceY >= blocksY) {
          throw new RangeError(`Invalid motion vector at ${frameIndex}:${blockIndex}`);
        }
        copyRecord(
          previous,
          (sourceY * blocksX + sourceX) * RECORD_BYTES,
          blocks,
          destination,
        );
      } else if (mode === MODE_BLOCK_DICT) {
        requireFrameBytes(offset, 2, frameEnd, frameIndex);
        const index = readU16(bytes, offset);
        offset += 2;
        if (index >= blockDictionary.length) {
          throw new RangeError(`Invalid block dictionary index at ${frameIndex}:${blockIndex}`);
        }
        blocks.set(blockDictionary[index], destination);
      } else if (mode === MODE_PATTERN_DICT) {
        requireFrameBytes(offset, 7, frameEnd, frameIndex);
        const patternIndex = readU16(bytes, offset);
        offset += 2;
        if (patternIndex >= patternDictionary.length) {
          throw new RangeError(`Invalid pattern dictionary index at ${frameIndex}:${blockIndex}`);
        }
        blocks.set(bytes.subarray(offset, offset + PATTERN_OFFSET), destination);
        offset += PATTERN_OFFSET;
        blocks.set(patternDictionary[patternIndex], destination + PATTERN_OFFSET);
        validateRecord(blocks, destination, header.paletteCount, frameIndex, blockIndex);
        addUnique(
          blockDictionary,
          blocks.subarray(destination, destination + RECORD_BYTES),
          header.maxBlockDictionary,
        );
      } else if (mode === MODE_RAW) {
        requireFrameBytes(offset, RECORD_BYTES, frameEnd, frameIndex);
        blocks.set(bytes.subarray(offset, offset + RECORD_BYTES), destination);
        offset += RECORD_BYTES;
        validateRecord(blocks, destination, header.paletteCount, frameIndex, blockIndex);
        addUnique(
          patternDictionary,
          blocks.subarray(destination + PATTERN_OFFSET, destination + RECORD_BYTES),
          header.maxPatternDictionary,
        );
        addUnique(
          blockDictionary,
          blocks.subarray(destination, destination + RECORD_BYTES),
          header.maxBlockDictionary,
        );
      }
    }
    if (offset !== frameEnd) {
      throw new RangeError(
        `BPV1 frame ${frameIndex} size mismatch: decoded ${offset - frameStart}, ` +
        `declared ${frameBytes}`,
      );
    }
    const audioStart = frameEnd;
    const audioEnd = audioStart + audioBytes;
    if (audioEnd > bytes.length || audioEnd < audioStart) {
      throw new RangeError(`Truncated BPV1 audio in frame ${frameIndex}`);
    }
    const audio = bytes.subarray(audioStart, audioEnd);
    offset = audioEnd;
    const storedFrameBytes = frameHeaderBytes + frameBytes + audioBytes;
    minimumFrameBytes = Math.min(minimumFrameBytes, storedFrameBytes);
    maximumFrameBytes = Math.max(maximumFrameBytes, storedFrameBytes);
    totalFrameBytes += storedFrameBytes;
    totalAudioBytes += audioBytes;
    if (typeof onFrame === "function") {
      onFrame({
        blocks,
        frameIndex,
        keyframe,
        frameBytes: storedFrameBytes,
        videoBytes: frameBytes,
        audio,
        audioBytes,
        palette: activePalette,
      }, header);
    }
    previous = blocks;
  }
  if (offset !== bytes.length) {
    throw new RangeError(`Trailing BPV1 data: ${bytes.length - offset} bytes`);
  }

  return {
    version: header.version,
    width: header.width,
    height: header.height,
    frameCount: header.frameCount,
    fpsNumerator: header.fpsNumerator,
    fpsDenominator: header.fpsDenominator,
    durationSeconds: header.frameCount * header.fpsDenominator / header.fpsNumerator,
    keyframeInterval: header.keyframeInterval,
    keyframes,
    paletteUpdates,
    maxBlockDictionary: header.maxBlockDictionary,
    maxPatternDictionary: header.maxPatternDictionary,
    searchRadius: header.searchRadius,
    paletteCount: header.paletteCount,
    audioCodec: header.audioCodec,
    audioSampleRate: header.audioSampleRate,
    audioChannels: header.audioChannels,
    audioBytes: totalAudioBytes,
    blockCount,
    modeCounts: Object.fromEntries(MODE_NAMES.map((name, index) => [name, modeCounts[index]])),
    minimumFrameBytes,
    maximumFrameBytes,
    meanFrameBytes: totalFrameBytes / header.frameCount,
    fileBytes: bytes.length,
  };
}

function renderFrameRgba(frame, header) {
  const blocks = frame.blocks || frame;
  const expectedBlocks = Math.ceil(header.width / BLOCK_SIZE) *
    Math.ceil(header.height / BLOCK_SIZE);
  if (!(blocks instanceof Uint8Array) || blocks.length !== expectedBlocks * RECORD_BYTES) {
    throw new RangeError("Packed BPV1 frame has an invalid length");
  }
  const rgba = new Uint8ClampedArray(header.width * header.height * 4);
  const blocksX = Math.ceil(header.width / BLOCK_SIZE);
  for (let y = 0; y < header.height; y += 1) {
    for (let x = 0; x < header.width; x += 1) {
      const blockIndex = Math.floor(y / BLOCK_SIZE) * blocksX + Math.floor(x / BLOCK_SIZE);
      const record = blockIndex * RECORD_BYTES;
      const pixel = (y & 3) * BLOCK_SIZE + (x & 3);
      const local = (blocks[record + PATTERN_OFFSET + (pixel >> 2)] >>
        (6 - ((pixel & 3) << 1))) & 3;
      const colorIndex = blocks[record] * COLORS_PER_PALETTE +
        blocks[record + 1 + local];
      const color = colorIndex * 3;
      const output = (y * header.width + x) * 4;
      const palette = frame.palette || header.palette;
      rgba[output] = palette[color];
      rgba[output + 1] = palette[color + 1];
      rgba[output + 2] = palette[color + 2];
      rgba[output + 3] = 255;
    }
  }
  return rgba;
}

function validateRecord(bytes, offset, paletteCount, frameIndex, blockIndex) {
  if (bytes[offset] >= paletteCount) {
    throw new RangeError(`Invalid palette index at ${frameIndex}:${blockIndex}`);
  }
  for (let index = 1; index <= 4; index += 1) {
    if (bytes[offset + index] >= COLORS_PER_PALETTE) {
      throw new RangeError(`Invalid local color at ${frameIndex}:${blockIndex}`);
    }
  }
}

function addUnique(dictionary, value, limit) {
  for (const entry of dictionary) {
    if (equalBytes(entry, value)) return;
  }
  if (dictionary.length >= limit) dictionary.shift();
  dictionary.push(Uint8Array.from(value));
}

function copyRecord(source, sourceOffset, destination, destinationOffset) {
  destination.set(
    source.subarray(sourceOffset, sourceOffset + RECORD_BYTES),
    destinationOffset,
  );
}

function equalBytes(left, right) {
  if (left.length !== right.length) return false;
  for (let index = 0; index < left.length; index += 1) {
    if (left[index] !== right[index]) return false;
  }
  return true;
}

function readBits(bytes, bitOffset, bitCount) {
  let value = 0;
  for (let index = 0; index < bitCount; index += 1) {
    const target = bitOffset + index;
    value = (value << 1) |
      ((bytes[target >> 3] >> (7 - (target & 7))) & 1);
  }
  return value;
}

function requireFrameBytes(offset, length, frameEnd, frameIndex) {
  if (offset + length > frameEnd || offset + length < offset) {
    throw new RangeError(`Truncated BPV1 payload in frame ${frameIndex}`);
  }
}

function requireBytes(bytes, offset, length, label) {
  if (offset + length > bytes.length || offset + length < offset) {
    throw new RangeError(`Truncated BPV1 ${label}`);
  }
}

function readU8(bytes, offset) {
  requireBytes(bytes, offset, 1, "byte");
  return bytes[offset];
}

function readI8(bytes, offset) {
  const value = readU8(bytes, offset);
  return value >= 128 ? value - 256 : value;
}

function readU16(bytes, offset) {
  requireBytes(bytes, offset, 2, "u16");
  return bytes[offset] | (bytes[offset + 1] << 8);
}

function readU32(bytes, offset) {
  requireBytes(bytes, offset, 4, "u32");
  return (
    bytes[offset] |
    (bytes[offset + 1] << 8) |
    (bytes[offset + 2] << 16) |
    (bytes[offset + 3] << 24)
  ) >>> 0;
}

module.exports = {
  parseHeader,
  renderFrameRgba,
  walkFrames,
  constants: {
    AUDIO_NONE,
    AUDIO_PCM_U8,
    AUDIO_VERSION,
    ACTIVE_PALETTE_VERSION,
    BLOCK_SIZE,
    COLORS_PER_PALETTE,
    LEGACY_VERSION,
    MODE_BLOCK_DICT,
    MODE_MOTION,
    MODE_PATTERN_DICT,
    MODE_RAW,
    MODE_SKIP,
    PALETTE_COUNT,
    RECORD_BYTES,
    VERSION,
  },
};
