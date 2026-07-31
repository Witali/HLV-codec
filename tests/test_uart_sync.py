import importlib.util
import pathlib
import sys
import tempfile
import unittest
import zlib


SCRIPT = (
    pathlib.Path(__file__).parents[1]
    / "firmware"
    / "esp32_2432s028_hlv_player_idf_c"
    / "uart_sync.py"
)
SPEC = importlib.util.spec_from_file_location("uart_sync", SCRIPT)
uart_sync = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.path.insert(0, str(SCRIPT.parent))
sys.modules[SPEC.name] = uart_sync
SPEC.loader.exec_module(uart_sync)


class UartSyncTest(unittest.TestCase):
    def test_calculate_blocks_and_full_crc(self):
        content = bytes(range(256)) * 40
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.avi"
            path.write_bytes(content)
            scan = uart_sync.calculate_blocks(path, 4096)
        self.assertEqual(scan.size, len(content))
        self.assertEqual(scan.crc32, zlib.crc32(content) & 0xFFFFFFFF)
        self.assertEqual([block.size for block in scan.blocks], [4096, 4096, 2048])
        for block in scan.blocks:
            selected = content[block.offset:block.offset + block.size]
            self.assertEqual(block.crc32, zlib.crc32(selected) & 0xFFFFFFFF)

    def test_adjacent_mismatches_are_combined(self):
        local = uart_sync.BlockScan(
            16_384, 1,
            tuple(uart_sync.BlockRecord(i * 4096, 4096, i)
                  for i in range(4)),
        )
        remote = uart_sync.BlockScan(
            16_384, 2,
            (
                uart_sync.BlockRecord(0, 4096, 99),
                uart_sync.BlockRecord(4096, 4096, 98),
                uart_sync.BlockRecord(8192, 4096, 2),
                uart_sync.BlockRecord(12_288, 4096, 97),
            ),
        )
        self.assertEqual(
            uart_sync.differing_ranges(local, remote),
            [(0, 8192), (12_288, 4096)],
        )

    def test_synchronize_patches_ranges_then_verifies_whole_file(self):
        content = bytes(range(256)) * 64
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "sample.avi"
            path.write_bytes(content)
            local = uart_sync.calculate_blocks(path, 4096)
            corrupt_blocks = list(local.blocks)
            corrupt_blocks[1] = uart_sync.BlockRecord(4096, 4096, 0)
            corrupt_blocks[3] = uart_sync.BlockRecord(12_288, 4096, 0)
            corrupt = uart_sync.BlockScan(
                local.size, 0, tuple(corrupt_blocks)
            )
            scans = iter((corrupt, local))
            calls = []
            original_scan = uart_sync.scan_remote
            original_patch = uart_sync.patch
            try:
                uart_sync.scan_remote = lambda *_: next(scans)
                uart_sync.patch = lambda *args: calls.append(args)
                verified = uart_sync.synchronize(
                    path, "FAKE", "sample.avi", 4096, 1_000_000, 1.0
                )
            finally:
                uart_sync.scan_remote = original_scan
                uart_sync.patch = original_patch
        self.assertEqual(verified.crc32, local.crc32)
        self.assertEqual(
            [(call[3], call[5]) for call in calls],
            [(4096, 4096), (12_288, 4096)],
        )


if __name__ == "__main__":
    unittest.main()
