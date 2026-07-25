"use strict";

const bpv1 = require("../tools/bpv1-file.js");

const ACTIVE_PALETTES = 64;
const COLORS_PER_PALETTE = 16;
const COLOR_BYTES = 3;
const PALETTE_BYTES = COLORS_PER_PALETTE * COLOR_BYTES;
const RECORD_BYTES = 9;
const PATTERN_OFFSET = 5;
const LITERAL_BITMAP_BYTES = ACTIVE_PALETTES / 8;

function collectPaletteStatistics(input) {
  const bytes = asUint8Array(input);
  const header = bpv1.parseHeader(bytes);
  if (header.version !== bpv1.constants.ACTIVE_PALETTE_VERSION) {
    throw new RangeError("Superpalette analysis requires a BPV1 v4 stream");
  }

  const blocksX = Math.ceil(header.width / 4);
  const blocksY = Math.ceil(header.height / 4);
  const fullBlocks = (header.width & 3) === 0 && (header.height & 3) === 0;
  const gops = [];
  let currentGop = null;

  bpv1.walkFrames(bytes, (frame) => {
    if (frame.keyframe) {
      currentGop = {
        firstFrame: frame.frameIndex,
        frameCount: 0,
        palette: Uint8Array.from(frame.palette),
        colorUses: new Float64Array(
          ACTIVE_PALETTES * COLORS_PER_PALETTE,
        ),
      };
      gops.push(currentGop);
    }
    if (!currentGop) throw new RangeError("Frame precedes the first keyframe");
    currentGop.frameCount += 1;
    accumulateFrameUses(
      frame.blocks,
      currentGop.colorUses,
      header.width,
      header.height,
      blocksX,
      blocksY,
      fullBlocks,
    );
  });

  const sources = [];
  const exactRows = new Map();
  let usedPaletteSlots = 0;
  for (let gopIndex = 0; gopIndex < gops.length; gopIndex += 1) {
    const gop = gops[gopIndex];
    for (let slot = 0; slot < ACTIVE_PALETTES; slot += 1) {
      const paletteOffset = slot * PALETTE_BYTES;
      const useOffset = slot * COLORS_PER_PALETTE;
      const palette = gop.palette.slice(
        paletteOffset,
        paletteOffset + PALETTE_BYTES,
      );
      const colorUses = Float64Array.from(
        gop.colorUses.subarray(
          useOffset,
          useOffset + COLORS_PER_PALETTE,
        ),
      );
      const pixelUses = sum(colorUses);
      if (pixelUses > 0) usedPaletteSlots += 1;
      const key = bytesKey(palette);
      exactRows.set(key, (exactRows.get(key) || 0) + 1);
      sources.push({
        gopIndex,
        slot,
        palette,
        colorUses,
        pixelUses,
        key,
      });
    }
  }

  return {
    version: header.version,
    width: header.width,
    height: header.height,
    frameCount: header.frameCount,
    totalRgbSamples: header.width * header.height * header.frameCount * 3,
    fileBytes: bytes.length,
    gops,
    sources,
    paletteUpdates: gops.length,
    activePaletteBytes: gops.length * ACTIVE_PALETTES * PALETTE_BYTES,
    uniquePaletteRows: exactRows.size,
    exactDuplicateSlots: sources.length - exactRows.size,
    usedPaletteSlots,
  };
}

function buildSuperpalette(statistics, requestedSize) {
  if (!statistics || !Array.isArray(statistics.sources)) {
    throw new TypeError("Palette statistics are required");
  }
  const size = normalizeInteger(requestedSize, 1, 4096, "bank size");
  const unique = combineExactSources(statistics.sources);
  if (unique.length === 0) return [];

  unique.sort(compareSourcePriority);
  const selected = [unique[0]];
  const selectedKeys = new Set([unique[0].key]);
  const minimumErrors = unique.map((source) =>
    weightedPaletteError(source, unique[0].palette));

  while (selected.length < size && selected.length < unique.length) {
    let bestIndex = -1;
    for (let index = 0; index < unique.length; index += 1) {
      if (selectedKeys.has(unique[index].key)) continue;
      if (bestIndex < 0 ||
          minimumErrors[index] > minimumErrors[bestIndex] ||
          (minimumErrors[index] === minimumErrors[bestIndex] &&
           compareSourcePriority(unique[index], unique[bestIndex]) < 0)) {
        bestIndex = index;
      }
    }
    if (bestIndex < 0) break;
    const chosen = unique[bestIndex];
    selected.push(chosen);
    selectedKeys.add(chosen.key);
    for (let index = 0; index < unique.length; index += 1) {
      const error = weightedPaletteError(unique[index], chosen.palette);
      if (error < minimumErrors[index]) minimumErrors[index] = error;
    }
  }

  return selected.map((source) => Uint8Array.from(source.palette));
}

