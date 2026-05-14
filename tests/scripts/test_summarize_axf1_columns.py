#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(sys.argv[1]) if len(sys.argv) > 1 else None

THIS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(THIS_DIR))

from test_inspect_axf1_metadata import write_fixture  # noqa: E402


class SummarizeAxf1ColumnsTest(unittest.TestCase):
    def setUp(self) -> None:
        if SCRIPT is None:
            raise RuntimeError("missing summary script path argument")
        self.script = SCRIPT

    def run_summary(self, path: Path) -> str:
        return subprocess.check_output(
            [sys.executable, str(self.script), str(path)],
            text=True,
        )

    def test_summarizes_payload_bytes_and_codec_distribution(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            fixture = Path(tmp) / "fixture.axf1"
            write_fixture(fixture)

            summary = self.run_summary(fixture)

            self.assertIn(
                "column_id\tcolumn_name\ttotal_payload_bytes\tchunk_count\t"
                "codec_distribution\tavg_payload_bytes_per_chunk\tpercent_payload",
                summary,
            )
            self.assertIn("2\tflag\t1\t1\t2:flag_bitpack:1\t1.000\t14.286", summary)
            self.assertIn("5\tcigar\t1\t1\t5:cigar_token:1\t1.000\t14.286", summary)
            self.assertIn("9\tsequence\t1\t1\t4:seq_2bit_literal:1\t1.000\t14.286", summary)
            self.assertIn("10\tquality\t1\t1\t6:qual_rle:1\t1.000\t14.286", summary)
            self.assertIn("99\tunknown_99\t1\t1\t77:unknown_77:1\t1.000\t14.286", summary)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
