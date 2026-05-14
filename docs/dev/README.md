# Developer Guide

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.25+ | Required for `CMakePresets.json` support |
| vcpkg | latest | `VCPKG_ROOT` env var must be set |
| MSVC (Windows) | VS 2026 / v143 | C++23 support required |
| GCC (Linux) | 13+ | Or Clang 17+ |
| Ninja (Linux) | any | Preferred generator on Linux |

## First-time setup (Windows)

```powershell
# Ensure VCPKG_ROOT is set
$env:VCPKG_ROOT = "C:/vcpkg"

# Configure + build
cmake --preset windows-debug
cmake --build --preset windows-debug

# Run tests
ctest --preset windows-debug --output-on-failure
```

vcpkg installs dependencies from `vcpkg.json` automatically on first configure.
The Windows manifest intentionally does not install htslib when the default
`x64-windows` triplet reports it as unsupported; scaffold builds continue with
BAM/CRAM I/O disabled.

## First-time setup (Linux)

```bash
export VCPKG_ROOT="$HOME/vcpkg"

cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

## First-time setup (WSL + Conda)

Use this path for Phase 1 development while Windows vcpkg htslib support is
unresolved.

```bash
# One-time environment creation
mamba create -y -n alignx-dev -c conda-forge -c bioconda \
  cmake ninja pkg-config cxx-compiler htslib samtools gtest cli11 zlib xz clang-format

# Configure + build + test
mamba run -n alignx-dev cmake --preset wsl-debug
mamba run -n alignx-dev cmake --build --preset wsl-debug
mamba run -n alignx-dev ctest --preset wsl-debug --output-on-failure
```

## Scaffold-only mode

Phase 0 includes only a minimal `alignx_lib` scaffold source so GoogleTest can
create a smoke-test target. No functional `alignx` executable is created until
`src/main.cpp` and Phase 1 sources exist.

## Adding a new source module

1. Create `src/<module>/foo.cpp` (and optionally `src/<module>/foo.hpp`).
2. CMake picks it up automatically via `GLOB_RECURSE CONFIGURE_DEPENDS`.
3. Add unit tests under `tests/unit/test_foo.cpp`.
4. Run `cmake --build --preset windows-debug && ctest --preset windows-debug`.

## Code style

- C++23 standard. No `using namespace std;` in headers.
- Prefer `std::span`, `std::print`, structured bindings over C equivalents.
- Format with `clang-format` (config at repo root) before committing.
- Comments only when the WHY is non-obvious.

## htslib target verification

After first configure, check CMake output for:
```
htslib found via CMake config
```
or
```
htslib found via pkg-config
```

If neither appears, htslib is missing. Install via vcpkg:
```bash
vcpkg install htslib
```
or on Ubuntu:
```bash
apt install libhts-dev
```

On Windows, the default vcpkg `x64-windows` triplet may report htslib as
unsupported. In that case, use the scaffold build without htslib until a
supported Windows HTSlib strategy is selected.

## AXF roundtrip correctness smoke

Use `scripts/smoke_axf_roundtrip.sh` to verify that a BAM region and the AXF0
MVP converted from that BAM produce identical SAM stdout for the same region.
This script performs no timing, repeats, profiling, or benchmark reporting.
The script passes the same region to `alignx convert --region`, so it does not
convert the whole BAM.

```bash
mamba run -n alignx-dev cmake --build --preset wsl-release
mkdir -p /tmp/alignx_axf_smoke
ALIGNX_BIN=build/wsl-release/alignx \
  scripts/smoke_axf_roundtrip.sh \
  --input tests/toy_data/toy_alignment.sorted.bam \
  --region chrToy:1-250 \
  --work-dir /tmp/alignx_axf_smoke
```

For real BAMs, choose a work directory with enough space because the script
writes an AXF file plus BAM/AXF SAM outputs for the requested region. For large
datasets, prefer running the smoke on `missmi-server00` under `/mypool/alignx/`
rather than growing the local WSL VHDX.

## AXF0 and AXF1 development status

AXF0 is the row-preserving MVP path. It stores SAM-line payloads in indexed AXF
blocks so conversion, view routing, region filtering, and stdout parity can be
validated before the columnar codec stack is complete.

AXF1 is the raw-column correctness scaffold. It writes independently encoded
raw columns, supports metadata-first lazy region view, and selectively decodes
`POS` plus `CIGAR` before full output columns. The current converter is
mapped-record only: unmapped records and invalid reference spans are skipped
until unmapped AXF1 semantics are designed.

`alignx view` detects AXF0 vs AXF1 by file magic before using extension-based
assumptions. `.axf1` remains useful in tests and temporary files as a readability
cue, but the view path is not dependent on that extension.

AXF1 chunks currently use a deliberately tiny MVP max-record split to force
multi-chunk correctness coverage on toy fixtures. Do not treat that threshold as
a performance policy. Production chunk sizing still needs a byte budget,
genomic span, record count, or hybrid policy.

Before running benchmarks, repeated timing, profiling, or real-data performance
work, notify the user and wait for confirmation. Lightweight builds, unit tests,
and toy correctness smokes are allowed without benchmark confirmation.
