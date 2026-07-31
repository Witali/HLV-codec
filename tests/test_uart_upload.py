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
    / "uart_upload.py"
)
sys.path.insert(0, str(SCRIPT.parent))
SPEC = importlib.util.spec_from_file_location("uart_upload", SCRIPT)
uart_upload = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(uart_upload)


class FakeEsp32Port:
    def __init__(self, nak_once_sequence=None, completion_suffix=b"\n"):
        self.responses = []
        self.data = bytearray()
        self.sequence = 0
        self.expected_size = 0
        self.expected_crc = 0
        self.remote_name = ""
        self.baudrate = uart_upload.CONTROL_BAUD
        self.baud_changes = []
        self.nak_once_sequence = nak_once_sequence
        self.completion_suffix = completion_suffix
        self.nak_sent = False
        self.recovering = False
        self.unread_acks = 0
        self.maximum_unread_acks = 0
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
        if packet.startswith(b"\nHLVPUT "):
            fields = packet.decode("ascii").strip().split()
            self.remote_name = fields[2]
            self.expected_size = int(fields[3])
            self.expected_crc = int(fields[4], 16)
            data_baud = int(fields[5])
            self.responses.append(
                (
                    f"HLVREADY {uart_upload.UPLOAD_PROTOCOL_VERSION} "
                    f"1024 {data_baud} 2\n"
                ).encode("ascii")
            )
            return len(packet)

        magic, sequence, count, checksum = struct.unpack(
            "<4sIHI", packet[: uart_upload.BLOCK_HEADER.size]
        )
        payload = packet[uart_upload.BLOCK_HEADER.size :]
        self.assert_equal(magic, uart_upload.BLOCK_MAGIC)
        self.assert_equal(count, len(payload))
        self.assert_equal(checksum, zlib.crc32(payload) & 0xFFFFFFFF)
        if self.recovering and sequence != self.sequence:
            return len(packet)
        if (
            sequence == self.nak_once_sequence
            and not self.nak_sent
        ):
            self.nak_sent = True
            self.recovering = True
            self.responses.append(
                f"HLVNAK {sequence} CRC\n".encode("ascii")
            )
            return len(packet)
        self.assert_equal(sequence, self.sequence)
        self.recovering = False
        self.data.extend(payload)
        self.responses.append(
            f"HLVACK {sequence} {len(self.data)}\n".encode("ascii")
        )
        self.unread_acks += 1
        self.maximum_unread_acks = max(
            self.maximum_unread_acks, self.unread_acks
        )
        self.sequence += 1
        if len(self.data) == self.expected_size:
            self.assert_equal(
                zlib.crc32(self.data) & 0xFFFFFFFF, self.expected_crc
            )
            self.responses.append(
                (
                    f"HLVDONE {uart_upload.UPLOAD_PROTOCOL_VERSION} "
                    f"{len(self.data)} {self.expected_crc:08x} "
                    f"{self.remote_name}"
                ).encode("ascii") + self.completion_suffix
            )
        return len(packet)

    def readline(self):
        if not self.responses:
            return b""
        response = self.responses.pop(0)
        if response.startswith(b"HLVACK "):
            self.unread_acks -= 1
        return response

    def flush(self):
        pass

    def reset_input_buffer(self):
        pass

    def change_baud(self, port, baud, _timeout):
        self.assert_equal(port, self)
        self.baudrate = baud
        self.baud_changes.append(baud)

    @staticmethod
    def assert_equal(actual, expected):
        if actual != expected:
            raise AssertionError(f"{actual!r} != {expected!r}")


