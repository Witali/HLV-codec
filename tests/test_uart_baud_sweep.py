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


def result(**overrides):
    values = {
        "nominal_baud": 3_000_000,
        "calibrated_baud": 2_935_780,
        "repetition": 1,
        "status": "PASS",
        "crc_rejections": 0,
        "accepted_blocks": 64,
        "received_bytes": 4096,
        "crc32": "12345678",
        "elapsed_seconds": 1.0,
        "detail": "",
    }
    values.update(overrides)
    return sweep.ProbeResult(**values)


class UartBaudSweepTest(unittest.TestCase):
    def test_default_candidates_are_unique_hardware_steps(self):
        two_megabit = sweep.default_candidates(2_000_000)
        three_megabit = sweep.default_candidates(3_000_000)
        self.assertEqual(len(two_megabit), len(set(two_megabit)))
        self.assertEqual(len(three_megabit), len(set(three_megabit)))
        self.assertIn(2_000_000, two_megabit)
        self.assertIn(3_000_000, three_megabit)
        self.assertGreater(max(two_megabit), 2_000_000)
        self.assertLess(min(three_megabit), 3_000_000)

    def test_candidate_requires_complete_zero_retry_transfer(self):
        self.assertTrue(sweep.candidate_is_clean([result()], 4096))
        self.assertFalse(
            sweep.candidate_is_clean([result(crc_rejections=1)], 4096)
        )
        self.assertFalse(
            sweep.candidate_is_clean([result(received_bytes=4032)], 4096)
        )
        self.assertFalse(
            sweep.candidate_is_clean([result(status="FAIL")], 4096)
        )

    def test_candidate_parser_rejects_unsupported_nominal_rate(self):
        self.assertEqual(
            sweep.parse_candidate("2000000=2015748"),
            (2_000_000, 2_015_748),
        )
        with self.assertRaises(Exception):
            sweep.parse_candidate("2500000=2500000")


if __name__ == "__main__":
    unittest.main()
