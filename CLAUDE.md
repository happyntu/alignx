# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Status

alignx is currently in **Phase 0 — Design, Scaffolding & Data Preparation**.

Completed:
- repository layout scaffold
- documentation structure (architecture, roadmap, ADRs, research)
- CMake/vcpkg build scaffold
- `.clang-format` config
- basic GoogleTest unit test stub files
- initial benchmark script and manifest scaffold
- toy SAM/BAM/BAI fixtures in `tests/toy_data/`

Phase 0 remaining:
- none

Phase 1 (v0.1) is in progress.

Completed:
- HTSlib wrapper: `BamReader` with `open`, `fetch(region)`, `next_record`
- `alignx view <bam> <region>` minimal CLI path for BAM region output
- `alignx stats <bam>` minimal TSV summary for record count, FLAG, MAPQ, and nonzero insert sizes
- BAI index reader metadata parser
- CSI index reader metadata parser
- `AXFIndex` v1 sorted interval list with binary read/write and CRC footer
- BAI/CSI bin projection into `AXFIndex` v1 intervals

Remaining implementation targets:
- Benchmark: `alignx view` vs `samtools view` on chr1:1M-2M

## Build & Test Commands

**Windows (MSVC via vcpkg + CMakePresets):**
```powershell
# Configure
cmake --preset windows-debug        # VS 2026 Debug + vcpkg
cmake --preset windows-release      # VS 2026 Release + vcpkg

# Build
cmake --build --preset windows-debug
cmake --build --preset windows-release

# Run all tests
ctest --preset windows-debug --output-on-failure

# Run single binary directly
./build/windows-debug/tests/Debug/unit_tests.exe
```

**Linux (GCC 13+ / Clang 17+):**
```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

**WSL (Conda + Ninja, current Phase 1 path):**
```bash
mamba run -n alignx-dev cmake --preset wsl-debug
mamba run -n alignx-dev cmake --build --preset wsl-debug
mamba run -n alignx-dev ctest --preset wsl-debug --output-on-failure
```

**Note:** Phase 0 includes only a minimal `alignx_lib` scaffold source for test target creation.
No functional `alignx` executable exists until `src/main.cpp` and Phase 1 sources are added.

## htslib dependency

CMake tries `find_package(htslib CONFIG)` first (vcpkg), then `pkg_check_modules(htslib)`.

If configure warns "htslib not found":
- Windows: htslib may be unavailable via the default `x64-windows` vcpkg triplet; scaffold builds continue without BAM/CRAM I/O.
- Linux: `apt install libhts-dev` or `vcpkg install htslib`
- WSL current path: use the `alignx-dev` conda environment with `htslib` and `samtools`.

After first configure, check CMake output for the detection result.

## Source of Truth

When documentation disagrees, use this priority:

1. `docs/adr/`
2. `docs/architecture.md`
3. `docs/roadmap.md`
4. Implementation in `src/` and `tests/`
5. Exploratory notes in `docs/research/`

Current decisions:
- Language and build: `docs/adr/adr-001-language-and-build.md`
- AXF format design: `docs/adr/adr-002-axf-format-design.md`
- Index design: `docs/adr/adr-003-index-design.md`
- Roadmap phases: `docs/roadmap.md`
- Developer workflow: `docs/dev/README.md`

## Current Tech Stack

| Layer | Current choice | Notes |
|---|---|---|
| Core engine | C++23 | All `src/` modules |
| BAM/CRAM I/O | HTSlib | Via vcpkg; see htslib note above |
| CLI | CLI11 | Subcommand + flag parsing |
| Testing | GoogleTest | vcpkg first, FetchContent fallback |
| Build system | CMake 3.25+ | Root `CMakeLists.txt` is authoritative |
| Dependency mgmt | vcpkg manifest mode | `vcpkg.json` at repo root |
| Compression (base) | zlib | BGZF decompression |
| SIMD | AVX2 (Phase 2+) | `ALIGNX_ENABLE_SIMD=ON` cmake option |
| GPU | CUDA (Phase 3+) | `ALIGNX_ENABLE_CUDA=ON` cmake option |

## Repository Layout

```text
alignx/
  src/            core C++23 implementation
    io/           HTSlib BamReader/BamWriter, AxfBlockReader/Writer, mmap
    index/        BaiReader, CsiReader, AXFIndex builder/reader
    format/       .axf chunk header/footer/magic, ColumnarChunk layout
    compress/     PosStream, QualCodec, CigarDelta, ReadNameDict
    query/        RegionQuery, RecordFilter (FLAG/MAPQ/tag)
    analysis/     Coverage, Pileup, Stats
    convert/      BamToAxf, AxfToBam pipelines
    cli/          Subcommand dispatch: convert/view/export/pileup/stats/index
    bench/        Timer, RSS, throughput metrics
  tests/          unit + integration tests
  docs/           architecture, ADRs, design, research
  benchmarks/     benchmark configs, results, plots
  data/           toy datasets and manifests
  scripts/        helper scripts
  examples/       small example inputs
