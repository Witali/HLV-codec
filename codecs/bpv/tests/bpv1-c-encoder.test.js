"use strict";

const assert = require("node:assert/strict");
const childProcess = require("node:child_process");
const crypto = require("node:crypto");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const bpv = require("../tools/bpv1-file.js");
const codec = require("../src/bpv1-codec.js");
const y4m = require("../tools/y4m.js");

const packageRoot = path.resolve(__dirname, "..");
const repositoryRoot = path.resolve(packageRoot, "..", "..");
const defaultExecutable = process.platform === "win32"
  ? path.join(repositoryRoot, "build", "msvc", "bpv1enc.exe")
  : path.join(packageRoot, "bpv1enc");
const executable = process.env.BPV1ENC || defaultExecutable;

function makeFrame(width, height, shift) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const output = (y * width + x) * 4;
      const checker = ((x + shift) >> 3) ^ (y >> 3);
      rgba[output] = (x * 3 + shift * 11) & 255;
      rgba[output + 1] = (y * 5 + (checker & 1) * 80) & 255;
      rgba[output + 2] = ((x + y) * 2 + (checker & 3) * 30) & 255;
      rgba[output + 3] = 255;
    }
  }
  return rgba;
}

function runEncoder(
  input,
  output,
  report,
  threads,
  audio = null,
  activePalettes = true,
) {
  const arguments_ = [
    input,
    output,
    "--threads", String(threads),
    "--gop", "3",
    "--lambda", "64",
    "--sample-blocks", "96",
    "--samples-per-frame", "16",
    "--block-iterations", "2",
    "--color-iterations", "2",
    "--colors-per-cluster", "128",
    "--report", report,
    "--no-progress",
    "--force",
  ];
  arguments_.push(activePalettes ? "--active-palettes" : "--fixed-palettes");
  if (audio) {
    arguments_.push("--audio-u8", audio, "--audio-rate", "16000");
  }
  const result = childProcess.spawnSync(executable, arguments_, {
    cwd: packageRoot,
    encoding: "utf8",
    maxBuffer: 16 * 1024 * 1024,
  });
  if (result.status !== 0) {
    throw new Error(
      `bpv1enc failed (${result.status})\nstdout:\n${result.stdout}\n` +
      `stderr:\n${result.stderr}`,
    );
  }
}

if (!fs.existsSync(executable)) {
  throw new Error(`BPV1 C encoder was not found: ${executable}`);
}

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), "bpv1-c-encoder-"));
try {
  const input = path.join(temporary, "input.y4m");
  const output1 = path.join(temporary, "output-1.bpv1");
  const output4 = path.join(temporary, "output-4.bpv1");
  const report1 = path.join(temporary, "report-1.json");
  const report4 = path.join(temporary, "report-4.json");
  const audio = path.join(temporary, "audio.u8");
  const outputAudio = path.join(temporary, "output-audio.bpv1");
  const reportAudio = path.join(temporary, "report-audio.json");
  const outputFixed = path.join(temporary, "output-fixed.bpv1");
  const reportFixed = path.join(temporary, "report-fixed.json");
  const width = 64;
  const height = 64;
  const parts = [y4m.y4mHeader(width, height, 24, 1)];
  for (let frame = 0; frame < 6; frame += 1) {
    parts.push(y4m.y4mFrame(
      makeFrame(width, height, frame < 3 ? frame * 2 : 4),
      width,
      height,
    ));
  }
  fs.writeFileSync(input, Buffer.concat(parts));
  fs.writeFileSync(audio, Buffer.from(
    Array.from({ length: 4000 }, (_, index) => index & 255),
  ));

  runEncoder(input, output1, report1, 1);
  runEncoder(input, output4, report4, 4);
  runEncoder(input, outputAudio, reportAudio, 4, audio);
  runEncoder(input, outputFixed, reportFixed, 1, null, false);

  const bytes1 = fs.readFileSync(output1);
  const bytes4 = fs.readFileSync(output4);
  assert.equal(
    crypto.createHash("sha256").update(bytes1).digest("hex"),
    crypto.createHash("sha256").update(bytes4).digest("hex"),
    "parallel GOP scheduling must not change the BPV1 bitstream",
  );

  const info = bpv.walkFrames(bytes4);
  assert.equal(info.version, 4);
  assert.equal(info.width, width);
  assert.equal(info.height, height);
  assert.equal(info.frameCount, 6);
  assert.equal(info.fpsNumerator, 24);
  assert.equal(info.fpsDenominator, 1);
  assert.equal(info.keyframes, 2);
  assert.equal(info.paletteUpdates, 2);
  const decoded = codec.decodeVideo(bytes4);
  assert.equal(decoded.frames.length, 6);
  assert.equal(
    codec.renderFrame(decoded, 0).length,
    width * height * 4,
  );

  const audioFrames = [];
  const audioInfo = bpv.walkFrames(
    fs.readFileSync(outputAudio),
    (frame) => audioFrames.push(frame.audio),
  );
  assert.equal(audioInfo.version, 4);
  assert.equal(audioInfo.audioCodec, 1);
  assert.equal(audioInfo.audioSampleRate, 16000);
  assert.equal(audioInfo.audioChannels, 1);
  assert.equal(audioInfo.audioBytes, 4000);
  assert.deepEqual(
    Buffer.concat(audioFrames.map((samples) => Buffer.from(samples))),
    fs.readFileSync(audio),
  );

  const fixedInfo = bpv.walkFrames(fs.readFileSync(outputFixed));
  assert.equal(fixedInfo.version, 2);
  assert.equal(fixedInfo.paletteUpdates, 0);

  const report = JSON.parse(fs.readFileSync(report4, "utf8"));
  assert.equal(report.encoder, "native C11");
  assert.equal(report.threads, 4);
  assert.equal(report.frames, 6);
  assert.equal(report.paletteMode, "active-gop");
  assert.equal(report.paletteUpdates, 2);
  assert.ok(Number.isFinite(report.rgbPsnrDb));
  assert.ok(report.rgbPsnrDb > 0);
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}

console.log("BPV1 C encoder compatibility and determinism tests passed");
