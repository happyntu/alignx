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
- `alignx index <bam>` builds projected `.axf.idx` files from `.bai` / `.csi`
- AXF1 raw-column file reader/writer and internal region view helper
- `convert::convert_bam_to_axf1_mvp()` for toy BAM -> AXF1 correctness work
- `alignx convert --format AXF1` opt-in path and `.axf1` view routing
- AXF1 metadata-first reader for `.axf1` region view; query decodes only overlapping chunks
- AXF1 view uses selective `POS` + `CIGAR` column decode before full output decode
- AXF1 converter writes deterministic MVP chunks for toy correctness work
- `alignx view` detects AXF0/AXF1 inputs by file magic before extension assumptions
- AXF1 tests cover multi-chunk and multi-reference query ordering
- AXF1 converter tests explicitly cover mapped-only toy output
- AXF1 production chunk sizing policy design note
- AXF1 converter implements hybrid chunk sizing policy
- AXF1 hybrid chunk sizing remote HG002 chr1 small-region correctness smoke
- AXF1 region-converted subset boundary smoke on HG002 chr1 small region
- AXF1 v2 file metadata records source path, conversion region, and subset flag
- AXF1 v2 metadata remote HG002 chr1 small-region correctness smoke
- AXF1 v2 metadata corruption tests cover full-file reader, metadata-only reader, and Python inspector
- AXF1 source identity design note: keep v2 as-is for now; future identity should prefer file size, mtime, and BAM header SHA-256 over default full-content hashing
- AXF1 metadata extensibility design note: future v3 metadata should use typed key/value entries with required/optional flags; keep v2 as active writer version for now
- AXF1 POS column delta-varint codec for monotonic chunks, with raw fallback for non-monotonic record order
- AXF1 POS delta-varint remote HG002 chr1 small-region correctness smoke
- AXF1 FLAG bit-pack codec with raw fallback when bit-packing is not smaller
- AXF1 FLAG bit-pack remote HG002 chr1 small-region correctness smoke
- AXF1 MAPQ run-length codec with raw fallback when RLE is not smaller
- AXF1 MAPQ RLE remote HG002 chr1 small-region correctness smoke
- AXF1 column codec inspector verified on the remote HG002 MAPQ RLE smoke output
- `scripts/smoke_axf1_codecs.sh` correctness smoke for AXF1 conversion, three-way SAM stdout parity, and codec distribution inspection
- AXF1 SEQ codec design note: implement chunk-local 2-bit literal with raw fallback before any reference-delta codec; defer reference-delta until reference identity metadata and CIGAR/strand semantics are designed
- AXF1 SEQ 2-bit literal codec with raw fallback for non-ACGT sequences
- AXF1 SEQ 2-bit literal toy smoke verified byte-identical SAM stdout and `seq_2bit_literal` codec distribution
- AXF1 SEQ 2-bit literal remote HG002 chr1 small-region correctness smoke
- AXF1 CIGAR codec design note: prefer self-contained token stream with raw fallback before dictionary, delta, or reference-aware CIGAR codecs
- Benchmark scripts validate BAM input, `alignx index` preflight, and `alignx view` vs `samtools view` stdout parity
- Benchmark scripts default to WSL release builds
- Benchmark scripts emit raw timing TSV plus median/p95/outlier summary TSV
- Remote HG002 chr1:1M-2M engineering benchmark completed for `alignx view` vs `samtools view`

Remaining implementation targets:
- Tune AXF1 hybrid chunk sizing thresholds with correctness smoke checks and confirmed benchmarks

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

For commands that write binary genomics files to stdout (BAM/CRAM/BCF or compressed BGZF output), do not use `conda run ... > output.bam`; activate the environment in a shell first, then run the tool and redirect output. This avoids any risk of environment-wrapper text contaminating binary files.

`alignx convert --region` writes a subset AXF/AXF1 file for the selected source
records. It is not a complete chromosome or whole-BAM cache. Queries outside the
conversion region are answered only from records stored in the subset file and
can differ from full BAM queries. AXF1 v2 records source path, conversion region,
and subset flag in file metadata; legacy AXF1 v1 files are read as full-input
caches with empty metadata.

**Remote large-data execution:**
- For large BAM/CRAM/genome assets and benchmark/profiling workloads, prefer `missmi-server00` storage and execution over this Windows machine or WSL disk.
- The local Codex session remains the orchestrator and should execute remote commands over SSH; do not require a persistent Git clone on the server.
- `missmi-server00` SSH: `ssh -i C:/Users/user/.ssh/missmi_server00 -p 19822 happyntu@140.112.183.210`.
- `missmi-server00` alignx remote root: `/mypool/alignx/` on the large ZFS `mypool` filesystem.
- Recommended remote layout: `/mypool/alignx/bin`, `/mypool/alignx/data`, `/mypool/alignx/refs`, `/mypool/alignx/results`, `/mypool/alignx/logs`, `/mypool/alignx/tmp`, and `/mypool/alignx/test_data`.
- Remote alignx binaries can use the existing server HTSlib environment with `LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib`.
- `scripts/inspect_axf1_metadata.py` can inspect AXF1 header, v2 source/subset metadata, chunk-index metadata, and per-chunk column codec metadata without decoding payloads; copy it to `/mypool/alignx/bin` for remote smoke checks when needed.
- If repository scripts or binaries are needed remotely, stream the script, copy the built binary, or create a minimal runtime snapshot from the local working tree over SSH. Keep the authoritative repository and commits on the local Windows workspace.

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
alignx convert  <input.bam|cram>  -o <output.axf>      # BAM/CRAM → AXF0 by default
alignx convert  <input.bam>       -o <output.axf1> --format AXF1 # opt-in columnar AXF1 MVP
alignx view     <input.axf|axf1|bam> [region]           # region query → SAM stdout
alignx export   <input.axf>       -o <output.bam|cram> # AXF → BAM/CRAM
alignx pileup   <input.axf|bam>   <region>             # per-base coverage
alignx stats    <input.axf|bam>                        # flag/MAPQ/insert stats
alignx index    <input.bam|axf>  [-o output.axf.idx]    # build/rebuild .axf.idx
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
- Do not stage large real datasets inside WSL `/home` when `/mypool` on `missmi-server00` is suitable; WSL VHDX growth consumes the Windows C: drive and may require manual compaction.
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
- BAM reading only for production query paths (no CRAM yet).
- HTSlib wrapper in `src/io/`, BAI reader in `src/index/`.
- AXFIndex v1: sorted interval list (no interval tree yet).
- `alignx view` supports BAM plus AXF0 MVP row-preserving payloads for toy correctness;
  `alignx stats` remains BAM-only.
- AXF0 MVP staging is allowed per ADR-005: `alignx convert <bam> -o <axf>` may write
  row-preserving SAM-line payloads for toy correctness before the final columnar
  AXF codec path is implemented.
- Benchmark target completed: `samtools view` on chr1:1M-2M latency comparison
  has an engineering baseline in `docs/research/phase1-bam-view-findings.md`.

Do not claim full samtools replacement. alignx v0.1 targets **region query acceleration** only.

## Benchmark and Paper Context

Before running benchmarks or profiling workloads, notify the user and wait for confirmation because the development machine is often busy. Lightweight build/test/smoke checks are allowed without confirmation; repeated timing runs, benchmark scripts, and profiling on real BAM/CRAM data require confirmation first.

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