function evaluateSuperpalette(statistics, bank, targetPsnrValues) {
  if (!Array.isArray(bank) || bank.length === 0) {
    throw new RangeError("Superpalette bank must not be empty");
  }
  const bitsPerId = Math.max(1, Math.ceil(Math.log2(bank.length)));
  const mappings = nearestMappings(statistics, bank);
  mappings.sort((left, right) =>
    left.error - right.error ||
    left.gopIndex - right.gopIndex ||
    left.slot - right.slot);

  const targets = [
    Infinity,
    ...Array.from(targetPsnrValues || []),
    -Infinity,
  ];
  return {
    bankSize: bank.length,
    bankBytes: bank.length * PALETTE_BYTES,
    bitsPerId,
    variants: targets.map((targetPsnr) =>
      evaluateTarget(statistics, mappings, bank.length, bitsPerId, targetPsnr)),
  };
}

function materializeNearestActiveBanks(statistics, bank) {
  if (!Array.isArray(bank) || bank.length === 0) {
    throw new RangeError("Superpalette bank must not be empty");
  }
  const mappings = nearestMappings(statistics, bank);
  const output = new Uint8Array(
    statistics.paletteUpdates * ACTIVE_PALETTES * PALETTE_BYTES,
  );
  for (const mapping of mappings) {
    const destination =
      (mapping.gopIndex * ACTIVE_PALETTES + mapping.slot) * PALETTE_BYTES;
    output.set(bank[mapping.bankIndex], destination);
  }
  return output;
}

function analyzeSuperpalettes(input, options) {
  const settings = options || {};
  const statistics = collectPaletteStatistics(input);
  const bankSizes = settings.bankSizes || [128, 256, 512];
  const targetPsnr = settings.targetPsnr || [60, 50, 45, 40];
  const banks = [];
  for (const size of bankSizes) {
    const bank = buildSuperpalette(statistics, size);
    banks.push(evaluateSuperpalette(statistics, bank, targetPsnr));
  }
  return {
    format: "BPV1 superpalette experiment",
    source: {
      version: statistics.version,
      width: statistics.width,
      height: statistics.height,
      frameCount: statistics.frameCount,
      fileBytes: statistics.fileBytes,
      paletteUpdates: statistics.paletteUpdates,
      activePaletteBytes: statistics.activePaletteBytes,
      paletteSlots: statistics.sources.length,
      usedPaletteSlots: statistics.usedPaletteSlots,
      uniquePaletteRows: statistics.uniquePaletteRows,
      exactDuplicateSlots: statistics.exactDuplicateSlots,
    },
    banks,
  };
}

function evaluateTarget(
  statistics,
  sortedMappings,
  bankSize,
  bitsPerId,
  targetPsnr,
) {
  const maximumError = Number.isFinite(targetPsnr)
    ? statistics.totalRgbSamples * 65025 / Math.pow(10, targetPsnr / 10)
    : targetPsnr === -Infinity ? Infinity : 0;
  const referencesPerGop = new Uint16Array(statistics.paletteUpdates);
  let error = 0;
  let references = 0;
  for (const mapping of sortedMappings) {
    if (error + mapping.error > maximumError) break;
    error += mapping.error;
    references += 1;
    referencesPerGop[mapping.gopIndex] += 1;
  }
  let mappingBytes = statistics.paletteUpdates * LITERAL_BITMAP_BYTES;
  for (const count of referencesPerGop) {
    mappingBytes += Math.ceil(count * bitsPerId / 8);
  }
  const literalPalettes = statistics.sources.length - references;
  const literalBytes = literalPalettes * PALETTE_BYTES;
  const superpaletteBytes = bankSize * PALETTE_BYTES;
  const paletteSectionBytes =
    superpaletteBytes + mappingBytes + literalBytes;
  const estimatedFileBytes =
    statistics.fileBytes - statistics.activePaletteBytes +
    paletteSectionBytes;
  const mse = error === 0 ? 0 : error / statistics.totalRgbSamples;
  return {
    targetPsnrDb: Number.isFinite(targetPsnr)
      ? targetPsnr
      : targetPsnr === -Infinity ? "all" : "exact",
    references,
    literalPalettes,
    superpaletteBytes,
    mappingBytes,
    literalBytes,
    paletteSectionBytes,
    paletteBytesSaved: statistics.activePaletteBytes - paletteSectionBytes,
    estimatedFileBytes,
    completeFileSavingPercent:
      (statistics.fileBytes - estimatedFileBytes) * 100 /
      statistics.fileBytes,
    rgbSse: error,
    perturbationMse: mse,
    perturbationPsnrDb: mse === 0
      ? "infinite"
      : 10 * Math.log10(65025 / mse),
  };
}

