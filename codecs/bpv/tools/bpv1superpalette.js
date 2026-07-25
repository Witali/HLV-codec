#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const superpalette = require("../src/bpv1-superpalette.js");

function usage() {
  return `Usage:
  node tools/bpv1superpalette.js <input-v4.bpv1> [options]

Options:
  --bank-sizes LIST   comma-separated sizes (default 128,256,512)
  --target-psnr LIST  perturbation PSNR limits (default 60,50,45,40)
  --output FILE       write JSON report to FILE instead of stdout
  -h, --help          show this help

The tool does not modify the BPV stream. It measures deterministic
file-level superpalette mappings against the colours actually used by each
GOP and estimates complete-file size including literal palette fallbacks.
`;
}

function main() {
  const arguments_ = process.argv.slice(2);
  if (arguments_.includes("-h") || arguments_.includes("--help")) {
    process.stdout.write(usage());
    return;
  }
  let input = null;
  let output = null;
  let bankSizes = [128, 256, 512];
  let targetPsnr = [60, 50, 45, 40];
  for (let index = 0; index < arguments_.length; index += 1) {
    const argument = arguments_[index];
    if (argument === "--bank-sizes") {
      bankSizes = parseNumberList(arguments_[++index], 1, 4096, "bank size");
    } else if (argument === "--target-psnr") {
      targetPsnr = parseNumberList(arguments_[++index], 1, 200, "PSNR");
    } else if (argument === "--output") {
      output = arguments_[++index];
      if (!output) throw new RangeError("--output requires a path");
    } else if (argument.startsWith("-")) {
      throw new RangeError(`Unknown option: ${argument}`);
    } else if (input === null) {
      input = argument;
    } else {
      throw new RangeError(`Unexpected positional argument: ${argument}`);
    }
  }
  if (!input) throw new RangeError("An input BPV1 v4 file is required");
  const report = superpalette.analyzeSuperpalettes(
    fs.readFileSync(input),
    { bankSizes, targetPsnr },
  );
  report.input = path.resolve(input);
  const json = `${JSON.stringify(report, null, 2)}\n`;
  if (output) fs.writeFileSync(output, json);
  else process.stdout.write(json);
}

function parseNumberList(value, minimum, maximum, label) {
  if (!value) throw new RangeError(`${label} list is required`);
  const numbers = value.split(",").map((entry) => Number(entry));
  if (numbers.length === 0 ||
      numbers.some((number) =>
        !Number.isFinite(number) ||
        !Number.isInteger(number) ||
        number < minimum ||
        number > maximum)) {
    throw new RangeError(
      `${label} values must be integers in ${minimum}..${maximum}`,
    );
  }
  return numbers;
}

try {
  main();
} catch (error) {
  process.stderr.write(
    `bpv1superpalette: ${error && error.message ? error.message : error}\n`,
  );
  process.exitCode = 1;
}
