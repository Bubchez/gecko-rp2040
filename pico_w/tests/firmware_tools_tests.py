# SPDX-License-Identifier: MIT
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "pico_w/tools"))


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / f"pico_w/tools/{name}.py")
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


build = module("build_firmware")
provision = module("provision_usb")


class Tools(unittest.TestCase):
    def test_uf2_blocks_family_address_and_exact_payload(self):
        for size in (1, 255, 256, 257, 4096, 100001):
            data = bytes(i % 256 for i in range(size))
            image = build.uf2(data)
            count = (size + 255) // 256
            self.assertEqual(len(image), count * 512)
            reconstructed = bytearray()
            for index in range(count):
                block = image[index * 512:(index + 1) * 512]
                self.assertEqual(struct.unpack("<8I", block[:32]),
                                 (0x0A324655, 0x9E5D5157, 0x2000, 0x10000000 + index * 256, 256, index, count, 0xE48BFF56))
                self.assertEqual(struct.unpack("<I", block[-4:])[0], 0x0AB16F30)
                reconstructed += block[32:288]
            self.assertEqual(reconstructed[:size], data)
            self.assertEqual(reconstructed[size:], bytes(len(reconstructed) - size))
        with self.assertRaises(ValueError):
            build.uf2(b"")
        with self.assertRaises(ValueError):
            build.uf2(bytes(0x1fe001))

    def test_usb_payload_is_bounded_and_validated_without_io(self):
        payload = provision.command("test ssid", "password", "a" * 32, "unit-1")
        self.assertEqual(payload, b"PROVISION 746573742073736964 70617373776f7264 " + b"a" * 32 + b" unit-1\n")
        self.assertLessEqual(len(provision.command("s" * 32, "p" * 63, "t" * 128, "d" * 64)), 513)
        for args in (("bad\nssid", "password", "a" * 32, "unit-1"), ("test", "short", "a" * 32, "unit-1"),
                     ("test", "password", "short", "unit-1"), ("test", "password", "a" * 32, "bad/id")):
            with self.assertRaises(ValueError):
                provision.command(*args)


if __name__ == "__main__":
    unittest.main(verbosity=2)
