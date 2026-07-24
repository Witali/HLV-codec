"use strict";

function asBuffer(value) {
  if (Buffer.isBuffer(value)) return value;
  if (ArrayBuffer.isView(value)) {
    return Buffer.from(value.buffer, value.byteOffset, value.byteLength);
  }
  if (value instanceof ArrayBuffer) return Buffer.from(value);
  return Buffer.from(value || []);
}

function nextLine(bytes, offset) {
  const end = bytes.indexOf(0x0a, offset);
  if (end < 0) throw new RangeError("Truncated Y4M line");
  let textEnd = end;
  if (textEnd > offset && bytes[textEnd - 1] === 0x0d) textEnd -= 1;
  return {
    text: bytes.toString("ascii", offset, textEnd),
    next: end + 1,
  };
}

function positiveInteger(value, name) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed <= 0) {
    throw new RangeError(`${name} must be a positive integer`);
  }
  return parsed;
}

function parseFrameRate(value) {
  const match = /^(\d+):(\d+)$/.exec(value || "");
  if (!match) throw new RangeError("Y4M frame rate must use Fnumerator:denominator");
  return {
    numerator: positiveInteger(match[1], "frame-rate numerator"),
    denominator: positiveInteger(match[2], "frame-rate denominator"),
  };
}

function parseY4m(input, options) {
  const settings = options || {};
  const maximumFrames = settings.maxFrames === undefined
    ? 0
    : Number(settings.maxFrames);
  if (!Number.isInteger(maximumFrames) || maximumFrames < 0) {
    throw new RangeError("maxFrames must be zero or a positive integer");
  }

  const bytes = asBuffer(input);
  const headerLine = nextLine(bytes, 0);
  const tokens = headerLine.text.split(/\s+/);
  if (tokens.shift() !== "YUV4MPEG2") throw new RangeError("Invalid Y4M magic");

  const fields = {};
  for (const token of tokens) {
    if (token.length > 1) fields[token[0]] = token.slice(1);
  }
  const width = positiveInteger(fields.W, "width");
  const height = positiveInteger(fields.H, "height");
  const fps = parseFrameRate(fields.F || "25:1");
  const colorSpace = (fields.C || "420jpeg").toLowerCase();
  if (!/^420(?:jpeg|mpeg2|paldv)?$/.test(colorSpace)) {
    throw new RangeError(`Unsupported Y4M colorspace: ${colorSpace}; 8-bit 4:2:0 is required`);
  }

  const chromaWidth = Math.ceil(width / 2);
  const chromaHeight = Math.ceil(height / 2);
  const lumaBytes = width * height;
  const chromaBytes = chromaWidth * chromaHeight;
  const payloadBytes = lumaBytes + chromaBytes * 2;
  const frames = [];
  let offset = headerLine.next;

  while (offset < bytes.length && (maximumFrames === 0 || frames.length < maximumFrames)) {
    const frameLine = nextLine(bytes, offset);
    if (frameLine.text !== "FRAME" && !frameLine.text.startsWith("FRAME ")) {
      throw new RangeError(`Invalid Y4M frame marker at byte ${offset}`);
    }
    offset = frameLine.next;
    if (offset + payloadBytes > bytes.length) {
      throw new RangeError(`Truncated Y4M frame ${frames.length}`);
    }
    const y = bytes.subarray(offset, offset + lumaBytes);
    const u = bytes.subarray(offset + lumaBytes, offset + lumaBytes + chromaBytes);
    const v = bytes.subarray(offset + lumaBytes + chromaBytes, offset + payloadBytes);
    frames.push(yuv420ToRgba(y, u, v, width, height, chromaWidth));
    offset += payloadBytes;
  }

  if (frames.length === 0) throw new RangeError("Y4M input contains no frames");
  if (maximumFrames === 0 && offset !== bytes.length) {
    throw new RangeError("Trailing bytes after the final Y4M frame");
  }
  return {
    width,
    height,
    fpsNumerator: fps.numerator,
    fpsDenominator: fps.denominator,
    colorSpace,
    frames,
  };
}

