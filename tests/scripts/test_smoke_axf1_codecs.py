#!/usr/bin/env python3
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


SCRIPT = Path(sys.argv[1]) if len(sys.argv) > 1 else None


def write_executable(path: Path, content: str) -> None:
    path.write_text(textwrap.dedent(content), encoding="utf-8", newline="\n")
    path.chmod(0o755)


def shell_path(path: Path) -> str:
    resolved = path.resolve()
    if os.name != "nt":
        return str(resolved)

    drive = resolved.drive
    if len(drive) != 2 or drive[1] != ":":
        return str(resolved)
    return f"/mnt/{drive[0].lower()}{resolved.as_posix()[2:]}"


class SmokeAxf1CodecsScriptTest(unittest.TestCase):
    def setUp(self) -> None:
        if SCRIPT is None:
            raise RuntimeError("missing smoke script path argument")
        self.script = SCRIPT

    def run_smoke(self, tmp: Path, *extra_args: str) -> subprocess.CompletedProcess[str]:
        input_bam = tmp / "input.bam"
        input_bam.write_bytes(b"fake bam")
        work_dir = tmp / "work"
        alignx = tmp / "alignx"
        samtools = tmp / "samtools"
        inspector = tmp / "inspect_axf1_metadata.py"
        log = tmp / "alignx.log"

        write_executable(
            alignx,
            f"""\
            #!/usr/bin/env bash
            set -euo pipefail
            echo "$@" >> "{shell_path(log)}"
            if [[ "$1" == "convert" ]]; then
              output=""
              while [[ $# -gt 0 ]]; do
                if [[ "$1" == "-o" ]]; then
                  output="$2"
                  shift 2
                else
                  shift
                fi
              done
              : > "$output"
              exit 0
            fi
            if [[ "$1" == "view" ]]; then
              printf 'read001\\t0\\tchrToy\\t1\\t60\\t10M\\t*\\t0\\t0\\tACGT\\tFFFF\\n'
              exit 0
            fi
            exit 2
            """,
        )
        write_executable(
            samtools,
            """\
            #!/usr/bin/env bash
            set -euo pipefail
            printf 'read001\t0\tchrToy\t1\t60\t10M\t*\t0\t0\tACGT\tFFFF\n'
            """,
        )
        write_executable(
            inspector,
            """\
            #!/usr/bin/env bash
            set -euo pipefail
            if [[ "$2" == "--column-codecs" ]]; then
              printf 'column_id\tcolumn_name\tcodec_id\tcodec_name\tchunk_count\n'
              printf '10\tquality\t8\tqual_pack_compressed\t1\n'
            elif [[ "$2" == "--columns" ]]; then
              printf 'chunk_index\tcolumn_id\tcolumn_name\tcodec_id\tcodec_name\tpayload_offset\tpayload_length\n'
              printf '0\t10\tquality\t8\tqual_pack_compressed\t0\t1\n'
            else
              exit 2
            fi
            """,
        )

        return subprocess.run(
            [
                "bash",
                shell_path(self.script),
                "--alignx",
                shell_path(alignx),
                "--samtools",
                shell_path(samtools),
                "--inspector",
                shell_path(inspector),
                "--input",
                shell_path(input_bam),
                "--region",
                "chrToy:1-10",
                "--work-dir",
                shell_path(work_dir),
                *extra_args,
            ],
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env={**os.environ, "LC_ALL": "C"},
        )

    def test_default_does_not_pass_quality_compression_option(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(tmp)

            self.assertEqual(result.returncode, 0, result.stderr)
            log = (tmp / "alignx.log").read_text(encoding="utf-8")
            self.assertIn("convert ", log)
            self.assertNotIn("--axf1-quality-compression", log)
            self.assertIn("axf1_quality_compression\tnone", result.stdout)

    def test_zstd_quality_compression_is_passed_to_convert(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(
                tmp,
                "--axf1-quality-compression",
                "zstd",
                "--expect-codec",
                "quality=qual_pack_compressed",
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            log = (tmp / "alignx.log").read_text(encoding="utf-8")
            self.assertIn("--axf1-quality-compression zstd", log)
            self.assertIn("axf1_quality_compression\tzstd", result.stdout)

    def test_rejects_invalid_quality_compression(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(tmp, "--axf1-quality-compression", "lz4")

            self.assertEqual(result.returncode, 2)
            self.assertIn("--axf1-quality-compression must be none or zstd", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
