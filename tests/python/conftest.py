from __future__ import annotations

import subprocess
import sys
from pathlib import Path

import pytest

REPO_ROOT = Path(__file__).resolve().parents[2]
TOY_DATA = REPO_ROOT / "tests" / "toy_data"


def _find_build_dir() -> Path:
    for name in ("wsl-release", "wsl-debug", "linux-release", "linux-debug"):
        candidate = REPO_ROOT / "build" / name
        if (candidate / "python").is_dir():
            return candidate
    raise RuntimeError("no build directory with Python module found; build with -DALIGNX_BUILD_PYTHON=ON")


BUILD_DIR = _find_build_dir()
ALIGNX_BIN = BUILD_DIR / "alignx"
PYTHON_DIR = BUILD_DIR / "python"


def pytest_configure(config: pytest.Config) -> None:
    if str(PYTHON_DIR) not in sys.path:
        sys.path.insert(0, str(PYTHON_DIR))


@pytest.fixture
def toy_bam() -> Path:
    return TOY_DATA / "toy_alignment.sorted.bam"


@pytest.fixture
def toy_ref() -> Path:
    return TOY_DATA / "toy_ref.fa"


@pytest.fixture
def toy_axf1(toy_bam: Path, tmp_path: Path) -> Path:
    out = tmp_path / "toy.axf1"
    subprocess.check_call(
        [str(ALIGNX_BIN), "convert", str(toy_bam), "-o", str(out), "--format", "AXF1"],
    )
    return out


@pytest.fixture
def toy_axf1_with_ref(toy_bam: Path, toy_ref: Path, tmp_path: Path) -> Path:
    out = tmp_path / "toy_ref.axf1"
    subprocess.check_call(
        [
            str(ALIGNX_BIN), "convert", str(toy_bam), "-o", str(out),
            "--format", "AXF1", "--reference", str(toy_ref),
        ],
    )
    return out
