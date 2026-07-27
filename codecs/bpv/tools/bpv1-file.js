"use strict";

const MAGIC = [0x42, 0x50, 0x56, 0x31];
const VERSION = 7;
const PIXEL_MOTION_VERSION = 7;
const FOUR_MODE_VERSION = 6;
const ADAPTIVE_RAW_VERSION = 5;
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
const DIRECT_RECORD_FLAG = 0x80;
const PATTERN_BYTES = 4;
const PATTERN_OFFSET = 5;

const MODE_SKIP = 0;
const MODE_MOTION = 1;
const MODE_BLOCK_DICT = 2;
const MODE_PATTERN_DICT = 3;
const MODE_RAW = 4;
const MODE_RAW_DIRECT = 5;
const MODE_NAMES = [
  "skip", "motion", "blockDictionary", "patternDictionary", "raw",
  "rawDirect",
];

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
  if (version < LEGACY_VERSION || version > VERSION) {
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
      (version >= PIXEL_MOTION_VERSION &&
       (width % BLOCK_SIZE !== 0 || height % BLOCK_SIZE !== 0)) ||
      (version >= FOUR_MODE_VERSION
        ? maxPatternDictionary !== 0 || searchRadius > 7
        : maxPatternDictionary === 0)) {
    throw new RangeError("Invalid zero BPV1 coding parameter");
  }
  const paletteBytes = paletteCount * COLORS_PER_PALETTE * 3;
  let palette = new Uint8Array(paletteBytes);
  if (version < ACTIVE_PALETTE_VERSION) {
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
  const modeBits = header.version >= FOUR_MODE_VERSION ? 2 : 3;
  const requiredModeBytes = Math.ceil(blockCount * modeBits / 8);
  let previous = null;
  let previousPixels = null;
  let blockDictionary = [];
  let patternDictionary = [];
  let offset = header.frameDataOffset;
  const modeCounts = new Array(MODE_NAMES.length).fill(0);
  const rawColorCounts = [0, 0, 0, 0];
  const rawDirectColorCounts = new Array(17).fill(0);
  const patternDictionaryColorCounts = [0, 0, 0, 0];
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
    if (header.version >= ACTIVE_PALETTE_VERSION && keyframe) {
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
      previousPixels = null;
      blockDictionary = [];
      patternDictionary = [];
      keyframes += 1;
    }
    const blocks = new Uint8Array(blockCount * RECORD_BYTES);
    const pixels = header.version >= PIXEL_MOTION_VERSION
      ? new Uint16Array(header.width * header.height)
      : null;

    for (let blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
      const mode = readBits(modes, blockIndex * modeBits, modeBits);
      const modeCount = header.version >= FOUR_MODE_VERSION
        ? 4
        : MODE_NAMES.length;
      if (mode >= modeCount) {
        throw new RangeError(`Invalid BPV1 mode ${mode} at ${frameIndex}:${blockIndex}`);
      }
      modeCounts[mode] += 1;
      const destination = blockIndex * RECORD_BYTES;

      if (mode === MODE_SKIP) {
        if (header.version >= PIXEL_MOTION_VERSION) {
          if (!previousPixels ||
              !copyPixelRegion(
                previousPixels, pixels, header.width, header.height,
                blockIndex, blocksX, 0, 0,
              )) {
            throw new RangeError(
              `SKIP without reference at ${frameIndex}:${blockIndex}`,
            );
          }
        } else {
          if (!previous) throw new RangeError(`SKIP without reference at ${frameIndex}:${blockIndex}`);
          copyRecord(previous, destination, blocks, destination);
        }
      } else if (mode === MODE_MOTION) {
        if (header.version >= PIXEL_MOTION_VERSION
            ? !previousPixels
            : !previous) {
          throw new RangeError(
            `MOTION without reference at ${frameIndex}:${blockIndex}`,
          );
        }
        const motionBytes = header.version >= FOUR_MODE_VERSION ? 1 : 2;
        requireFrameBytes(offset, motionBytes, frameEnd, frameIndex);
        const dx = motionBytes === 1
          ? readSignedNibble(bytes[offset] >>> 4)
          : readI8(bytes, offset);
        const dy = motionBytes === 1
          ? readSignedNibble(bytes[offset] & 15)
          : readI8(bytes, offset + 1);
        if (motionBytes === 1 && (dx === -8 || dy === -8)) {
          throw new RangeError(
            `Invalid packed motion vector at ${frameIndex}:${blockIndex}`,
          );
        }
        offset += motionBytes;
        const bx = blockIndex % blocksX;
        const by = Math.floor(blockIndex / blocksX);
        if (header.version >= PIXEL_MOTION_VERSION) {
          if (!copyPixelRegion(
            previousPixels, pixels, header.width, header.height,
            blockIndex, blocksX, dx, dy,
          )) {
            throw new RangeError(
              `Invalid pixel motion vector at ${frameIndex}:${blockIndex}`,
            );
          }
        } else {
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
        }
      } else if (mode === MODE_BLOCK_DICT) {
        requireFrameBytes(offset, 2, frameEnd, frameIndex);
        const index = readU16(bytes, offset);
        offset += 2;
        if (index >= blockDictionary.length) {
          throw new RangeError(`Invalid block dictionary index at ${frameIndex}:${blockIndex}`);
        }
        blocks.set(blockDictionary[index], destination);
      } else if (header.version >= FOUR_MODE_VERSION &&
                 mode === MODE_PATTERN_DICT) {
        const decoded = readV6Raw(
          bytes,
          offset,
          frameEnd,
          frameIndex,
          header.paletteCount,
        );
        blocks.set(decoded.record, destination);
        offset = decoded.offset;
        if (decoded.direct) {
          rawDirectColorCounts[decoded.count] += 1;
        } else {
          rawColorCounts[decoded.count - 1] += 1;
        }
        addUnique(
          blockDictionary,
          blocks.subarray(destination, destination + RECORD_BYTES),
          header.maxBlockDictionary,
        );
      } else if (mode === MODE_PATTERN_DICT) {
        requireFrameBytes(offset, 2, frameEnd, frameIndex);
        const patternIndex = readU16(bytes, offset);
        offset += 2;
        if (patternIndex >= patternDictionary.length) {
          throw new RangeError(`Invalid pattern dictionary index at ${frameIndex}:${blockIndex}`);
        }
        if (header.version >= ADAPTIVE_RAW_VERSION) {
          const count = patternColorCount(patternDictionary[patternIndex]);
          const prefix = readPackedPrefix(
            bytes,
            offset,
            frameEnd,
            count,
            frameIndex,
          );
          blocks.set(prefix.recordPrefix, destination);
          offset = prefix.offset;
          patternDictionaryColorCounts[prefix.count - 1] += 1;
        } else {
          requireFrameBytes(
            offset,
            PATTERN_OFFSET,
            frameEnd,
            frameIndex,
          );
          blocks.set(
            bytes.subarray(offset, offset + PATTERN_OFFSET),
            destination,
          );
          offset += PATTERN_OFFSET;
        }
        blocks.set(
          patternDictionary[patternIndex],
          destination + PATTERN_OFFSET,
        );
        validateRecord(blocks, destination, header.paletteCount, frameIndex, blockIndex);
        addUnique(
          blockDictionary,
          blocks.subarray(destination, destination + RECORD_BYTES),
          header.maxBlockDictionary,
        );
      } else if (mode === MODE_RAW) {
        if (header.version >= ADAPTIVE_RAW_VERSION) {
          const prefix = readPackedPrefix(
            bytes,
            offset,
            frameEnd,
            0,
            frameIndex,
          );
          blocks.set(prefix.recordPrefix, destination);
          offset = prefix.offset;
          rawColorCounts[prefix.count - 1] += 1;
          if (prefix.count === 1) {
            blocks.fill(
              0,
              destination + PATTERN_OFFSET,
              destination + RECORD_BYTES,
            );
          } else if (prefix.count === 2) {
            requireFrameBytes(offset, 2, frameEnd, frameIndex);
            blocks.set(
              expand1BitPattern(bytes, offset),
              destination + PATTERN_OFFSET,
            );
            offset += 2;
          } else {
            requireFrameBytes(
              offset,
              PATTERN_BYTES,
              frameEnd,
              frameIndex,
            );
            blocks.set(
              bytes.subarray(offset, offset + PATTERN_BYTES),
              destination + PATTERN_OFFSET,
            );
            offset += PATTERN_BYTES;
          }
          const decodedPattern = blocks.subarray(
            destination + PATTERN_OFFSET,
            destination + RECORD_BYTES,
          );
          if (patternColorCount(decodedPattern) !== prefix.count ||
              patternUsedMask(decodedPattern) !==
                (1 << prefix.count) - 1) {
            throw new RangeError(
              `Non-canonical BPV1 RAW at ${frameIndex}:${blockIndex}`,
            );
          }
        } else {
          requireFrameBytes(offset, RECORD_BYTES, frameEnd, frameIndex);
          blocks.set(
            bytes.subarray(offset, offset + RECORD_BYTES),
            destination,
          );
          offset += RECORD_BYTES;
        }
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
      } else if (mode === MODE_RAW_DIRECT) {
        if (header.version < ADAPTIVE_RAW_VERSION ||
            header.version >= FOUR_MODE_VERSION) {
          throw new RangeError(
            `Direct RAW requires BPV1 v${ADAPTIVE_RAW_VERSION} ` +
            `at ${frameIndex}:${blockIndex}`,
          );
        }
        requireFrameBytes(offset, 9, frameEnd, frameIndex);
        const paletteIndex = bytes[offset++];
        if (paletteIndex >= header.paletteCount) {
          throw new RangeError(
            `Invalid direct palette at ${frameIndex}:${blockIndex}`,
          );
        }
        blocks[destination] = DIRECT_RECORD_FLAG | paletteIndex;
        blocks.set(bytes.subarray(offset, offset + 8), destination + 1);
        offset += 8;
        const used = directUsedMask(blocks, destination);
        const count = popcount16(used);
        if (count < 5 || count > 16) {
          throw new RangeError(
            `Non-canonical direct RAW at ${frameIndex}:${blockIndex}`,
          );
        }
        rawDirectColorCounts[count] += 1;
        addUnique(
          blockDictionary,
          blocks.subarray(destination, destination + RECORD_BYTES),
          header.maxBlockDictionary,
        );
      }
      if (header.version >= PIXEL_MOTION_VERSION &&
          mode !== MODE_SKIP && mode !== MODE_MOTION) {
        storeRecordPixels(
          pixels, header.width, header.height, blockIndex, blocksX,
          blocks, destination, activePalette,
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
        pixels,
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
    previousPixels = pixels;
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
    modeCounts: Object.fromEntries(
      (header.version >= FOUR_MODE_VERSION
        ? ["skip", "motion", "blockDictionary", "raw"]
        : MODE_NAMES
      ).map((name, index) => [name, modeCounts[index]]),
    ),
    rawColorCounts,
    rawDirectColorCounts,
    patternDictionaryColorCounts,
    minimumFrameBytes,
    maximumFrameBytes,
    meanFrameBytes: totalFrameBytes / header.frameCount,
    fileBytes: bytes.length,
  };
}

function renderFrameRgba(frame, header) {
  if (frame.pixels instanceof Uint16Array &&
      frame.pixels.length === header.width * header.height) {
    const rgba = new Uint8ClampedArray(
      header.width * header.height * 4,
    );
    for (let pixel = 0; pixel < frame.pixels.length; pixel += 1) {
      const color = frame.pixels[pixel];
      const red = (color >>> 11) & 31;
      const green = (color >>> 5) & 63;
      const blue = color & 31;
      const output = pixel * 4;
      rgba[output] = (red << 3) | (red >>> 2);
      rgba[output + 1] = (green << 2) | (green >>> 4);
      rgba[output + 2] = (blue << 3) | (blue >>> 2);
      rgba[output + 3] = 255;
    }
    return rgba;
  }
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
      let paletteIndex;
      let paletteColor;
      if (blocks[record] & DIRECT_RECORD_FLAG) {
        paletteIndex = blocks[record] & 63;
        paletteColor = directColor(blocks, record, pixel);
      } else {
        const local =
          (blocks[record + PATTERN_OFFSET + (pixel >> 2)] >>
           (6 - ((pixel & 3) << 1))) & 3;
        paletteIndex = blocks[record];
        paletteColor = blocks[record + 1 + local];
      }
      const colorIndex =
        paletteIndex * COLORS_PER_PALETTE + paletteColor;
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

function patternColorCount(pattern) {
  let count = 1;
  for (let position = 0; position < 16; position += 1) {
    const local = (
      pattern[position >> 2] >> (6 - (position & 3) * 2)
    ) & 3;
    count = Math.max(count, local + 1);
  }
  return count;
}

function patternUsedMask(pattern) {
  let mask = 0;
  for (let position = 0; position < 16; position += 1) {
    const local = (
      pattern[position >> 2] >> (6 - (position & 3) * 2)
    ) & 3;
    mask |= 1 << local;
  }
  return mask;
}

function readPackedPrefix(
  bytes,
  offset,
  frameEnd,
  expectedCount,
  frameIndex,
) {
  requireFrameBytes(offset, 2, frameEnd, frameIndex);
  const tag = bytes[offset++];
  const count = (tag >>> 6) + 1;
  if (expectedCount && count !== expectedCount) {
    throw new RangeError(
      `BPV1 packed colour count mismatch in frame ${frameIndex}`,
    );
  }
  const recordPrefix = new Uint8Array(PATTERN_OFFSET);
  recordPrefix[0] = tag & 63;
  const first = bytes[offset++];
  recordPrefix[1] = first >>> 4;
  if (count > 1) recordPrefix[2] = first & 15;
  if (count > 2) {
    requireFrameBytes(offset, 1, frameEnd, frameIndex);
    const second = bytes[offset++];
    recordPrefix[3] = second >>> 4;
    if (count > 3) recordPrefix[4] = second & 15;
  }
  return { count, offset, recordPrefix };
}

function readV6Raw(
  bytes,
  offset,
  frameEnd,
  frameIndex,
  paletteCount,
) {
  requireFrameBytes(offset, 2, frameEnd, frameIndex);
  const tag = bytes[offset++];
  const subtype = tag >>> 6;
  const paletteIndex = tag & 63;
  if (paletteIndex >= paletteCount) {
    throw new RangeError(
      `Invalid RAW palette at ${frameIndex}`,
    );
  }
  const record = new Uint8Array(RECORD_BYTES);
  if (subtype === 3) {
    requireFrameBytes(offset, 8, frameEnd, frameIndex);
    record[0] = DIRECT_RECORD_FLAG | paletteIndex;
    record.set(bytes.subarray(offset, offset + 8), 1);
    offset += 8;
    const count = popcount16(directUsedMask(record, 0));
    if (count < 5 || count > 16) {
      throw new RangeError(
        `Non-canonical direct RAW in frame ${frameIndex}`,
      );
    }
    return { record, offset, count, direct: true };
  }

  const capacity = subtype === 2 ? 4 : subtype + 1;
  const localBytes = (capacity + 1) >> 1;
  requireFrameBytes(offset, localBytes, frameEnd, frameIndex);
  record[0] = paletteIndex;
  record[1] = bytes[offset] >>> 4;
  if (capacity > 1) record[2] = bytes[offset] & 15;
  if (capacity > 2) {
    record[3] = bytes[offset + 1] >>> 4;
    record[4] = bytes[offset + 1] & 15;
  }
  offset += localBytes;
  if (subtype === 1) {
    requireFrameBytes(offset, 2, frameEnd, frameIndex);
    record.set(expand1BitPattern(bytes, offset), PATTERN_OFFSET);
    offset += 2;
  } else if (subtype === 2) {
    requireFrameBytes(offset, PATTERN_BYTES, frameEnd, frameIndex);
    record.set(
      bytes.subarray(offset, offset + PATTERN_BYTES),
      PATTERN_OFFSET,
    );
    offset += PATTERN_BYTES;
  }
  const pattern = record.subarray(PATTERN_OFFSET);
  const count = patternColorCount(pattern);
  const mask = patternUsedMask(pattern);
  if ((subtype === 0 && count !== 1) ||
      (subtype === 1 && (count !== 2 || mask !== 3)) ||
      (subtype === 2 &&
       ((count !== 3 && count !== 4) ||
        mask !== (1 << count) - 1 ||
        (count === 3 && record[4] !== 0)))) {
    throw new RangeError(
      `Non-canonical BPV1 v6 RAW in frame ${frameIndex}`,
    );
  }
  return { record, offset, count, direct: false };
}

function expand1BitPattern(bytes, offset) {
  const pattern = new Uint8Array(PATTERN_BYTES);
  for (let position = 0; position < 16; position += 1) {
    const local =
      (bytes[offset + (position >> 3)] >> (7 - (position & 7))) & 1;
    pattern[position >> 2] |=
      local << (6 - (position & 3) * 2);
  }
  return pattern;
}

function validateRecord(bytes, offset, paletteCount, frameIndex, blockIndex) {
  if (bytes[offset] & DIRECT_RECORD_FLAG) {
    if ((bytes[offset] & 0x40) || (bytes[offset] & 63) >= paletteCount) {
      throw new RangeError(
        `Invalid direct palette index at ${frameIndex}:${blockIndex}`,
      );
    }
    return;
  }
  if (bytes[offset] >= paletteCount) {
    throw new RangeError(`Invalid palette index at ${frameIndex}:${blockIndex}`);
  }
  for (let index = 1; index <= 4; index += 1) {
    if (bytes[offset + index] >= COLORS_PER_PALETTE) {
      throw new RangeError(`Invalid local color at ${frameIndex}:${blockIndex}`);
    }
  }
}

function directColor(bytes, offset, pixel) {
  const value = bytes[offset + 1 + (pixel >> 1)];
  return pixel & 1 ? value & 15 : value >>> 4;
}

function recordPixelColor(bytes, offset, pixel) {
  if (bytes[offset] & DIRECT_RECORD_FLAG) {
    return directColor(bytes, offset, pixel);
  }
  const shift = 6 - ((pixel & 3) * 2);
  const local =
    (bytes[offset + PATTERN_OFFSET + (pixel >> 2)] >> shift) & 3;
  return bytes[offset + 1 + local];
}

function copyPixelRegion(
  source, destination, width, height,
  blockIndex, blocksX, dx, dy,
) {
  const destinationX = (blockIndex % blocksX) * BLOCK_SIZE;
  const destinationY =
    Math.floor(blockIndex / blocksX) * BLOCK_SIZE;
  const sourceX = destinationX + dx;
  const sourceY = destinationY + dy;
  if (!source || sourceX < 0 || sourceY < 0 ||
      sourceX + BLOCK_SIZE > width ||
      sourceY + BLOCK_SIZE > height) {
    return false;
  }
  for (let y = 0; y < BLOCK_SIZE; y += 1) {
    for (let x = 0; x < BLOCK_SIZE; x += 1) {
      destination[(destinationY + y) * width + destinationX + x] =
        source[(sourceY + y) * width + sourceX + x];
    }
  }
  return true;
}

function storeRecordPixels(
  destination, width, height, blockIndex, blocksX,
  blocks, record, palette,
) {
  const destinationX = (blockIndex % blocksX) * BLOCK_SIZE;
  const destinationY =
    Math.floor(blockIndex / blocksX) * BLOCK_SIZE;
  const paletteIndex = blocks[record] & 63;
  for (let y = 0; y < BLOCK_SIZE; y += 1) {
    for (let x = 0; x < BLOCK_SIZE; x += 1) {
      const pixel = y * BLOCK_SIZE + x;
      const paletteColor =
        recordPixelColor(blocks, record, pixel);
      const color =
        (paletteIndex * COLORS_PER_PALETTE + paletteColor) * 3;
      destination[(destinationY + y) * width + destinationX + x] =
        ((palette[color] & 0xf8) << 8) |
        ((palette[color + 1] & 0xfc) << 3) |
        (palette[color + 2] >>> 3);
    }
  }
}

function directUsedMask(bytes, offset) {
  let mask = 0;
  for (let pixel = 0; pixel < 16; pixel += 1) {
    mask |= 1 << directColor(bytes, offset, pixel);
  }
  return mask;
}

function popcount16(value) {
  let count = 0;
  while (value) {
    value &= value - 1;
    count += 1;
  }
  return count;
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

function readSignedNibble(value) {
  return value & 8 ? value - 16 : value;
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
    MODE_RAW: MODE_PATTERN_DICT,
    LEGACY_MODE_PATTERN_DICT: MODE_PATTERN_DICT,
    LEGACY_MODE_RAW: MODE_RAW,
    LEGACY_MODE_RAW_DIRECT: MODE_RAW_DIRECT,
    MODE_SKIP,
    PALETTE_COUNT,
    PIXEL_MOTION_VERSION,
    RECORD_BYTES,
    VERSION,
  },
};
