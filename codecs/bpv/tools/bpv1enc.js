#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const autoPalette = require("../src/bpv1-auto-palette.js");
const rateDistortion = require("../src/bpv1-rate-distortion.js");
const y4m = require("./y4m.js");

function usage() {
  return `Usage:
  node tools/bpv1enc.js <input.y4m|-> <output.bpv1|-> [options]

Options:
  --lambda N                    RD multiplier (default: 64)
  --candidate-palettes N        Nearby BPAL palettes to test (default: 3)
  --gop N                       Keyframe interval (default: 30)
  --search-radius N             Motion radius in 4x4 blocks (default: 4)
  --max-frames N                Stop after N frames (default: all)
  --max-sample-blocks N         Palette-training block sample (default: 32768)
  --max-pixels-per-cluster N    Palette color sample (default: 8192)
  --block-iterations N          Block k-means iterations (default: 10)
  --color-iterations N          Color k-means iterations (default: 10)
  --color-space oklab|rgb       Palette-training color space (default: oklab)
  --max-block-dictionary N      Full-block dictionary entries (default: 256)
  --max-pattern-dictionary N    Pattern dictionary entries (default: 256)
  --no-rd                       Use nearest-palette encoding without RD
  --no-progress                 Suppress progress messages
  -h, --help                    Show this help

Input and output are video-only. Feed normalized 8-bit YUV 4:2:0 Y4M, for
example: ffmpeg -i input.mov -an -vf scale=320:200 -pix_fmt yuv420p -f yuv4mpegpipe -
`;
}

function parseArguments(argv) {
  const options = {
    lambda: 64,
    candidatePaletteCount: 3,
    keyframeInterval: 30,
    searchRadius: 4,
    maxFrames: 0,
    maximumSampleBlocks: 32768,
    maximumPixelsPerCluster: 8192,
    blockClusterIterations: 10,
    colorClusterIterations: 10,
    colorSpace: "oklab",
    maxBlockDictionary: 256,
    maxPatternDictionary: 256,
    rateDistortion: true,
    progress: true,
  };
  const positional = [];
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    const value = () => {
      if (index + 1 >= argv.length) throw new RangeError(`${argument} requires a value`);
      index += 1;
      return argv[index];
    };
    if (argument === "-h" || argument === "--help") {
      options.help = true;
    } else if (argument === "--lambda") {
      options.lambda = finiteNumber(value(), argument, 0, 1e9);
    } else if (argument === "--candidate-palettes") {
      options.candidatePaletteCount = integer(value(), argument, 1, 8);
    } else if (argument === "--gop") {
      options.keyframeInterval = integer(value(), argument, 1, 65535);
    } else if (argument === "--search-radius") {
      options.searchRadius = integer(value(), argument, 0, 127);
    } else if (argument === "--max-frames") {
      options.maxFrames = integer(value(), argument, 0, 0xffffffff);
    } else if (argument === "--max-sample-blocks") {
      options.maximumSampleBlocks = integer(value(), argument, 64, 262144);
    } else if (argument === "--max-pixels-per-cluster") {
      options.maximumPixelsPerCluster = integer(value(), argument, 256, 65536);
    } else if (argument === "--block-iterations") {
      options.blockClusterIterations = integer(value(), argument, 1, 32);
    } else if (argument === "--color-iterations") {
      options.colorClusterIterations = integer(value(), argument, 1, 32);
    } else if (argument === "--color-space") {
      options.colorSpace = value();
      if (options.colorSpace !== "oklab" && options.colorSpace !== "rgb") {
        throw new RangeError("--color-space must be oklab or rgb");
      }
    } else if (argument === "--max-block-dictionary") {
      options.maxBlockDictionary = integer(value(), argument, 1, 65535);
    } else if (argument === "--max-pattern-dictionary") {
      options.maxPatternDictionary = integer(value(), argument, 1, 65535);
    } else if (argument === "--no-rd") {
      options.rateDistortion = false;
    } else if (argument === "--no-progress") {
      options.progress = false;
    } else if (argument.startsWith("-") && argument !== "-") {
      throw new RangeError(`Unknown option: ${argument}`);
    } else {
      positional.push(argument);
    }
  }
  if (!options.help && positional.length !== 2) {
    throw new RangeError("Input and output paths are required");
  }
  options.input = positional[0];
  options.output = positional[1];
  return options;
}

function integer(value, name, minimum, maximum) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < minimum || parsed > maximum) {
    throw new RangeError(`${name} must be an integer in ${minimum}..${maximum}`);
  }
  return parsed;
}

function finiteNumber(value, name, minimum, maximum) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed < minimum || parsed > maximum) {
    throw new RangeError(`${name} must be in ${minimum}..${maximum}`);
  }
  return parsed;
}

function readInput(path) {
  return path === "-" ? fs.readFileSync(0) : fs.readFileSync(path);
}

function writeOutput(path, bytes) {
  const buffer = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (path === "-") process.stdout.write(buffer);
  else fs.writeFileSync(path, buffer);
}

function createProgressReporter(enabled) {
  const reported = new Map();
  return enabled
    ? ({ stage, completed, total }) => {
      const percentage = total === 0 ? 100 : Math.floor(completed * 100 / total);
      const bucket = percentage === 100 ? 100 : Math.floor(percentage / 10) * 10;
      if (reported.get(stage) === bucket) return;
      reported.set(stage, bucket);
      process.stderr.write(`BPV1 ${stage}: ${bucket}%\n`);
    }
    : undefined;
}

function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(usage());
    return;
  }

  const video = y4m.parseY4m(readInput(options.input), {
    maxFrames: options.maxFrames,
  });
  const settings = {
    lambda: options.lambda,
    candidatePaletteCount: options.candidatePaletteCount,
    keyframeInterval: options.keyframeInterval,
    searchRadius: options.searchRadius,
    maximumSampleBlocks: options.maximumSampleBlocks,
    maximumPixelsPerCluster: options.maximumPixelsPerCluster,
    blockClusterIterations: options.blockClusterIterations,
    colorClusterIterations: options.colorClusterIterations,
    colorSpace: options.colorSpace,
    maxBlockDictionary: options.maxBlockDictionary,
    maxPatternDictionary: options.maxPatternDictionary,
    onProgress: createProgressReporter(options.progress),
  };
  const encoded = options.rateDistortion
    ? rateDistortion.compressRgbaVideoRateDistortion(video, settings)
    : autoPalette.compressRgbaVideo(video, settings);
  writeOutput(options.output, encoded.bytes);

  const report = {
    codec: "BPV1",
    version: 2,
    width: video.width,
    height: video.height,
    frames: video.frames.length,
    fps: `${video.fpsNumerator}/${video.fpsDenominator}`,
    bytes: encoded.bytes.length,
    bitsPerPixelPerFrame: encoded.stats.bitsPerPixel,
    modeCounts: encoded.stats.modeCounts,
    training: encoded.training,
    rateDistortion: options.rateDistortion ? encoded.rateDistortion : null,
  };
  process.stderr.write(`${JSON.stringify(report, null, 2)}\n`);
}

try {
  main();
} catch (error) {
  process.stderr.write(`bpv1enc: ${error && error.message ? error.message : error}\n`);
  process.exitCode = 1;
}