function nearestMappings(statistics, bank) {
  return statistics.sources.map((source) => {
    let bestIndex = 0;
    let bestError = weightedPaletteError(source, bank[0]);
    for (let index = 1; index < bank.length; index += 1) {
      const error = weightedPaletteError(source, bank[index]);
      if (error < bestError) {
        bestError = error;
        bestIndex = index;
      }
    }
    return {
      gopIndex: source.gopIndex,
      slot: source.slot,
      bankIndex: bestIndex,
      error: bestError,
    };
  });
}

function combineExactSources(sources) {
  const combined = new Map();
  for (const source of sources) {
    let entry = combined.get(source.key);
    if (!entry) {
      entry = {
        key: source.key,
        palette: Uint8Array.from(source.palette),
        colorUses: new Float64Array(COLORS_PER_PALETTE),
        pixelUses: 0,
      };
      combined.set(source.key, entry);
    }
    for (let color = 0; color < COLORS_PER_PALETTE; color += 1) {
      entry.colorUses[color] += source.colorUses[color];
    }
    entry.pixelUses += source.pixelUses;
  }
  return Array.from(combined.values());
}

function weightedPaletteError(source, candidate) {
  let error = 0;
  for (let color = 0; color < COLORS_PER_PALETTE; color += 1) {
    const weight = source.colorUses[color];
    if (weight === 0) continue;
    const offset = color * COLOR_BYTES;
    const red = source.palette[offset] - candidate[offset];
    const green = source.palette[offset + 1] - candidate[offset + 1];
    const blue = source.palette[offset + 2] - candidate[offset + 2];
    error += weight * (red * red + green * green + blue * blue);
  }
  return error;
}

function accumulateFrameUses(
  blocks,
  colorUses,
  width,
  height,
  blocksX,
  blocksY,
  fullBlocks,
) {
  for (let blockY = 0; blockY < blocksY; blockY += 1) {
    for (let blockX = 0; blockX < blocksX; blockX += 1) {
      const blockIndex = blockY * blocksX + blockX;
      const record = blockIndex * RECORD_BYTES;
      const useBase = blocks[record] * COLORS_PER_PALETTE;
      const local0 = blocks[record + 1];
      const local1 = blocks[record + 2];
      const local2 = blocks[record + 3];
      const local3 = blocks[record + 4];
      for (let row = 0; row < 4; row += 1) {
        const y = blockY * 4 + row;
        if (!fullBlocks && y >= height) continue;
        const pattern = blocks[record + PATTERN_OFFSET + row];
        for (let column = 0; column < 4; column += 1) {
          const x = blockX * 4 + column;
          if (!fullBlocks && x >= width) continue;
          const local = (pattern >> (6 - column * 2)) & 3;
          const color = local === 0 ? local0
            : local === 1 ? local1
              : local === 2 ? local2 : local3;
          colorUses[useBase + color] += 1;
        }
      }
    }
  }
}

function compareSourcePriority(left, right) {
  if (left.pixelUses !== right.pixelUses) {
    return right.pixelUses - left.pixelUses;
  }
  return left.key < right.key ? -1 : left.key > right.key ? 1 : 0;
}

function bytesKey(bytes) {
  return Buffer.from(bytes).toString("hex");
}

function sum(values) {
  let result = 0;
  for (const value of values) result += value;
  return result;
}

function normalizeInteger(value, minimum, maximum, label) {
  const number = Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) {
    throw new RangeError(`${label} must be ${minimum}..${maximum}`);
  }
  return number;
}

function asUint8Array(value) {
  if (value instanceof Uint8Array) return value;
  if (ArrayBuffer.isView(value)) {
    return new Uint8Array(value.buffer, value.byteOffset, value.byteLength);
  }
  if (value instanceof ArrayBuffer) return new Uint8Array(value);
  return Uint8Array.from(value || []);
}

module.exports = {
  analyzeSuperpalettes,
  buildSuperpalette,
  collectPaletteStatistics,
  evaluateSuperpalette,
  materializeNearestActiveBanks,
  constants: {
    ACTIVE_PALETTES,
    COLORS_PER_PALETTE,
    PALETTE_BYTES,
  },
};
