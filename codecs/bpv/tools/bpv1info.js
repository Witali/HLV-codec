#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const bpv1 = require("./bpv1-file.js");

function usage() {
  return `Usage:
  node tools/bpv1info.js <input.bpv1|-> [--json]

Fully validates the stream and reports its header, frame-size range and block
mode counts without allocating a full RGB framebuffer.
`;
}

function main() {
  const arguments_ = process.argv.slice(2);
  if (arguments_.includes("-h") || arguments_.includes("--help")) {
    process.stdout.write(usage());
    return;
  }
  const json = arguments_.includes("--json");
  const positional = arguments_.filter((argument) => argument !== "--json");
  if (positional.length !== 1) throw new RangeError("An input path is required");
  const bytes = positional[0] === "-" ? fs.readFileSync(0) : fs.readFileSync(positional[0]);
  const summary = bpv1.walkFrames(bytes);
  if (json) {
    process.stdout.write(`${JSON.stringify(summary, null, 2)}\n`);
    return;
  }
  process.stdout.write(
    [
      `BPV1 v${summary.version}: ${summary.width}x${summary.height}`,
      `Frames: ${summary.frameCount} at ${summary.fpsNumerator}/${summary.fpsDenominator} fps`,
      `Duration: ${summary.durationSeconds.toFixed(3)} s`,
      `File: ${summary.fileBytes} bytes`,
      `Keyframes: ${summary.keyframes} (configured interval ${summary.keyframeInterval})`,
      `Frame bytes: min ${summary.minimumFrameBytes}, mean ${summary.meanFrameBytes.toFixed(2)}, max ${summary.maximumFrameBytes}`,
      `Modes: ${Object.entries(summary.modeCounts).map(([name, count]) => `${name}=${count}`).join(", ")}`,
    ].join("\n") + "\n",
  );
}

try {
  main();
} catch (error) {
  process.stderr.write(`bpv1info: ${error && error.message ? error.message : error}\n`);
  process.exitCode = 1;
}
