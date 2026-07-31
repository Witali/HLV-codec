import importlib.util
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib


SCRIPT = (
    pathlib.Path(__file__).parents[1]
    / "firmware"
    / "esp32_2432s028_hlv_player_idf_c"
    / "uart_patch.py"
)
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("uart_patch", SCRIPT)
uart_patch = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(uart_patch)


class FakePatchPort:
    def __init__(self, nak_once_sequence=None):
        self.responses = []
        self.data = bytearray()
        self.sequence = 0
        self.remote_name = ""
        self.remote_offset = 0
        self.expected_size = 0
        self.expected_crc = 0
        self.baudrate = uart_patch.CONTROL_BAUD
        self.nak_once_sequence = nak_once_sequence
        self.nak_sent = False
        self.monitoring_calls = 0

    def __enter__(self):
        return self

    def __exit__(self, *_):
        return False

    def write(self, packet):
        if packet.startswith(b"\nHLVSESSION "):
            command = packet.decode("ascii").strip().split()[2]
            self.responses.append(
                f"HLVSESSIONREADY 1 {command}\n".encode("ascii")
            )
            return len(packet)
        if packet == b"\nHLVMONITOR 1 ON\n":
            self.monitoring_calls += 1
            self.responses.append(b"HLVMONITORREADY 1 ON\n")
            return len(packet)
        if packet.startswith(b"\nHLVPATCH "):
            fields = packet.decode("ascii").strip().split()
            self.remote_name = fields[2]
            self.remote_offset = int(fields[3])
            self.expected_size = int(fields[4])
            self.expected_crc = int(fields[5], 16)
            baud = int(fields[6])
            self.responses.append(
                f"HLVPATCHREADY 1 1024 {baud}\n".encode("ascii")
            )
            return len(packet)

        magic, sequence, count, checksum = struct.unpack(
            "<4sIHI", packet[: uart_patch.PATCH_HEADER.size]
        )
        payload = packet[uart_patch.PATCH_HEADER.size :]
        self.assert_equal(magic, uart_patch.PATCH_MAGIC)
        self.assert_equal(count, len(payload))
        self.assert_equal(checksum, zlib.crc32(payload) & 0xFFFFFFFF)
        if sequence == self.nak_once_sequence and not self.nak_sent:
            self.nak_sent = True
            self.responses.append(
                f"HLVPATCHNAK {sequence} CRC\n".encode("ascii")
            )
            return len(packet)
        self.assert_equal(sequence, self.sequence)
        self.data.extend(payload)
        self.responses.append(
            f"HLVPATCHACK {sequence} {len(self.data)}\n".encode("ascii")
        )
        self.sequence += 1
        if len(self.data) == self.expected_size:
            self.assert_equal(
                zlib.crc32(self.data) & 0xFFFFFFFF, self.expected_crc
            )
            self.responses.append(
                (
                    f"HLVPATCHDONE 1 {self.remote_offset} "
                    f"{self.expected_size} {self.expected_crc:08x} "
                    f"{self.remote_name}\n"
                ).encode("ascii")
            )
        return len(packet)

    def readline(self):
        return self.responses.pop(0) if self.responses else b""

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    @staticmethod
    def assert_equal(actual, expected):
        if actual != expected:
            raise AssertionError(f"{actual!r} != {expected!r}")


class UartPatchTest(unittest.TestCase):
    def test_packet_layout_and_crc(self):
        packet = uart_patch.make_packet(7, b"123456789")
        self.assertEqual(packet[:10], b"HLVP" + struct.pack("<IH", 7, 9))
        self.assertEqual(struct.unpack("<I", packet[10:14])[0], 0xCBF43926)

    def run_transfer(self, nak_once_sequence=None):
        content = bytes(range(256)) * 16
        source_offset = 123
        length = 2700
        fake = FakePatchPort(nak_once_sequence)
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "sample.avi"
            source.write_bytes(content)
            original_open_port = uart_patch.open_port
            original_progress = uart_patch.print_progress
            try:
                uart_patch.open_port = lambda *_: fake
                uart_patch.print_progress = lambda *_args, **_kwargs: None
                checksum = uart_patch.patch(
                    source, "FAKE", "video.avi", 4096,
                    source_offset, length, uart_patch.CONTROL_BAUD, 1.0
                )
            finally:
                uart_patch.open_port = original_open_port
                uart_patch.print_progress = original_progress
        self.assertEqual(bytes(fake.data), content[source_offset:source_offset + length])
        self.assertEqual(checksum, zlib.crc32(fake.data) & 0xFFFFFFFF)
        self.assertEqual(fake.remote_offset, 4096)
        self.assertEqual(fake.monitoring_calls, 1)
        return fake

    def test_selected_range_transfer(self):
        self.run_transfer()

    def test_crc_rejection_retransmits_block(self):
        fake = self.run_transfer(nak_once_sequence=2)
        self.assertTrue(fake.nak_sent)


if __name__ == "__main__":
    unittest.main()
