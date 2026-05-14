#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import struct
import sys
import tempfile
import unittest
from pathlib import Path


HEADER = struct.Struct("<4sIIQQ")
INDEX_ENTRY = struct.Struct("<IiiIQQ")
CHUNK_HEADER = struct.Struct("<IiiIH")
COLUMN_ENTRY = struct.Struct("<HHQQ")
INSPECTOR = Path(sys.argv[1]) if len(sys.argv) > 1 else None


def append_string(data: bytearray, value: bytes) -> None:
    data.extend(struct.pack("<I", len(value)))
    data.extend(value)


def write_fixture(path: Path) -> None:
    data = bytearray()
    data.extend(b"\x00" * HEADER.size)

    reference_name = b"chrToy"
    data.extend(struct.pack("<H", len(reference_name)))
    data.extend(reference_name)
    data.extend(struct.pack("<I", 1000))

    data.extend(struct.pack("<B", 0))
    append_string(data, b"")
    append_string(data, b"")

    chunk_offset = len(data)
    records = 3
    columns = [
        (2, 2, 0, 1),  # FLAG bit-pack
        (3, 1, 1, 1),  # POS delta-varint
        (4, 3, 2, 1),  # MAPQ RLE
        (5, 5, 3, 1),  # CIGAR token
        (9, 4, 4, 1),  # SEQ 2-bit literal
        (10, 7, 5, 1),  # QUAL pack
        (10, 8, 6, 1),  # QUAL pack compressed
        (99, 77, 7, 1),  # unknown numeric fallback
    ]
    data.extend(CHUNK_HEADER.pack(0, 100, 160, records, len(columns)))
    for column in columns:
        data.extend(COLUMN_ENTRY.pack(*column))
    data.extend(b"abcdefgh")
    chunk_length = len(data) - chunk_offset

    index_offset = len(data)
    data.extend(INDEX_ENTRY.pack(0, 100, 160, records, chunk_offset, chunk_length))
    HEADER.pack_into(data, 0, b"AXF1", 2, 1, 1, index_offset)
    path.write_bytes(data)


class InspectAxf1MetadataTest(unittest.TestCase):
    def setUp(self) -> None:
        if INSPECTOR is None:
            raise RuntimeError("missing inspector path argument")
        self.inspector = INSPECTOR

    def run_inspector(self, path: Path, *args: str) -> str:
        return subprocess.check_output(
            [sys.executable, str(self.inspector), str(path), *args],
            text=True,
        )

    def test_columns_and_codec_summary_include_known_and_unknown_names(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "fixture.axf1"
            write_fixture(fixture)

            columns = self.run_inspector(fixture, "--columns")
            self.assertIn("chunk_index\tcolumn_id\tcolumn_name\tcodec_id\tcodec_name", columns)
            self.assertIn("0\t2\tflag\t2\tflag_bitpack\t0\t1", columns)
            self.assertIn("0\t3\tpos\t1\tpos_delta_varint\t1\t1", columns)
            self.assertIn("0\t4\tmapq\t3\tmapq_rle\t2\t1", columns)
            self.assertIn("0\t5\tcigar\t5\tcigar_token\t3\t1", columns)
            self.assertIn("0\t9\tsequence\t4\tseq_2bit_literal\t4\t1", columns)
            self.assertIn("0\t10\tquality\t7\tqual_pack\t5\t1", columns)
            self.assertIn("0\t10\tquality\t8\tqual_pack_compressed\t6\t1", columns)
            self.assertIn("0\t99\tunknown_99\t77\tunknown_77\t7\t1", columns)

            codecs = self.run_inspector(fixture, "--column-codecs")
            self.assertIn("column_id\tcolumn_name\tcodec_id\tcodec_name\tchunk_count", codecs)
            self.assertIn("2\tflag\t2\tflag_bitpack\t1", codecs)
            self.assertIn("3\tpos\t1\tpos_delta_varint\t1", codecs)
            self.assertIn("4\tmapq\t3\tmapq_rle\t1", codecs)
            self.assertIn("5\tcigar\t5\tcigar_token\t1", codecs)
            self.assertIn("9\tsequence\t4\tseq_2bit_literal\t1", codecs)
            self.assertIn("10\tquality\t7\tqual_pack\t1", codecs)
            self.assertIn("10\tquality\t8\tqual_pack_compressed\t1", codecs)
            self.assertIn("99\tunknown_99\t77\tunknown_77\t1", codecs)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