function yuv420ToRgba(yPlane, uPlane, vPlane, width, height, chromaWidth) {
  const rgba = new Uint8ClampedArray(width * height * 4);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const luma = yPlane[y * width + x];
      const chromaOffset = Math.floor(y / 2) * chromaWidth + Math.floor(x / 2);
      const u = uPlane[chromaOffset] - 128;
      const v = vPlane[chromaOffset] - 128;
      const scaledY = 1.164383 * (luma - 16);
      const output = (y * width + x) * 4;
      rgba[output] = clampByte(scaledY + 1.596027 * v);
      rgba[output + 1] = clampByte(scaledY - 0.391762 * u - 0.812968 * v);
      rgba[output + 2] = clampByte(scaledY + 2.017232 * u);
      rgba[output + 3] = 255;
    }
  }
  return rgba;
}

function rgbaToYuv420(rgbaInput, width, height) {
  const rgba = asBuffer(rgbaInput);
  if (rgba.length !== width * height * 4) {
    throw new RangeError("RGBA frame length does not match its dimensions");
  }
  const chromaWidth = Math.ceil(width / 2);
  const chromaHeight = Math.ceil(height / 2);
  const lumaBytes = width * height;
  const chromaBytes = chromaWidth * chromaHeight;
  const output = Buffer.allocUnsafe(lumaBytes + chromaBytes * 2);
  const uOffset = lumaBytes;
  const vOffset = lumaBytes + chromaBytes;

  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const source = (y * width + x) * 4;
      output[y * width + x] = rgbToY(
        rgba[source],
        rgba[source + 1],
        rgba[source + 2],
      );
    }
  }

  for (let cy = 0; cy < chromaHeight; cy += 1) {
    for (let cx = 0; cx < chromaWidth; cx += 1) {
      let red = 0;
      let green = 0;
      let blue = 0;
      let samples = 0;
      for (let dy = 0; dy < 2; dy += 1) {
        const y = cy * 2 + dy;
        if (y >= height) continue;
        for (let dx = 0; dx < 2; dx += 1) {
          const x = cx * 2 + dx;
          if (x >= width) continue;
          const source = (y * width + x) * 4;
          red += rgba[source];
          green += rgba[source + 1];
          blue += rgba[source + 2];
          samples += 1;
        }
      }
      red /= samples;
      green /= samples;
      blue /= samples;
      const chroma = cy * chromaWidth + cx;
      output[uOffset + chroma] = rgbToU(red, green, blue);
      output[vOffset + chroma] = rgbToV(red, green, blue);
    }
  }
  return output;
}

function y4mHeader(width, height, fpsNumerator, fpsDenominator) {
  return Buffer.from(
    `YUV4MPEG2 W${width} H${height} F${fpsNumerator}:${fpsDenominator} Ip A0:0 C420jpeg\n`,
    "ascii",
  );
}

function y4mFrame(rgba, width, height) {
  return Buffer.concat([
    Buffer.from("FRAME\n", "ascii"),
    rgbaToYuv420(rgba, width, height),
  ]);
}

function rgbToY(red, green, blue) {
  return clampByte(16 + 0.256788 * red + 0.504129 * green + 0.097906 * blue);
}

function rgbToU(red, green, blue) {
  return clampByte(128 - 0.148223 * red - 0.290993 * green + 0.439216 * blue);
}

function rgbToV(red, green, blue) {
  return clampByte(128 + 0.439216 * red - 0.367788 * green - 0.071427 * blue);
}

function clampByte(value) {
  return Math.max(0, Math.min(255, Math.round(value)));
}

module.exports = {
  parseY4m,
  rgbaToYuv420,
  y4mFrame,
  y4mHeader,
  yuv420ToRgba,
};
