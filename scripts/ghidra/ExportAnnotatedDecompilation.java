// Exports annotated Xtensa disassembly and Ghidra pseudo-C for one program.
// Run this script with Ghidra's analyzeHeadless as a postScript.

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;

public class ExportAnnotatedDecompilation extends GhidraScript {
    private final Map<String, String> exactDescriptions = new HashMap<>();

    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) {
            throw new IllegalArgumentException(
                "Expected output assembly and pseudo-C paths");
        }

        addExactDescriptions();
        File assemblyFile = new File(args[0]);
        File pseudoCFile = new File(args[1]);
        ensureParentDirectory(assemblyFile);
        ensureParentDirectory(pseudoCFile);

        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        decompiler.setOptions(options);
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(true);
        decompiler.setSimplificationStyle("decompile");
        if (!decompiler.openProgram(currentProgram)) {
            throw new IllegalStateException(
                "Decompiler could not open " + currentProgram.getName());
        }

        try (
            PrintWriter assembly = utf8Writer(assemblyFile);
            PrintWriter pseudoC = utf8Writer(pseudoCFile)
        ) {
            writeAssemblyHeader(assembly);
            writePseudoCHeader(pseudoC);

            FunctionIterator functions =
                currentProgram.getFunctionManager().getFunctions(true);
            for (Function function : functions) {
                monitor.checkCancelled();
                if (function.isExternal() || function.getBody().isEmpty()) {
                    continue;
                }

                String name = function.getName();
                String description = describeFunction(name);
                exportAssemblyFunction(assembly, function, description);
                exportPseudoCFunction(
                    pseudoC, decompiler, function, description);
            }
        }
        finally {
            decompiler.dispose();
        }

        println("Exported " + currentProgram.getName() + " to:");
        println("  " + assemblyFile);
        println("  " + pseudoCFile);
    }

    private void exportAssemblyFunction(
        PrintWriter output,
        Function function,
        String description
    ) {
        output.println();
        output.println(
            "# ====================================================================");
        output.println("# " + function.getName());
        output.println("# Purpose: " + description);
        output.println("# Entry: " + function.getEntryPoint());
        output.println(
            "# ====================================================================");
        output.println(function.getName() + ":");

        InstructionIterator instructions = currentProgram.getListing()
            .getInstructions(function.getBody(), true);
        for (Instruction instruction : instructions) {
            byte[] bytes;
            try {
                bytes = instruction.getBytes();
            }
            catch (Exception exception) {
                bytes = new byte[0];
            }
            output.printf(
                "  %-10s  %-24s  %s%n",
                instruction.getAddress(),
                bytesToHex(bytes),
                instruction);
        }
    }

    private void exportPseudoCFunction(
        PrintWriter output,
        DecompInterface decompiler,
        Function function,
        String description
    ) {
        output.println();
        output.println(
            "/* ==================================================================");
        output.println(" * " + function.getName());
        output.println(" * Purpose: " + description);
        output.println(" * Entry: " + function.getEntryPoint());
        output.println(
            " * ================================================================== */");

        DecompileResults result =
            decompiler.decompileFunction(function, 120, monitor);
        if (!result.decompileCompleted() ||
            result.getDecompiledFunction() == null) {
            output.println(
                "/* Ghidra decompiler failed: " +
                result.getErrorMessage() + " */");
            return;
        }
        writeNormalizedText(
            output, result.getDecompiledFunction().getC());
    }

    private String describeFunction(String name) {
        String exact = exactDescriptions.get(name);
        if (exact != null) {
            return exact;
        }

        if (name.startsWith("idct_block_")) {
            return describeIdct(name);
        }
        if (name.startsWith("jpeg_dec_proc_")) {
            return describeMcuProcessor(name);
        }
        if (name.startsWith("yuv") || name.startsWith("y_to_")) {
            return describeColorConversion(name);
        }
        if (name.startsWith("FUN_")) {
            return "Analyzer-created helper without a retained source symbol.";
        }
        return "Internal ESP_NEW_JPEG decoder helper; inspect callers and " +
            "data flow below.";
    }

    private String describeIdct(String name) {
        String scale;
        if (name.contains("_8_8")) {
            scale = "a full 8x8 output block";
        }
        else if (name.contains("_4_8")) {
            scale = "a horizontally reduced 4x8 output block";
        }
        else {
            scale = "a half-resolution 4x4 output block";
        }
        return "Xtensa integer inverse DCT producing " + scale +
            rotationDescription(name) +
            "; includes the all-zero-AC column shortcut and sample clipping.";
    }

    private String describeMcuProcessor(String name) {
        String sampling = samplingDescription(name);
        String variant;
        if (name.endsWith("_block")) {
            variant = "block-output path used by the streaming API";
        }
        else if (name.endsWith("_clipper")) {
            variant = "clipped-region path";
        }
        else if (name.endsWith("_unalign")) {
            variant = "edge path for dimensions not aligned to an MCU";
        }
        else if (name.endsWith("_variety")) {
            variant = "general path selecting scale, clip and output details";
        }
        else {
            variant = "ordinary image-output path";
        }
        return "Decodes JPEG MCUs for " + sampling +
            rotationDescription(name) + " using the " + variant +
            "; orchestrates entropy decode, dequantization, IDCT and color " +
            "packing.";
    }

    private String describeColorConversion(String name) {
        String sampling = samplingDescription(name);
        String output;
        if (name.contains("rgb565le") || name.contains("8align_le")) {
            output = "little-endian RGB565";
        }
        else if (name.contains("rgb565be") || name.contains("8align_be")) {
            output = "big-endian RGB565";
        }
        else if (name.contains("rgb888")) {
            output = "RGB888";
        }
        else {
            output = "packed UYVY";
        }
        return "Converts " + sampling + " samples to " + output +
            rotationDescription(name) +
            "; performs chroma reuse/upsampling, fixed-point YCbCr conversion " +
            "and output packing.";
    }

    private String samplingDescription(String name) {
        if (name.contains("gray") || name.startsWith("y_to_")) {
            return "grayscale Y";
        }
        if (name.contains("444")) {
            return "YUV444";
        }
        if (name.contains("422")) {
            return "YUV422";
        }
        return "YUV420";
    }

    private String rotationDescription(String name) {
        if (name.contains("_270")) {
            return " with 270-degree rotation";
        }
        if (name.contains("_180")) {
            return " with 180-degree rotation";
        }
        if (name.contains("_90")) {
            return " with 90-degree rotation";
        }
        return " without rotation";
    }

    private void addExactDescriptions() {
        exactDescriptions.put(
            "jpeg_dec_open",
            "Validates decoder configuration, allocates persistent state and " +
                "selects output, scale, clip and rotation dispatch tables.");
        exactDescriptions.put(
            "jpeg_dec_close",
            "Releases the decoder state and every persistent work allocation.");
        exactDescriptions.put(
            "jpeg_dec_parse_header",
            "Scans baseline-JPEG markers, parses tables and frame geometry, " +
                "then prepares MCU and output state for decoding.");
        exactDescriptions.put(
            "jpeg_dec_process",
            "Public decode entry that dispatches the prepared image to the " +
                "selected rotation, scale, clip and block-processing kernel.");
        exactDescriptions.put(
            "jpeg_dec_get_outbuf_len",
            "Calculates the required output-buffer size for current dimensions " +
                "and selected pixel format.");
        exactDescriptions.put(
            "jpeg_dec_get_process_count",
            "Returns the number of block callbacks/process iterations needed " +
                "for the prepared image.");
        exactDescriptions.put(
            "jpeg_dec_create_huffman_tbl",
            "Builds canonical JPEG Huffman decode metadata and fast lookup " +
                "tables from DHT code-length/value arrays.");
        exactDescriptions.put(
            "jpeg_dec_huffman",
            "Decodes one entropy-coded 8x8 coefficient block: obtains the DC " +
                "difference, expands AC run/size codes, handles EOB/ZRL and " +
                "updates restart/DC-predictor state.");
        exactDescriptions.put(
            "jpeg_dec_parse_soi",
            "Validates the JPEG Start Of Image marker and initializes marker " +
                "scan state.");
        exactDescriptions.put(
            "jpeg_dec_parse_dqt",
            "Parses an 8-bit baseline quantization table and stores it in the " +
                "decoder's coefficient order.");
        exactDescriptions.put(
            "jpeg_dec_parse_sof0",
            "Parses baseline SOF0 dimensions, component sampling factors and " +
                "quantization-table assignments.");
        exactDescriptions.put(
            "jpeg_dec_parse_sos",
            "Parses scan component selectors and DC/AC Huffman table " +
                "assignments, then positions input at entropy data.");
        exactDescriptions.put(
            "jpeg_dec_process_0",
            "Dispatches non-rotated decoding to the prepared grayscale, " +
                "YUV444, YUV422 or YUV420 MCU kernel.");
        exactDescriptions.put(
            "jpeg_dec_process_90",
            "Dispatches decoding to the prepared 90-degree rotation kernel.");
        exactDescriptions.put(
            "jpeg_dec_process_180",
            "Dispatches decoding to the prepared 180-degree rotation kernel.");
        exactDescriptions.put(
            "jpeg_dec_process_270",
            "Dispatches decoding to the prepared 270-degree rotation kernel.");
        exactDescriptions.put(
            "jpeg_dec_process_scale",
            "General scaled decode path handling reduced IDCT dimensions, " +
                "clipping, rotation and output-format dispatch.");
        exactDescriptions.put(
            "jpeg_calloc",
            "Allocates zero-initialized JPEG work memory using the component's " +
                "default capability policy.");
        exactDescriptions.put(
            "jpeg_calloc_align",
            "Allocates zero-initialized JPEG work memory with the requested " +
                "alignment using the component's default capability policy.");
        exactDescriptions.put(
            "jpeg_calloc_inner",
            "Allocates zero-initialized internal-memory JPEG work storage, " +
                "preferring capability combinations suitable for decoder " +
                "tables and state.");
        exactDescriptions.put(
            "jpeg_calloc_align_inner",
            "Allocates aligned, zero-initialized internal-memory JPEG work " +
                "storage with capability-aware heap selection.");
        exactDescriptions.put(
            "jpeg_free",
            "Releases an ordinary JPEG component allocation.");
        exactDescriptions.put(
            "jpeg_free_align",
            "Releases an aligned JPEG component allocation.");
        exactDescriptions.put(
            "esp_jpeg_get_version",
            "Returns the compile-time ESP_NEW_JPEG version string; it is not " +
                "part of the image decode path.");
        exactDescriptions.put(
            "jpeg_dec_proc_yuv420_0_block",
            "Hot Player path: decodes six blocks per 16x16 YUV420 MCU, runs " +
                "integer IDCT, converts to RGB565LE and submits block rows " +
                "through the streaming output callback.");
        exactDescriptions.put(
            "y_to_rgb888",
            "Expands an 8x8 grayscale luma block to RGB888 by copying each " +
                "clipped Y sample into the red, green and blue channels.");
        exactDescriptions.put(
            "y_to_rgb565le",
            "Converts an 8x8 grayscale luma block to little-endian RGB565 by " +
                "clipping Y and replicating its high bits into R, G and B.");
        exactDescriptions.put(
            "y_to_rgb565be",
            "Converts an 8x8 grayscale luma block to big-endian RGB565 by " +
                "clipping Y and replicating its high bits into R, G and B.");
        exactDescriptions.put(
            "y_to_uyvy",
            "Packs an 8x8 grayscale luma block as UYVY, inserting neutral " +
                "chroma values around pairs of clipped Y samples.");
        exactDescriptions.put(
            "yuv420_to_rgb565le",
            "Hot Player color path: reuses each chroma sample for a 2x2 luma " +
                "group, converts fixed-point YCbCr to RGB and packs two-byte " +
                "little-endian RGB565 pixels.");
    }

    private void writeAssemblyHeader(PrintWriter output) {
        output.println(
            "# ESP_NEW_JPEG 1.0.2 ESP32 decoder annotated disassembly");
        output.println(
            "# Derived from the Espressif binary; see LICENSE.ESPRESSIF in " +
            "the analysis directory.");
        output.println("# Program: " + currentProgram.getName());
        output.println("# Language: " +
            currentProgram.getLanguageID().getIdAsString());
        output.println(
            "# Generated by Ghidra; addresses are relocatable-object section " +
            "addresses.");
        output.println(
            "# This is analysis output, not directly reassemblable source.");
    }

    private void writePseudoCHeader(PrintWriter output) {
        output.println(
            "/* ESP_NEW_JPEG 1.0.2 ESP32 decoder Ghidra pseudo-C.");
        output.println(
            " * Derived from the Espressif binary; see LICENSE.ESPRESSIF in " +
            "the analysis directory.");
        output.println(" * Program: " + currentProgram.getName());
        output.println(" * Language: " +
            currentProgram.getLanguageID().getIdAsString());
        output.println(
            " * Types and names are reconstructed and must not be treated as " +
            "the original source.");
        output.println(" */");
    }

    private PrintWriter utf8Writer(File file) throws Exception {
        return new PrintWriter(new OutputStreamWriter(
            new FileOutputStream(file), StandardCharsets.UTF_8));
    }

    private void ensureParentDirectory(File file) {
        File parent = file.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IllegalStateException(
                "Could not create output directory " + parent);
        }
    }

    private String bytesToHex(byte[] bytes) {
        StringBuilder result = new StringBuilder(bytes.length * 3);
        for (byte value : bytes) {
            if (result.length() != 0) {
                result.append(' ');
            }
            result.append(String.format("%02x", value & 0xff));
        }
        return result.toString();
    }

    private void writeNormalizedText(PrintWriter output, String text) {
        String[] lines = text.split("\\R", -1);
        int lastContentLine = lines.length - 1;
        while (lastContentLine >= 0 &&
            lines[lastContentLine].stripTrailing().isEmpty()) {
            lastContentLine--;
        }
        for (int index = 0; index <= lastContentLine; index++) {
            output.println(lines[index].stripTrailing());
        }
    }
}
