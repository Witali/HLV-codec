#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const bpv1 = require("./bpv1-file.js");
const y4m = require("./y4m.js");

function usage() {
  return `Usage:
  node tools/bpv1dec.js <input.bpv1|-> <output.y4m|-> [--no-progress]

The decoder validates every frame while converting BPV1 v1 through v5 to an
8-bit YUV 4:2:0 Y4M stream.
`;
}

function parseArguments(argv) {
  const positional = [];
  let progress = true;
  let help = false;
  for (const argument of argv) {
    if (argument === "-h" || argument === "--help") help = true;
    else if (argument === "--no-progress") progress = false;
    else if (argument.startsWith("-") && argument !== "-") {
      throw new RangeError(`Unknown option: ${argument}`);
    } else {
      positional.push(argument);
    }
  }
  if (!help && positional.length !== 2) {
    throw new RangeError("Input and output paths are required");
  }
  return { help, input: positional[0], output: positional[1], progress };
}

function main() {
  const options = parseArguments(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(usage());
    return;
  }
  const bytes = options.input === "-" ? fs.readFileSync(0) : fs.readFileSync(options.input);
  const header = bpv1.parseHeader(bytes);
  const descriptor = options.output === "-"
    ? 1
    : fs.openSync(options.output, "w");
  let writtenFrames = 0;
  try {
    fs.writeSync(
      descriptor,
      y4m.y4mHeader(
        header.width,
        header.height,
        header.fpsNumerator,
        header.fpsDenominator,
      ),
    );
    const summary = bpv1.walkFrames(bytes, (frame, parsedHeader) => {
      const rgba = bpv1.renderFrameRgba(frame, parsedHeader);
      fs.writeSync(descriptor, y4m.y4mFrame(rgba, parsedHeader.width, parsedHeader.height));
      writtenFrames += 1;
      if (options.progress &&
          (writtenFrames === parsedHeader.frameCount || writtenFrames % 100 === 0)) {
        process.stderr.write(
          `BPV1 decode: ${writtenFrames}/${parsedHeader.frameCount} frames\n`,
        );
      }
    });
    process.stderr.write(`${JSON.stringify(summary, null, 2)}\n`);
  } finally {
    if (descriptor !== 1) fs.closeSync(descriptor);
  }
}

try {
  main();
} catch (error) {
  process.stderr.write(`bpv1dec: ${error && error.message ? error.message : error}\n`);
  process.exitCode = 1;
}
