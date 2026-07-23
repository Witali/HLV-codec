import importlib.util
import pathlib
import struct
import tempfile
import unittest
import zlib


SCRIPT = (
    pathlib.Path(__file__).parents[1]
    / "firmware"
    / "esp32_2432s028_hlv_player_idf"
    / "uart_upload.py"
)
SPEC = importlib.util.spec_from_file_location("uart_upload", SCRIPT)
uart_upload = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(uart_upload)


class FakeEsp32Port:
    def __init__(self):
        self.responses = []
        self.data = bytearray()
        self.sequence = 0
        self.expected_size = 0
        self.expected_crc = 0
        self.remote_name = ""
        self.baudrate = uart_upload.CONTROL_BAUD

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def write(self, packet):
        if packet.startswith(b"\nHLVPUT "):
            fields = packet.decode("ascii").strip().split()
            self.remote_name = fields[2]
            self.expected_size = int(fields[3])
            self.expected_crc = int(fields[4], 16)
            data_baud = int(fields[5])
            self.responses.append(
                f"HLVREADY 1 4096 {data_baud}\n".encode("ascii")
            )
            return len(packet)

        magic, sequence, count, checksum = struct.unpack(
            "<4sIHI", packet[: uart_upload.BLOCK_HEADER.size]
        )
        payload = packet[uart_upload.BLOCK_HEADER.size :]
        self.assert_equal(magic, uart_upload.BLOCK_MAGIC)
        self.assert_equal(sequence, self.sequence)
        self.assert_equal(count, len(payload))
        self.assert_equal(checksum, zlib.crc32(payload) & 0xFFFFFFFF)
        self.data.extend(payload)
        self.responses.append(
            f"HLVACK {sequence} {len(self.data)}\n".encode("ascii")
        )
        self.sequence += 1
        if len(self.data) == self.expected_size:
            self.assert_equal(
                zlib.crc32(self.data) & 0xFFFFFFFF, self.expected_crc
            )
            self.responses.append(
                (
                    f"HLVDONE 1 {len(self.data)} {self.expected_crc:08x} "
                    f"{self.remote_name}\n"
                ).encode("ascii")
            )
        return len(packet)

    def readline(self):
        return self.responses.pop(0) if self.responses else b""

    def flush(self):
        pass

    @staticmethod
    def assert_equal(actual, expected):
        if actual != expected:
            raise AssertionError(f"{actual!r} != {expected!r}")


class UartUploadTest(unittest.TestCase):
    def test_block_layout_and_crc(self):
        packet = uart_upload.make_block(7, b"123456789")
        self.assertEqual(packet[:4], b"HLVB")
        self.assertEqual(packet[4:10], struct.pack("<IH", 7, 9))
        self.assertEqual(
            struct.unpack("<I", packet[10:14])[0], 0xCBF43926
        )

    def test_complete_transfer(self):
        content = b"HLV1" + bytes(range(256)) * 40
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "sample.hlv"
            source.write_bytes(content)
            fake = FakeEsp32Port()
            original_open_port = uart_upload.open_port
            original_sleep = uart_upload.time.sleep
            original_progress = uart_upload.print_progress
            try:
                uart_upload.open_port = lambda *_: fake
                uart_upload.time.sleep = lambda *_: None
                uart_upload.print_progress = lambda *_args, **_kwargs: None
                uart_upload.upload(
                    source, "FAKE", "video.hlv", 921600, 1.0
                )
            finally:
                uart_upload.open_port = original_open_port
                uart_upload.time.sleep = original_sleep
                uart_upload.print_progress = original_progress
            self.assertEqual(bytes(fake.data), content)
            self.assertEqual(fake.baudrate, 921600)


if __name__ == "__main__":
    unittest.main()