```

### Where new files go

- New architectural decisions → `docs/adr/`
- Research notes → `docs/research/`
- Developer workflow notes → `docs/dev/`
- Benchmark run definitions → `benchmarks/configs/`
- Benchmark raw outputs → `benchmarks/results/`
- Benchmark summary figures → `benchmarks/plots/`
- Toy BAM/CRAM fixtures → `tests/toy_data/` or `data/toy/`

## CLI Design

```bash
alignx convert  <input.bam|cram>  -o <output.axf>     # BAM/CRAM → AXF
alignx view     <input.axf|bam>   [region]             # region query → SAM stdout
alignx export   <input.axf>       -o <output.bam|cram> # AXF → BAM/CRAM
alignx pileup   <input.axf|bam>   <region>             # per-base coverage
alignx stats    <input.axf|bam>                        # flag/MAPQ/insert stats
alignx index    <input.axf>                            # build/rebuild .axf.idx
```

## Development Expectations

### Before making changes

- Read the relevant ADR and architecture docs first.
- Check which phase the change belongs to — do not implement Phase 2+ features during Phase 1.
- Do not add SIMD intrinsics until `ALIGNX_ENABLE_SIMD` option is activated (Phase 2+).
- Do not add CUDA code until Phase 3.

### Build and test expectations

- Tests enabled from root `CMakeLists.txt` via `ALIGNX_BUILD_TESTS`.
- GoogleTest under `tests/CMakeLists.txt`; resolved by vcpkg first, FetchContent fallback.
- Windows builds use `cmake --preset windows-debug/release`.
- Do not introduce a second test framework.
- New source files are picked up automatically by `GLOB_RECURSE CONFIGURE_DEPENDS`.

### Correctness baseline (when implemented)

- `alignx view` output must be byte-identical to `samtools view` SAM output for the same region.
- `alignx pileup` coverage must match `samtools depth` exactly.
- BAM → AXF → BAM round-trip must produce zero diff on SAM output.

### Data hygiene

- Do not commit large BAM/CRAM/genome files. Use scripts or manifests.
- Small deterministic toy fixtures are acceptable in `tests/toy_data/` and `data/toy/`.
- Raw benchmark results go in `benchmarks/results/`, not `docs/`.

### Documentation hygiene

- Changed architecture or workflow → update corresponding docs.
- New decision → add or update an ADR.
- Keep exploratory notes in `docs/research/`, not ADRs.

## Code Style

- No `using namespace std;` in headers.
- Prefer `std::span`, structured bindings, `std::print`, `std::expected` over C equivalents.
- Inline comments only where the WHY is non-obvious.
- Format with `clang-format` (config at repo root) before committing.

## Implementation Boundaries (Phase 1)

For v0.1:
- BAM reading only (no CRAM yet, no AXF format writing yet).
- HTSlib wrapper in `src/io/`, BAI reader in `src/index/`.
- AXFIndex v1: sorted interval list (no interval tree yet).
- `alignx view` and `alignx stats` subcommands only.
- Benchmark target: `samtools view` on chr1:1M-2M latency comparison.

Do not claim full samtools replacement. alignx v0.1 targets **region query acceleration** only.

## Benchmark and Paper Context

This project may support a future methods paper around:
- columnar alignment format for selective-column I/O
- next-generation index for faster region queries
- SIMD / GPU decompression for high-throughput genomics
- cloud object storage friendly alignment access

Phase-sensitive comparison targets:
- v0.1: `samtools view` region query latency on chr1
- v0.2: `samtools depth`, `mosdepth` coverage speed
- v0.3: AXF columnar vs BAM full-record parse throughput
- v1.0: full format comparison vs BAM and CRAM
