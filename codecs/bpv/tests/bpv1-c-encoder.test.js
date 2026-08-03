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
  activePaletteFile = null,
  extraArguments = [],
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
  if (activePaletteFile) {
    arguments_.push("--active-palette-file", activePaletteFile);
  }
  if (audio) {
    arguments_.push("--audio-u8", audio, "--audio-rate", "16000");
  }
  arguments_.push(...extraArguments);
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
  const outputCpu = path.join(temporary, "output-cpu.bpv1");
  const reportCpu = path.join(temporary, "report-cpu.json");
  const outputPixel = path.join(temporary, "output-pixel.bpv1");
  const reportPixel = path.join(temporary, "report-pixel.json");
  const outputPixelCpu = path.join(temporary, "output-pixel-cpu.bpv1");
  const reportPixelCpu = path.join(temporary, "report-pixel-cpu.json");
  const audio = path.join(temporary, "audio.u8");
  const outputAudio = path.join(temporary, "output-audio.bpv1");
  const reportAudio = path.join(temporary, "report-audio.json");
  const imaAudio = path.join(temporary, "audio.s16le");
  const outputImaAudio = path.join(temporary, "output-ima-audio.bpv1");
  const reportImaAudio = path.join(temporary, "report-ima-audio.json");
  const outputFixed = path.join(temporary, "output-fixed.bpv1");
  const reportFixed = path.join(temporary, "report-fixed.json");
  const activePaletteFile = path.join(temporary, "active-palettes.rgb");
  const outputOverride1 = path.join(temporary, "output-override-1.bpv1");
  const outputOverride4 = path.join(temporary, "output-override-4.bpv1");
  const reportOverride1 = path.join(temporary, "report-override-1.json");
  const reportOverride4 = path.join(temporary, "report-override-4.json");
  const sceneInput = path.join(temporary, "scene-input.y4m");
  const sceneOutput = path.join(temporary, "scene-output.bpv1");
  const sceneReport = path.join(temporary, "scene-report.json");
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
  const sceneParts = [y4m.y4mHeader(width, height, 24, 1)];
  for (let frame = 0; frame < 8; frame += 1) {
    const value = frame < 4 ? 0 : 255;
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let pixel = 0; pixel < width * height; pixel += 1) {
      const offset = pixel * 4;
      rgba[offset] = value;
      rgba[offset + 1] = value;
      rgba[offset + 2] = value;
      rgba[offset + 3] = 255;
    }
    sceneParts.push(y4m.y4mFrame(rgba, width, height));
  }
  fs.writeFileSync(sceneInput, Buffer.concat(sceneParts));
  fs.writeFileSync(audio, Buffer.from(
    Array.from({ length: 4000 }, (_, index) => index & 255),
  ));
  const imaSamples = Buffer.alloc(8000 * 2);
  for (let sample = 0; sample < 8000; sample += 1) {
    imaSamples.writeInt16LE(
      Math.round(Math.sin(sample * 2 * Math.PI * 440 / 32000) * 24000),
      sample * 2,
    );
  }
  fs.writeFileSync(imaAudio, imaSamples);

  runEncoder(input, output1, report1, 1);
  runEncoder(input, output4, report4, 4);
  runEncoder(input, outputAudio, reportAudio, 4, audio);
  runEncoder(
    input,
    outputImaAudio,
    reportImaAudio,
    4,
    null,
    true,
    null,
    ["--audio-ima-s16le", imaAudio, "--audio-rate", "32000"],
  );
  runEncoder(input, outputFixed, reportFixed, 1, null, false);
  runEncoder(
    input,
    outputPixel,
    reportPixel,
    1,
    null,
    true,
    null,
    ["--pixel-motion"],
  );
  runEncoder(
    sceneInput,
    sceneOutput,
    sceneReport,
    1,
    null,
    true,
    null,
    [
      "--gop", "12",
      "--min-gop", "2",
      "--scene-threshold", "0.35",
      "--candidate-palettes", "64",
    ],
  );

  const bytes1 = fs.readFileSync(output1);
  const bytes4 = fs.readFileSync(output4);
  const pixelBytes = fs.readFileSync(outputPixel);
  assert.equal(bpv.walkFrames(pixelBytes).version, 7);
  assert.equal(
    JSON.parse(fs.readFileSync(reportPixel, "utf8")).motionUnits,
    "pixels",
  );
  const primaryReport = JSON.parse(fs.readFileSync(report1, "utf8"));
  if (primaryReport.computeBackend === "cuda") {
    runEncoder(
      input,
      outputCpu,
      reportCpu,
      1,
      null,
      true,
      null,
      ["--device", "cpu"],
    );
    assert.deepEqual(
      fs.readFileSync(outputCpu),
      bytes1,
      "CUDA block search must match the CPU BPV1 bitstream",
    );
    assert.equal(
      JSON.parse(fs.readFileSync(reportCpu, "utf8")).computeBackend,
      "cpu",
    );
    runEncoder(
      input,
      outputPixelCpu,
      reportPixelCpu,
      1,
      null,
      true,
      null,
      ["--pixel-motion", "--device", "cpu"],
    );
    assert.deepEqual(
      fs.readFileSync(outputPixelCpu),
      pixelBytes,
      "CUDA and CPU pixel-motion encoding must match",
    );
  }
  const activeBanks = [];
  bpv.walkFrames(bytes1, (frame) => {
    if (frame.keyframe) activeBanks.push(Buffer.from(frame.palette));
  });
  fs.writeFileSync(activePaletteFile, Buffer.concat(activeBanks));
  runEncoder(
    input,
    outputOverride1,
    reportOverride1,
    1,
    null,
    true,
    activePaletteFile,
  );
  runEncoder(
    input,
    outputOverride4,
    reportOverride4,
    4,
    null,
    true,
    activePaletteFile,
  );
  assert.equal(
    crypto.createHash("sha256").update(bytes1).digest("hex"),
    crypto.createHash("sha256").update(bytes4).digest("hex"),
    "parallel GOP scheduling must not change the BPV1 bitstream",
  );
  assert.equal(
    crypto.createHash("sha256")
      .update(fs.readFileSync(outputOverride1)).digest("hex"),
    crypto.createHash("sha256")
      .update(fs.readFileSync(outputOverride4)).digest("hex"),
    "active palette overrides must be deterministic across worker counts",
  );

  const info = bpv.walkFrames(bytes4);
  assert.equal(info.version, 6);
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
  assert.equal(audioInfo.version, 6);
  assert.equal(audioInfo.audioCodec, 1);
  assert.equal(audioInfo.audioSampleRate, 16000);
  assert.equal(audioInfo.audioChannels, 1);
  assert.equal(audioInfo.audioBytes, 4000);
  assert.deepEqual(
    Buffer.concat(audioFrames.map((samples) => Buffer.from(samples))),
    fs.readFileSync(audio),
  );

  const imaFrames = [];
  const imaInfo = bpv.walkFrames(
    fs.readFileSync(outputImaAudio),
    (frame) => imaFrames.push(Buffer.from(frame.audio)),
  );
  assert.equal(imaInfo.version, 6);
  assert.equal(imaInfo.audioCodec, 2);
  assert.equal(imaInfo.audioSampleRate, 32000);
  assert.equal(imaInfo.audioChannels, 1);
  let imaDecodedSamples = 0;
  for (const block of imaFrames) {
    assert.ok(block.length > 512, "IMA packet must exceed the refill buffer");
    assert.ok(block[2] <= 88);
    assert.equal(block[3], 0);
    const sampleCount = block.readUInt16LE(4);
    assert.ok(sampleCount > 0 && sampleCount <= 4096);
    assert.equal(block.length, 6 + Math.floor(sampleCount / 2));
    imaDecodedSamples += sampleCount;
  }
  assert.equal(imaDecodedSamples, 8000);
  assert.equal(
    JSON.parse(fs.readFileSync(reportImaAudio, "utf8")).audioCodec,
    "ima_adpcm",
  );

  const fixedInfo = bpv.walkFrames(fs.readFileSync(outputFixed));
  assert.equal(fixedInfo.version, 6);
  assert.equal(fixedInfo.paletteUpdates, 2);

  const report = JSON.parse(fs.readFileSync(report4, "utf8"));
  assert.ok(["cpu", "cuda"].includes(report.computeBackend));
  assert.equal(
    report.encoder,
    report.computeBackend === "cuda"
      ? "native C11 + CUDA"
      : "native C11",
  );
  assert.equal(report.threads, 4);
  assert.equal(report.frames, 6);
  assert.equal(report.paletteMode, "active-gop");
  assert.equal(report.paletteUpdates, 2);
  assert.equal(report.candidatePaletteCount, 8);
  assert.equal(report.paletteSearch, "rgb-lut");
  assert.equal(report.paletteIndexBitsPerChannel, 4);
  assert.ok(Number.isFinite(report.rgbPsnrDb));
  assert.ok(report.rgbPsnrDb > 0);
  assert.equal(report.version, 6);
  assert.ok(Number.isInteger(report.modeCounts.raw));
  assert.equal(
    report.modeCounts.raw,
    report.rawSubtypeCounts.oneColor +
      report.rawSubtypeCounts.twoColor +
      report.rawSubtypeCounts.fourColor +
      report.rawSubtypeCounts.direct5To8 +
      report.rawSubtypeCounts.direct9To16,
  );
  const overrideReport =
    JSON.parse(fs.readFileSync(reportOverride4, "utf8"));
  assert.equal(overrideReport.paletteMode, "active-override");

  const sceneKeyframes = [];
  const sceneInfo = bpv.walkFrames(
    fs.readFileSync(sceneOutput),
    (frame) => {
      if (frame.keyframe) sceneKeyframes.push(frame.frameIndex);
    },
  );
  assert.deepEqual(
    sceneKeyframes,
    [0, 4],
    "a hard cut must start a new GOP at the changed frame",
  );
  assert.equal(sceneInfo.keyframes, 2);
  const parsedSceneReport =
    JSON.parse(fs.readFileSync(sceneReport, "utf8"));
  assert.equal(parsedSceneReport.sceneKeyframes, 1);
  assert.equal(parsedSceneReport.paletteUpdates, 2);
  assert.equal(parsedSceneReport.candidatePaletteCount, 64);
  assert.deepEqual(
    parsedSceneReport.keyframes.map((keyframe) => keyframe.frame),
    [0, 4],
  );
  assert.equal(parsedSceneReport.keyframes[1].reason, "scene");
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}

console.log("BPV1 C encoder compatibility and determinism tests passed");