class UartUploadTest(unittest.TestCase):
    def test_completion_timeout_includes_sd_readback_budget(self):
        self.assertGreater(
            uart_upload.completion_timeout(156_087_240, 15.0),
            600.0,
        )

    def test_block_layout_and_crc(self):
        packet = uart_upload.make_block(7, b"123456789")
        self.assertEqual(packet[:4], b"HLVB")
        self.assertEqual(packet[4:10], struct.pack("<IH", 7, 9))
        self.assertEqual(
            struct.unpack("<I", packet[10:14])[0], 0xCBF43926
        )

    def test_complete_transfer(self):
        content = b"HLV1" + bytes(range(256)) * 300
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "sample.hlv"
            source.write_bytes(content)
            fake = FakeEsp32Port()
            original_open_port = uart_upload.open_port
            original_change_baud = uart_upload.change_baud
            original_sleep = uart_upload.time.sleep
            original_progress = uart_upload.print_progress
            try:
                uart_upload.open_port = lambda *_: fake
                uart_upload.change_baud = fake.change_baud
                uart_upload.time.sleep = lambda *_: None
                uart_upload.print_progress = lambda *_args, **_kwargs: None
                uart_upload.upload(
                    source, "FAKE", "video.hlv", 921600, 1.0
                )
            finally:
                uart_upload.open_port = original_open_port
                uart_upload.change_baud = original_change_baud
                uart_upload.time.sleep = original_sleep
                uart_upload.print_progress = original_progress
            self.assertEqual(bytes(fake.data), content)
            self.assertEqual(fake.sequence, (len(content) + 1023) // 1024)
            self.assertEqual(fake.baudrate, uart_upload.CONTROL_BAUD)
            self.assertEqual(fake.baud_changes, [921600, 1000000])
            self.assertEqual(fake.maximum_unread_acks, 2)
            self.assertEqual(fake.monitoring_calls, 1)

    def test_go_back_n_after_rejected_block(self):
        content = b"HLV1" + bytes(range(256)) * 300
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "sample.hlv"
            source.write_bytes(content)
            fake = FakeEsp32Port(nak_once_sequence=0)
            original_open_port = uart_upload.open_port
            original_change_baud = uart_upload.change_baud
            original_sleep = uart_upload.time.sleep
            original_progress = uart_upload.print_progress
            try:
                uart_upload.open_port = lambda *_: fake
                uart_upload.change_baud = fake.change_baud
                uart_upload.time.sleep = lambda *_: None
                uart_upload.print_progress = lambda *_args, **_kwargs: None
                uart_upload.upload(
                    source, "FAKE", "video.hlv", 921600, 1.0
                )
            finally:
                uart_upload.open_port = original_open_port
                uart_upload.change_baud = original_change_baud
                uart_upload.time.sleep = original_sleep
                uart_upload.print_progress = original_progress
            self.assertTrue(fake.nak_sent)
            self.assertEqual(bytes(fake.data), content)
            self.assertEqual(fake.sequence, (len(content) + 1023) // 1024)
            self.assertEqual(fake.monitoring_calls, 1)

    def test_completion_ignores_trailing_baud_transition_noise(self):
        content = b"HLV1" + bytes(range(64))
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "sample.hlv"
            source.write_bytes(content)
            fake = FakeEsp32Port(completion_suffix=b"\xffx\x00\n")
            original_open_port = uart_upload.open_port
            original_change_baud = uart_upload.change_baud
            original_sleep = uart_upload.time.sleep
            original_progress = uart_upload.print_progress
            try:
                uart_upload.open_port = lambda *_: fake
                uart_upload.change_baud = fake.change_baud
                uart_upload.time.sleep = lambda *_: None
                uart_upload.print_progress = lambda *_args, **_kwargs: None
                uart_upload.upload(
                    source, "FAKE", "video.hlv", 921600, 1.0
                )
            finally:
                uart_upload.open_port = original_open_port
                uart_upload.change_baud = original_change_baud
                uart_upload.time.sleep = original_sleep
                uart_upload.print_progress = original_progress
            self.assertEqual(bytes(fake.data), content)
            self.assertEqual(fake.monitoring_calls, 1)


if __name__ == "__main__":
    unittest.main()
