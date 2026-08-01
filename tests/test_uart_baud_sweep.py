import importlib.util
import pathlib
import sys
import unittest


SCRIPT = (
    pathlib.Path(__file__).parents[1]
    / "scripts"
    / "sweep_esp32_uart_baud.py"
)
SPEC = importlib.util.spec_from_file_location("sweep_esp32_uart_baud", SCRIPT)
sweep = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = sweep
SPEC.loader.exec_module(sweep)


class UartBaudSweepTest(unittest.TestCase):
    def test_wait_line_discards_binary_transition_prefix(self):
        class Port:
            def __init__(self):
                self.lines = iter((b"\x00\x80SWEEP TXDONE 1 2000000 1\n",))

            def readline(self):
                return next(self.lines, b"")

        self.assertEqual(
            sweep.wait_line(Port(), "SWEEP TXDONE 1 ", 0.1),
            "SWEEP TXDONE 1 2000000 1",
        )

    def test_binary_frame_round_trip(self):
        frame = sweep.make_frame(17, 256, 2_929_062)
        self.assertTrue(sweep.valid_frame(frame, 17, 256, 2_929_062))
        damaged = bytearray(frame)
        damaged[-1] ^= 1
        self.assertFalse(
            sweep.valid_frame(bytes(damaged), 17, 256, 2_929_062)
        )

    def test_payload_depends_on_candidate_and_sequence(self):
        first = sweep.make_payload(64, 0, 2_000_000)
        self.assertNotEqual(first, sweep.make_payload(64, 1, 2_000_000))
        self.assertNotEqual(first, sweep.make_payload(64, 0, 2_003_130))

    def test_host_report_is_crc_protected(self):
        report = sweep.make_host_report(2_000_000, 2_003_130, 7, 1)
        fields = sweep.HOST_REPORT.unpack(report)
        self.assertEqual(fields[1:-1], (2_000_000, 2_003_130, 7, 1))
        self.assertEqual(fields[-1], sweep.zlib.crc32(report[:-4]) & 0xFFFFFFFF)

    def test_control_result_layout(self):
        prefix = sweep.RESULT.pack(
            b"SWPR", 3_000_000, 2_929_062, 8, 0, 7, 1, 0
        )[:-4]
        frame = prefix + sweep.struct.pack(
            "<I", sweep.zlib.crc32(prefix) & 0xFFFFFFFF
        )
        fields = sweep.RESULT.unpack(frame)
        self.assertEqual(fields[1:-1], (3_000_000, 2_929_062, 8, 0, 7, 1))

    def test_rx_go_marker_is_crc_protected(self):
        prefix = sweep.RX_READY.pack(
            b"SWPG", 2_000_000, 2_003_130, 8, 256, 0
        )[:-4]
        frame = prefix + sweep.struct.pack(
            "<I", sweep.zlib.crc32(prefix) & 0xFFFFFFFF
        )

        class Port:
            def __init__(self, data):
                self.data = bytearray(data)

            def read(self, size):
                chunk = self.data[:size]
                del self.data[:size]
                return bytes(chunk)

        sweep.wait_rx_ready(
            Port(frame), 2_000_000, 2_003_130, 8, 256, 0.1
        )

    def test_result_and_best_parsing(self):
        best = sweep.parse_best("SWEEP BEST 1 3000000 2929062 1 0")
        self.assertFalse(best.clean)
        self.assertEqual(best.calibrated_baud, 2_929_062)
        joined = sweep.parse_best(
            "SWEEP BEST 1 3000000 2929062 1 0P COMPLETE 1"
        )
        self.assertEqual(joined.calibrated_baud, 2_929_062)

    def test_rate_mask(self):
        self.assertEqual(sweep.rate_mask(None), 3)
        self.assertEqual(sweep.rate_mask([2_000_000]), 1)
        self.assertEqual(sweep.rate_mask([3_000_000]), 2)
        self.assertEqual(sweep.rate_mask([3_000_000, 2_000_000]), 3)


if __name__ == "__main__":
    unittest.main()
