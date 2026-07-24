(function (root, factory) {
  "use strict";
  const codec = typeof module === "object" && module.exports
    ? require("./bpv1-codec.js")
    : root.Bpv1Codec;
  const autoPalette = typeof module === "object" && module.exports
    ? require("./bpv1-auto-palette.js")
    : root.Bpv1AutoPalette;
  const api = factory(codec, autoPalette);
  if (typeof module === "object" && module.exports) module.exports = api;
  root.Bpv1RateDistortion = api;
})(typeof self !== "undefined" ? self : globalThis, function (codec, autoPalette) {
  "use strict";

  const BLOCK_SIZE = 4;
  const PIXELS_PER_BLOCK = 16;
  const COLORS_PER_PALETTE = 16;
  const LOCAL_COLORS = 4;
  const RAW_BITS = 72;
  const BLOCK_DICTIONARY_BITS = 16;
  const PATTERN_DICTIONARY_BITS = 56;

  function compressRgbaVideoRateDistortion(video, options) {
    if (!codec || typeof codec.encodeVideo !== "function") throw new Error("Bpv1Codec is required");
    if (!autoPalette || typeof autoPalette.buildAutomaticPalettes !== "function") {
      throw new Error("Bpv1AutoPalette is required");
    }
    const settings = options || {};
    const normalized = normalizeVideo(video);
    const lambda = finiteOption(settings.lambda, 64, 0, 1e9, "lambda");
    const candidatePaletteCount = integerOption(settings.candidatePaletteCount, 3, 1, 8);
    const keyframeInterval = integerOption(settings.keyframeInterval, 30, 1, 65535);
    const maximumBlockDictionary = integerOption(settings.maxBlockDictionary, 256, 1, 65535);
    const maximumPatternDictionary = integerOption(settings.maxPatternDictionary, 256, 1, 65535);
    const training = autoPalette.buildAutomaticPalettes(normalized, settings);
    const blocksX = Math.ceil(normalized.width / BLOCK_SIZE);
    const blocksY = Math.ceil(normalized.height / BLOCK_SIZE);
    const blockCount = blocksX * blocksY;
    const selectedFrames = new Array(normalized.frames.length);
    let previousBlocks = null;
    let blockSet = new Set();
    let patternSet = new Set();
    let totalSquaredError = 0;
    const decisionCounts = { previous: 0, quantized: 0 };

    for (let frameIndex = 0; frameIndex < normalized.frames.length; frameIndex += 1) {
      const keyframe = frameIndex === 0 || frameIndex % keyframeInterval === 0;
      if (keyframe) {
        previousBlocks = null;
        blockSet = new Set();
        patternSet = new Set();
      }
      const frame = normalized.frames[frameIndex];
      const blocks = new Array(blockCount);
      for (let blockIndex = 0; blockIndex < blockCount; blockIndex += 1) {
        const pixels = readBlock(frame, normalized.width, normalized.height, blocksX, blockIndex);
        const descriptor = describeBlock(pixels, training.colorSpace);
        const paletteIndices = nearestCenters(descriptor, training.blockCenters, candidatePaletteCount);
        let best = null;

        if (previousBlocks) {
          const previous = previousBlocks[blockIndex];
          const error = blockSquaredError(pixels, previous, training.palette);
          best = { block: previous, error, bits: 0, score: error, source: "previous" };
        }

        for (const paletteIndex of paletteIndices) {
          const candidate = quantizeBlock(pixels, paletteIndex, training.palette);
          const bkey = blockKey(candidate.block);
          const pkey = patternKey(candidate.block.pattern);
          let bits = RAW_BITS;
          if (previousBlocks && equalBlock(candidate.block, previousBlocks[blockIndex])) bits = 0;
          else if (blockSet.has(bkey)) bits = BLOCK_DICTIONARY_BITS;
          else if (patternSet.has(pkey)) bits = PATTERN_DICTIONARY_BITS;
          const score = candidate.error + lambda * bits;
          if (!best || score < best.score ||
              (score === best.score && bits < best.bits) ||
              (score === best.score && bits === best.bits && candidate.error < best.error) ||
              (score === best.score && bits === best.bits && candidate.error === best.error && bkey < blockKey(best.block))) {
            best = { block: candidate.block, error: candidate.error, bits, score, source: "quantized" };
          }
        }

        blocks[blockIndex] = cloneBlock(best.block);
        totalSquaredError += best.error;
        decisionCounts[best.source] += 1;

        // This shadow dictionary is used only for future rate estimates. The
        // canonical encoder still decides the actual SKIP/MOTION/DICT/RAW mode.
        const bkey = blockKey(best.block);
        const pkey = patternKey(best.block.pattern);
        const samePrevious = previousBlocks && equalBlock(best.block, previousBlocks[blockIndex]);
        if (!samePrevious && !blockSet.has(bkey)) {
          if (patternSet.has(pkey)) {
            addBounded(blockSet, bkey, maximumBlockDictionary);
          } else {
            addBounded(patternSet, pkey, maximumPatternDictionary);
            addBounded(blockSet, bkey, maximumBlockDictionary);
          }
        }
      }
      selectedFrames[frameIndex] = { blocks };
      previousBlocks = blocks;
      reportProgress(settings.onProgress, "rate-distortion", frameIndex + 1, normalized.frames.length);
    }

    const encoded = codec.encodeVideo({
      width: normalized.width,
      height: normalized.height,
      frames: selectedFrames,
      palette: training.palette,
      fpsNumerator: normalized.fpsNumerator,
      fpsDenominator: normalized.fpsDenominator,
    }, { ...settings, keyframeInterval });
    const samples = normalized.width * normalized.height * 3 * normalized.frames.length;
    const mse = totalSquaredError / samples;
    const psnrDb = mse === 0 ? Infinity : 10 * Math.log10(255 * 255 / mse);
    return {
      ...encoded,
      palette: training.palette,
      blockCenters: training.blockCenters,
      training: training.stats,
      frames: selectedFrames,
      rateDistortion: {
        lambda,
        candidatePaletteCount,
        criterion: "RGB block SSE + lambda * payload bits",
        payloadBits: {
          skip: 0,
          motion: 16,
          blockDictionary: BLOCK_DICTIONARY_BITS,
          patternDictionary: PATTERN_DICTIONARY_BITS,
          raw: RAW_BITS,
        },
        mse,
        psnrDb,
        decisionCounts,
      },
    };
  }

  function quantizeBlock(pixels, paletteIndex, flatPalette) {
    const paletteOffset = paletteIndex * COLORS_PER_PALETTE;
    const distances = Array.from({ length: PIXELS_PER_BLOCK }, () => new Float64Array(COLORS_PER_PALETTE));
    for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
      for (let color = 0; color < COLORS_PER_PALETTE; color += 1) {
        distances[pixel][color] = rgbSquaredDistance(pixels[pixel], flatPalette[paletteOffset + color]);
      }
    }
    const selected = [];
    const current = new Float64Array(PIXELS_PER_BLOCK);
    current.fill(Infinity);
    while (selected.length < LOCAL_COLORS) {
      let bestColor = 0;
      let bestError = Infinity;
      for (let color = 0; color < COLORS_PER_PALETTE; color += 1) {
        if (selected.includes(color)) continue;
        let error = 0;
        for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
          error += Math.min(current[pixel], distances[pixel][color]);
        }
        if (error < bestError) { bestError = error; bestColor = color; }
      }
      selected.push(bestColor);
      for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
        current[pixel] = Math.min(current[pixel], distances[pixel][bestColor]);
      }
    }
    selected.sort((a, b) => a - b);
    const pattern = new Uint8Array(4);
    let error = 0;
    for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
      let local = 0;
      let distance = distances[pixel][selected[0]];
      for (let slot = 1; slot < LOCAL_COLORS; slot += 1) {
        const candidate = distances[pixel][selected[slot]];
        if (candidate < distance) { distance = candidate; local = slot; }
      }
      error += distance;
      pattern[pixel >> 2] |= local << (6 - (pixel & 3) * 2);
    }
    return { block: { paletteIndex, localColors: selected, pattern }, error };
  }

  function blockSquaredError(pixels, block, palette) {
    let error = 0;
    const base = block.paletteIndex * COLORS_PER_PALETTE;
    for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
      const local = (block.pattern[pixel >> 2] >> (6 - (pixel & 3) * 2)) & 3;
      error += rgbSquaredDistance(pixels[pixel], palette[base + block.localColors[local]]);
    }
    return error;
  }

  function nearestCenters(point, centers, count) {
    return centers.map((center, index) => ({ index, distance: squaredDistance(point, center) }))
      .sort((a, b) => a.distance - b.distance || a.index - b.index)
      .slice(0, count)
      .map((entry) => entry.index);
  }

  function normalizeVideo(video) {
    if (!video || typeof video !== "object") throw new TypeError("Video must be an object");
    const width = integerOption(video.width, null, 1, 65535);
    const height = integerOption(video.height, null, 1, 65535);
    if (!Array.isArray(video.frames) || video.frames.length === 0) throw new RangeError("Video must contain RGBA frames");
    const frames = video.frames.map((frame) => {
      const rgba = asUint8Array(frame);
      if (rgba.length !== width * height * 4) throw new RangeError("RGBA frame length mismatch");
      return rgba;
    });
    return {
      width,
      height,
      frames,
      fpsNumerator: integerOption(video.fpsNumerator, 25, 1, 65535),
      fpsDenominator: integerOption(video.fpsDenominator, 1, 1, 65535),
    };
  }

  function readBlock(rgba, width, height, blocksX, blockIndex) {
    const bx = blockIndex % blocksX;
    const by = Math.floor(blockIndex / blocksX);
    const pixels = new Array(PIXELS_PER_BLOCK);
    let target = 0;
    for (let ly = 0; ly < BLOCK_SIZE; ly += 1) {
      const y = Math.min(height - 1, by * BLOCK_SIZE + ly);
      for (let lx = 0; lx < BLOCK_SIZE; lx += 1) {
        const x = Math.min(width - 1, bx * BLOCK_SIZE + lx);
        const offset = (y * width + x) * 4;
        pixels[target++] = { r: rgba[offset], g: rgba[offset + 1], b: rgba[offset + 2] };
      }
    }
    return pixels;
  }

  function describeBlock(pixels, colorSpace) {
    const points = pixels.map((color) => colorSpace === "oklab" ? srgbToOklab(color) : [color.r, color.g, color.b]);
    const mean = [0, 0, 0];
    for (const point of points) for (let c = 0; c < 3; c += 1) mean[c] += point[c];
    for (let c = 0; c < 3; c += 1) mean[c] /= points.length;
    const variance = [0, 0, 0];
    for (const point of points) for (let c = 0; c < 3; c += 1) { const d = point[c] - mean[c]; variance[c] += d * d; }
    return [mean[0], mean[1], mean[2], Math.sqrt(variance[0] / points.length), Math.sqrt(variance[1] / points.length), Math.sqrt(variance[2] / points.length)];
  }

  function srgbToOklab(color) {
    const rgb = [color.r, color.g, color.b].map((value) => {
      const x = value / 255;
      return x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
    });
    const l = 0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2];
    const m = 0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2];
    const s = 0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2];
    const ll = Math.cbrt(Math.max(0, l)), mm = Math.cbrt(Math.max(0, m)), ss = Math.cbrt(Math.max(0, s));
    return [
      0.2104542553 * ll + 0.7936177850 * mm - 0.0040720468 * ss,
      1.9779984951 * ll - 2.4285922050 * mm + 0.4505937099 * ss,
      0.0259040371 * ll + 0.7827717662 * mm - 0.8086757660 * ss,
    ];
  }

  function addBounded(set, value, limit) {
    if (set.has(value)) return;
    if (set.size >= limit) set.delete(set.values().next().value);
    set.add(value);
  }
  function blockKey(block) { return `${block.paletteIndex}:${Array.from(block.localColors).join(",")}:${patternKey(block.pattern)}`; }
  function patternKey(pattern) { return Array.from(pattern).join(","); }
  function cloneBlock(block) { return { paletteIndex: block.paletteIndex, localColors: Array.from(block.localColors), pattern: Uint8Array.from(block.pattern) }; }
  function equalBlock(a, b) { return a.paletteIndex === b.paletteIndex && equalArray(a.localColors, b.localColors) && equalArray(a.pattern, b.pattern); }
  function equalArray(a, b) { if (!a || !b || a.length !== b.length) return false; for (let i = 0; i < a.length; i += 1) if (a[i] !== b[i]) return false; return true; }
  function rgbSquaredDistance(a, b) { const r = a.r - b.r, g = a.g - b.g, bl = a.b - b.b; return r * r + g * g + bl * bl; }
  function squaredDistance(a, b) { let value = 0; for (let i = 0; i < a.length; i += 1) { const d = a[i] - b[i]; value += d * d; } return value; }
  function asUint8Array(value) { if (value instanceof Uint8Array) return value; if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength); if (value instanceof ArrayBuffer) return new Uint8Array(value); return Uint8Array.from(value || []); }
  function integerOption(value, fallback, min, max) { const n = value === undefined || value === null ? fallback : Number(value); if (!Number.isInteger(n) || n < min || n > max) throw new RangeError(`Integer ${min}..${max} required`); return n; }
  function finiteOption(value, fallback, min, max, name) { const n = value === undefined || value === null ? fallback : Number(value); if (!Number.isFinite(n) || n < min || n > max) throw new RangeError(`${name} must be ${min}..${max}`); return n; }
  function reportProgress(callback, stage, completed, total) { if (typeof callback === "function") callback({ stage, completed, total, progress: total === 0 ? 1 : completed / total }); }

  return { compressRgbaVideoRateDistortion };
});
