"""Regression checks for the release gate, including safe NDK RELRO padding."""
import struct
import unittest
from verify_android_pages import audit_elf


def elf(headers):
    data = bytearray(0x10000)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, 183)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HH', data, 54, 56, len(headers))
    for i, row in enumerate(headers):
        struct.pack_into('<IIQQQQQQ', data, 64 + i * 56, *row)
    return bytes(data)


class PageGateTests(unittest.TestCase):
    def test_reject_four_k_loads(self):
        self.assertTrue(audit_elf('old', elf([
            (1, 6, 0, 0, 0, 0x3000, 0x3000, 4096)]))['errors'])

    def test_aligned_library(self):
        self.assertFalse(audit_elf('new', elf([
            (1, 6, 0, 0, 0, 0x8000, 0x8000, 16384),
            (0x6474e552, 4, 0, 0, 0, 0x4000, 0x4000, 1)]))['errors'])

    def test_reject_relro_protecting_writable_tail(self):
        errors = audit_elf('old-qt', elf([
            (1, 6, 0, 0, 0, 0x8000, 0x8000, 65536),
            (0x6474e552, 4, 0, 0, 0, 0x3000, 0x3000, 1)]))['errors']
        self.assertIn('RELRO rounds over writable data after its end', errors)

    def test_allow_separate_writable_load_after_padding(self):
        self.assertFalse(audit_elf('ndk-runtime', elf([
            (1, 6, 0, 0, 0, 0x3000, 0x3000, 16384),
            (1, 6, 0x4000, 0x4000, 0, 0x1000, 0x1000, 16384),
            (0x6474e552, 4, 0, 0, 0, 0x3000, 0x3000, 1)]))['errors'])

    def test_reject_incongruent_load(self):
        self.assertTrue(audit_elf('bad-offset', elf([
            (1, 4, 0, 4096, 0, 0x1000, 0x1000, 16384)]))['errors'])

    def test_reject_truncated_headers(self):
        with self.assertRaises(ValueError):
            audit_elf('truncated', elf([(1, 4, 0, 0, 0, 0, 0, 16384)])[:70])


if __name__ == '__main__':
    unittest.main()
