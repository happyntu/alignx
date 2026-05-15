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


class SmokeAxfRoundtripScriptTest(unittest.TestCase):
    def setUp(self) -> None:
        if SCRIPT is None:
            raise RuntimeError("missing smoke script path argument")
        self.script = SCRIPT

    def run_smoke(self, tmp: Path, *extra_args: str) -> subprocess.CompletedProcess[str]:
        input_bam = tmp / "input.bam"
        input_bam.write_bytes(b"fake bam")
        work_dir = tmp / "work"
        alignx = tmp / "alignx"
        log = tmp / "alignx.log"

        write_executable(
            alignx,
            f"""\
            #!/usr/bin/env bash
            set -euo pipefail
            echo "$@" >> "{shell_path(log)}"
            if [[ "$1" == "view" ]]; then
              printf 'read001\\t0\\tchrToy\\t1\\t60\\t10M\\t*\\t0\\t0\\tACGT\\tFFFF\\n'
              exit 0
            fi
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
            exit 2
            """,
        )

        return subprocess.run(
            [
                "bash",
                shell_path(self.script),
                "--alignx",
                shell_path(alignx),
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

    def test_default_uses_axf0_without_axf1_quality_flag(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(tmp)

            self.assertEqual(result.returncode, 0, result.stderr)
            log = (tmp / "alignx.log").read_text(encoding="utf-8")
            self.assertIn("convert ", log)
            self.assertIn("--format AXF0", log)
            self.assertNotIn("--axf1-quality-compression", log)
            self.assertIn("format\tAXF0", result.stdout)
            self.assertIn("axf1_quality_compression\tnone", result.stdout)

    def test_axf1_passes_quality_compression_flag(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(
                tmp,
                "--format",
                "AXF1",
                "--axf1-quality-compression",
                "zstd",
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            log = (tmp / "alignx.log").read_text(encoding="utf-8")
            self.assertIn("--format AXF1", log)
            self.assertIn("--axf1-quality-compression zstd", log)
            self.assertIn("format\tAXF1", result.stdout)
            self.assertIn("axf1_quality_compression\tzstd", result.stdout)

    def test_rejects_invalid_format(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_name:
            tmp = Path(tmp_name)
            result = self.run_smoke(tmp, "--format", "CRAM")

            self.assertEqual(result.returncode, 2)
            self.assertIn("--format must be AXF0 or AXF1", result.stderr)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
