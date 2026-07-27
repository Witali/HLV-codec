"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const y4m = require("../tools/y4m.js");

const packageRoot = path.resolve(__dirname, "..");

function run(script, arguments_) {
  const result = childProcess.spawnSync(
    process.execPath,
    [path.join(packageRoot, "tools", script), ...arguments_],
    {
      cwd: packageRoot,
      encoding: "utf8",
      maxBuffer: 16 * 1024 * 1024,
    },
  );
  if (result.status !== 0) {
    throw new Error(
      `${script} failed (${result.status})\nstdout:\n${result.stdout}\nstderr:\n${result.stderr}`,
    );
  }
  return result;
}

function makeFrame(width, height, shift) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const output = (y * width + x) * 4;
      const band = (Math.floor((x + shift) / 4) + Math.floor(y / 4)) & 3;
      const colors = [
        [230, 40, 30],
        [30, 210, 70],
        [35, 80, 225],
        [220, 205, 45],
      ];
      rgba[output] = colors[band][0];
      rgba[output + 1] = colors[band][1];
      rgba[output + 2] = colors[band][2];
      rgba[output + 3] = 255;
    }
  }
  return rgba;
}

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "bpv1-cli-"));
try {
  const input = path.join(temporary, "input.y4m");
  const encoded = path.join(temporary, "output.bpv1");
  const decoded = path.join(temporary, "decoded.y4m");
  const damaged = path.join(temporary, "damaged.bpv1");
  const width = 16;
  const height = 8;
  const source = [
    y4m.y4mHeader(width, height, 12, 1),
    y4m.y4mFrame(makeFrame(width, height, 0), width, height),
    y4m.y4mFrame(makeFrame(width, height, 4), width, height),
    y4m.y4mFrame(makeFrame(width, height, 4), width, height),
  ];
  fs.writeFileSync(input, Buffer.concat(source));

  run("bpv1enc.js", [
    input,
    encoded,
    "--lambda", "16",
    "--gop", "12",
    "--search-radius", "2",
    "--max-sample-blocks", "64",
    "--max-pixels-per-cluster", "256",
    "--block-iterations", "2",
    "--color-iterations", "2",
    "--no-progress",
  ]);
  const info = JSON.parse(run("bpv1info.js", [encoded, "--json"]).stdout);
  assert.equal(info.version, 6);
  assert.equal(info.width, width);
  assert.equal(info.height, height);
  assert.equal(info.frameCount, 3);
  assert.equal(info.fpsNumerator, 12);
  assert.equal(info.fpsDenominator, 1);
  assert.equal(info.maxPatternDictionary, 0);
  assert.deepEqual(
    Object.keys(info.modeCounts),
    ["skip", "motion", "blockDictionary", "raw"],
  );
  assert.ok(info.modeCounts.skip > 0 || info.modeCounts.motion > 0);

  run("bpv1dec.js", [encoded, decoded, "--no-progress"]);
  const reconstructed = y4m.parseY4m(fs.readFileSync(decoded));
  assert.equal(reconstructed.width, width);
  assert.equal(reconstructed.height, height);
  assert.equal(reconstructed.frames.length, 3);
  assert.equal(reconstructed.fpsNumerator, 12);

  const validBytes = fs.readFileSync(encoded);
  fs.writeFileSync(damaged, validBytes.subarray(0, validBytes.length - 1));
  const invalid = childProcess.spawnSync(
    process.execPath,
    [path.join(packageRoot, "tools", "bpv1info.js"), damaged],
    { cwd: packageRoot, encoding: "utf8" },
  );
  assert.notEqual(invalid.status, 0);
  assert.match(invalid.stderr, /Truncated|mismatch/);
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}

console.log("BPV1 CLI round-trip tests passed");
