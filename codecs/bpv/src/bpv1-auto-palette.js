(function (root, factory) {
  "use strict";
  const bpv1Codec = typeof module === "object" && module.exports
    ? require("./bpv1-codec.js")
    : root.Bpv1Codec;
  const api = factory(bpv1Codec);
  if (typeof module === "object" && module.exports) module.exports = api;
  root.Bpv1AutoPalette = api;
})(typeof self !== "undefined" ? self : globalThis, function (bpv1Codec) {
  "use strict";

  const BLOCK_SIZE = 4;
  const PIXELS_PER_BLOCK = 16;
  const PALETTE_COUNT = 64;
  const COLORS_PER_PALETTE = 16;
  const LOCAL_COLORS = 4;

  function compressRgbaVideo(video, options) {
    if (!bpv1Codec || typeof bpv1Codec.encodeVideo !== "function") {
      throw new Error("Bpv1Codec is required");
    }
    const settings = options || {};
    const normalized = validateRgbaVideo(video);
    const training = buildAutomaticPalettes(normalized, settings);
    const frames = normalized.frames.map((rgba, frameIndex) => {
      reportProgress(settings.onProgress, "quantizing-frames", frameIndex, normalized.frames.length);
      return quantizeRgbaFrame(rgba, normalized.width, normalized.height, training);
    });
    reportProgress(settings.onProgress, "quantizing-frames", normalized.frames.length, normalized.frames.length);
    const encoded = bpv1Codec.encodeVideo({
      width: normalized.width,
      height: normalized.height,
      frames,
      palette: training.palette,
      fpsNumerator: normalized.fpsNumerator,
      fpsDenominator: normalized.fpsDenominator,
    }, settings);
    return {
      ...encoded,
      palette: training.palette,
      blockCenters: training.blockCenters,
      training: training.stats,
      frames,
    };
  }

  function buildAutomaticPalettes(video, options) {
    const settings = options || {};
    const maximumSampleBlocks = integerOption(settings.maximumSampleBlocks, 32768, 64, 262144);
    const maximumPixelsPerCluster = integerOption(settings.maximumPixelsPerCluster, 8192, 256, 65536);
    const blockIterations = integerOption(settings.blockClusterIterations, 10, 1, 32);
    const colorIterations = integerOption(settings.colorClusterIterations, 10, 1, 32);
    const colorSpace = settings.colorSpace || "oklab";
    if (colorSpace !== "oklab" && colorSpace !== "rgb") {
      throw new RangeError("colorSpace must be 'oklab' or 'rgb'");
    }
    const blocksX = Math.ceil(video.width / BLOCK_SIZE);
    const blocksY = Math.ceil(video.height / BLOCK_SIZE);
    const blockCount = blocksX * blocksY;
    const perFrame = Math.max(1, Math.ceil(maximumSampleBlocks / video.frames.length));
    const descriptors = [];
    const sampledBlocks = [];

    for (let frameIndex = 0; frameIndex < video.frames.length && descriptors.length < maximumSampleBlocks; frameIndex += 1) {
      const frame = video.frames[frameIndex];
      const take = Math.min(perFrame, blockCount, maximumSampleBlocks - descriptors.length);
      for (let sample = 0; sample < take; sample += 1) {
        const blockIndex = (Math.floor(sample * blockCount / take) + frameIndex * 977) % blockCount;
        const pixels = readBlock(frame, video.width, video.height, blocksX, blockIndex);
        sampledBlocks.push(pixels);
        descriptors.push(describeBlock(pixels, colorSpace));
      }
      reportProgress(settings.onProgress, "analyzing-blocks", frameIndex + 1, video.frames.length);
    }

    const blockKmeans = deterministicKmeans(descriptors, PALETTE_COUNT, blockIterations);
    const palette = [];
    const clusterCounts = new Uint32Array(PALETTE_COUNT);
    blockKmeans.labels.forEach((label) => { clusterCounts[label] += 1; });
    const allPixels = sampledBlocks.flat();
    const globalSamples = uniformSample(allPixels, maximumPixelsPerCluster);
    const globalPoints = globalSamples.map((color) => colorPoint(color, colorSpace));
    const globalColors = deterministicKmeans(globalPoints, COLORS_PER_PALETTE, colorIterations).centers;

    for (let cluster = 0; cluster < PALETTE_COUNT; cluster += 1) {
      const clusterPixels = [];
      for (let block = 0; block < sampledBlocks.length; block += 1) {
        if (blockKmeans.labels[block] === cluster) clusterPixels.push(...sampledBlocks[block]);
      }
      const samples = uniformSample(clusterPixels, maximumPixelsPerCluster);
      const points = samples.length > 0
        ? samples.map((color) => colorPoint(color, colorSpace))
        : globalPoints;
      const centers = points.length > 0
        ? deterministicKmeans(points, COLORS_PER_PALETTE, colorIterations).centers
        : globalColors;
      const colors = centers.map((center) => colorSpace === "oklab"
        ? oklabToSrgb(center)
        : createColor(center[0], center[1], center[2]));
      colors.sort((a, b) => {
        const left = srgbToOklab(a);
        const right = srgbToOklab(b);
        return left[0] - right[0] || left[1] - right[1] || left[2] - right[2];
      });
      palette.push(...colors);
    }

    return {
      palette,
      blockCenters: blockKmeans.centers,
      colorSpace,
      stats: {
        sampledBlocks: descriptors.length,
        clusterCounts: Array.from(clusterCounts),
        blockClusterIterations: blockIterations,
        colorClusterIterations: colorIterations,
        colorSpace,
        blockDescriptor: "mean and standard deviation",
      },
    };
  }

  function quantizeRgbaFrame(rgba, width, height, training) {
    const blocksX = Math.ceil(width / BLOCK_SIZE);
    const blocksY = Math.ceil(height / BLOCK_SIZE);
    const blocks = new Array(blocksX * blocksY);
    for (let blockIndex = 0; blockIndex < blocks.length; blockIndex += 1) {
      const pixels = readBlock(rgba, width, height, blocksX, blockIndex);
      const descriptor = describeBlock(pixels, training.colorSpace);
      const paletteIndex = nearestCenter(descriptor, training.blockCenters);
      blocks[blockIndex] = quantizeBlock(pixels, paletteIndex, training.palette);
    }
    return { blocks };
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
    for (let pixel = 0; pixel < PIXELS_PER_BLOCK; pixel += 1) {
      let local = 0;
      let distance = distances[pixel][selected[0]];
      for (let slot = 1; slot < LOCAL_COLORS; slot += 1) {
        const candidate = distances[pixel][selected[slot]];
        if (candidate < distance) { distance = candidate; local = slot; }
      }
      const bitOffset = pixel * 2;
      pattern[bitOffset >> 3] |= local << (6 - (bitOffset & 7));
    }
    return { paletteIndex, localColors: selected, pattern };
  }

  function validateRgbaVideo(video) {
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
    const blockX = blockIndex % blocksX;
    const blockY = Math.floor(blockIndex / blocksX);
    const pixels = new Array(PIXELS_PER_BLOCK);
    let target = 0;
    for (let localY = 0; localY < BLOCK_SIZE; localY += 1) {
      const y = Math.min(height - 1, blockY * BLOCK_SIZE + localY);
      for (let localX = 0; localX < BLOCK_SIZE; localX += 1) {
        const x = Math.min(width - 1, blockX * BLOCK_SIZE + localX);
        const offset = (y * width + x) * 4;
        pixels[target++] = createColor(rgba[offset], rgba[offset + 1], rgba[offset + 2]);
      }
    }
    return pixels;
  }

  function describeBlock(pixels, colorSpace) {
    const mean = [0, 0, 0];
    const points = pixels.map((color) => colorPoint(color, colorSpace));
    points.forEach((point) => { mean[0] += point[0]; mean[1] += point[1]; mean[2] += point[2]; });
    mean[0] /= points.length; mean[1] /= points.length; mean[2] /= points.length;
    const variance = [0, 0, 0];
    points.forEach((point) => {
      for (let channel = 0; channel < 3; channel += 1) {
        const d = point[channel] - mean[channel];
        variance[channel] += d * d;
      }
    });
    return [mean[0], mean[1], mean[2], Math.sqrt(variance[0] / points.length), Math.sqrt(variance[1] / points.length), Math.sqrt(variance[2] / points.length)];
  }

  function deterministicKmeans(points, requestedClusters, iterations) {
    if (!Array.isArray(points) || points.length === 0) throw new RangeError("Cannot cluster an empty sample");
    const dimension = points[0].length;
    const clusters = requestedClusters;
    const mean = new Float64Array(dimension);
    points.forEach((point) => { for (let d = 0; d < dimension; d += 1) mean[d] += point[d]; });
    for (let d = 0; d < dimension; d += 1) mean[d] /= points.length;
    let first = 0;
    let farthest = -1;
    points.forEach((point, index) => {
      const distance = squaredDistance(point, mean);
      if (distance > farthest) { farthest = distance; first = index; }
    });
    const centers = [Array.from(points[first])];
    const nearest = new Float64Array(points.length);
    nearest.fill(Infinity);
    while (centers.length < clusters) {
      const newest = centers[centers.length - 1];
      let next = 0;
      let nextDistance = -1;
      points.forEach((point, index) => {
        nearest[index] = Math.min(nearest[index], squaredDistance(point, newest));
        if (nearest[index] > nextDistance) { nextDistance = nearest[index]; next = index; }
      });
      centers.push(Array.from(points[next]));
    }
    let labels = new Uint16Array(points.length);
    for (let iteration = 0; iteration < iterations; iteration += 1) {
      const sums = Array.from({ length: clusters }, () => new Float64Array(dimension));
      const counts = new Uint32Array(clusters);
      let changed = false;
      points.forEach((point, index) => {
        const label = nearestCenter(point, centers);
        if (labels[index] !== label) changed = true;
        labels[index] = label;
        counts[label] += 1;
        for (let d = 0; d < dimension; d += 1) sums[label][d] += point[d];
      });
      for (let cluster = 0; cluster < clusters; cluster += 1) {
        if (counts[cluster] === 0) continue;
        for (let d = 0; d < dimension; d += 1) centers[cluster][d] = sums[cluster][d] / counts[cluster];
      }
      if (!changed && iteration > 0) break;
    }
    return { centers, labels };
  }

  function nearestCenter(point, centers) {
    let best = 0;
    let distance = squaredDistance(point, centers[0]);
    for (let center = 1; center < centers.length; center += 1) {
      const candidate = squaredDistance(point, centers[center]);
      if (candidate < distance) { distance = candidate; best = center; }
    }
    return best;
  }

  function colorPoint(color, colorSpace) {
    return colorSpace === "oklab" ? srgbToOklab(color) : [color.r, color.g, color.b];
  }

  function srgbToOklab(color) {
    const rgb = [color.r, color.g, color.b].map((value) => {
      const x = value / 255;
      return x <= 0.04045 ? x / 12.92 : ((x + 0.055) / 1.055) ** 2.4;
    });
    const l = 0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2];
    const m = 0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2];
    const s = 0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2];
    const ll = Math.cbrt(Math.max(0, l));
    const mm = Math.cbrt(Math.max(0, m));
    const ss = Math.cbrt(Math.max(0, s));
    return [
      0.2104542553 * ll + 0.7936177850 * mm - 0.0040720468 * ss,
      1.9779984951 * ll - 2.4285922050 * mm + 0.4505937099 * ss,
      0.0259040371 * ll + 0.7827717662 * mm - 0.8086757660 * ss,
    ];
  }

  function oklabToSrgb(lab) {
    const L = lab[0], a = lab[1], b = lab[2];
    const ll = L + 0.3963377774 * a + 0.2158037573 * b;
    const mm = L - 0.1055613458 * a - 0.0638541728 * b;
    const ss = L - 0.0894841775 * a - 1.2914855480 * b;
    const l = ll ** 3, m = mm ** 3, s = ss ** 3;
    const linear = [
      4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
      -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
      -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    ];
    return createColor(...linear.map((value) => {
      const x = Math.max(0, Math.min(1, value));
      const srgb = x <= 0.0031308 ? 12.92 * x : 1.055 * x ** (1 / 2.4) - 0.055;
      return Math.round(srgb * 255);
    }));
  }

  function uniformSample(values, maximum) {
    if (values.length <= maximum) return values.slice();
    const out = new Array(maximum);
    for (let index = 0; index < maximum; index += 1) out[index] = values[Math.floor(index * values.length / maximum)];
    return out;
  }

  function squaredDistance(left, right) {
    let value = 0;
    for (let i = 0; i < left.length; i += 1) { const d = left[i] - right[i]; value += d * d; }
    return value;
  }
  function rgbSquaredDistance(left, right) {
    const r = left.r - right.r, g = left.g - right.g, b = left.b - right.b;
    return r * r + g * g + b * b;
  }
  function createColor(r, g, b) {
    return { r: clampByte(r), g: clampByte(g), b: clampByte(b) };
  }
  function clampByte(value) { return Math.max(0, Math.min(255, Math.round(Number(value) || 0))); }
  function integerOption(value, fallback, minimum, maximum) {
    const number = value === undefined || value === null ? fallback : Number(value);
    if (!Number.isInteger(number) || number < minimum || number > maximum) throw new RangeError(`Integer ${minimum}..${maximum} required`);
    return number;
  }
  function asUint8Array(value) {
    if (value instanceof Uint8Array) return value;
    if (ArrayBuffer.isView(value)) return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
    if (value instanceof ArrayBuffer) return new Uint8Array(value);
    return Uint8Array.from(value || []);
  }
  function reportProgress(callback, stage, completed, total) {
    if (typeof callback === "function") callback({ stage, completed, total, progress: total === 0 ? 1 : completed / total });
  }

  return { compressRgbaVideo, buildAutomaticPalettes, quantizeRgbaFrame };
});
