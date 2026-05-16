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
- AXF1 view uses selective `POS` + `CIGAR` column decode for filtering, then selective output-column decode for matched records (no full-chunk reads)
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
- AXF1 CIGAR token codec with raw fallback for non-tokenizable CIGAR strings
- AXF1 CIGAR token toy smoke verified byte-identical SAM stdout and `cigar_token` codec distribution
- AXF1 CIGAR token remote HG002 chr1 small-region correctness smoke
- AXF1 expected-codec remote HG002 chr1 small-region correctness smoke asserts POS/FLAG/MAPQ/CIGAR/SEQ codec distribution
- AXF1 column payload summary tool; HG002 small-region summary shows raw QUAL dominates remaining payload
- AXF1 QUAL codec design note: prefer lossless chunk-local byte RLE with raw fallback before context models, zstd wrappers, or lossy binning
- AXF1 QUAL byte RLE codec with raw fallback for `*`, empty, and non-beneficial quality strings
- AXF1 QUAL byte RLE toy smoke verified byte-identical SAM stdout and `qual_rle` codec distribution
- AXF1 QUAL byte RLE remote HG002 chr1 small-region correctness smoke verified stdout parity; QUAL fell back to raw on 7/7 chunks
- AXF1 next QUAL codec design note: prefer lossless chunk-local alphabet bit-pack (`qual_pack`) with raw fallback before context models or compressed column wrappers
- AXF1 QUAL alphabet bit-pack codec with raw fallback; writer chooses the smallest of raw, `qual_rle`, and `qual_pack`
- AXF1 QUAL alphabet bit-pack toy smoke verified byte-identical SAM stdout and `qual_pack` codec distribution
- AXF1 QUAL alphabet bit-pack remote HG002 chr1 small-region correctness smoke verified stdout parity and `qual_pack` on 7/7 chunks
- AXF1 compressed payload wrapper design note: prefer shared payload envelopes over one-off `*_zstd` codec ids before adding compression dependencies
- AXF1 `qual_pack_compressed` stored-envelope reader path with raw/base writer fallback and WSL `ctest` coverage
- AXF1 zstd payload wrapper feature-gate design: optional `ALIGNX_ENABLE_ZSTD`, no default writer behavior change, clear unsupported-compression errors without zstd
- AXF1 zstd feature-gate skeleton: `ALIGNX_ENABLE_ZSTD=OFF` by default; disabled builds reject zstd payload envelopes without changing writer output
- AXF1 zstd compressed payload envelope reader: `ALIGNX_ENABLE_ZSTD=ON` can decode zstd-wrapped `qual_pack_compressed`; writer still emits base codecs by default
- AXF1 explicit zstd quality writer path: `alignx convert --format AXF1 --axf1-quality-compression zstd` may emit zstd-wrapped `qual_pack_compressed` when built with `ALIGNX_ENABLE_ZSTD=ON`; default remains uncompressed base codec selection and explicit zstd falls back when the wrapper is not smaller
- AXF1 zstd quality writer remote HG002 chr1 small-region correctness smoke verified stdout parity and `qual_pack_compressed` on 7/7 quality chunks
- AXF1 zstd quality writer size-policy remote HG002 chr1 small-region correctness smoke reverified stdout parity and `qual_pack_compressed` on 7/7 quality chunks
- `scripts/smoke_axf1_codecs.sh` supports `--axf1-quality-compression none|zstd` for reusable zstd quality writer correctness smoke checks
- Scripted AXF1 zstd quality writer remote HG002 chr1 small-region correctness smoke passed with `scripts/smoke_axf1_codecs.sh --axf1-quality-compression zstd`
- AXF1 LZ4 compressed payload decision: keep `compression_id=2` reserved but defer implementation until profiling shows a fast-profile need
- AXF1 QUAL query-impact observation design note: define future benchmark axes for wrapper vs QUAL-specific model
- AXF1 chunk sizing tuning plan script and hidden env overrides for target/max bytes, record count, and genomic span
- Remote AXF1 chunk sizing sweeps on missmi-server00 were rerun after refreshing `/mypool/alignx/bin/alignx`; smaller_chunks and denser_chunks materially change chunk shape, span_biased still matches baseline on chr1:1000000-2000000, chr1:121000000-142000000, and chrY:20000000-21000000, but span_tight at 50 kb finally changes chunk shape on chr1:121000000-142000000 and chrY:20000000-21000000, so 50 kb is the first span-sensitive candidate worth keeping and 250 kb should be treated as a loose experiment only
- Remote benchmark preflight for the final AXF1 chunk-sizing comparison passed on missmi-server00 for chr1:1000000-2000000, chr1:121000000-142000000, and chrY:20000000-21000000; the ready-to-run comparison is baseline (`max_genomic_span=1,000,000`) versus span_tight (`max_genomic_span=50,000`), but timed repeats still require explicit user confirmation
- Timed AXF1 chunk-sizing benchmarks completed on missmi-server00 for baseline vs span_tight across chr1:1000000-2000000, chr1:121000000-142000000, and chrY:20000000-21000000; span_tight was faster only on chr1:121000000-142000000 and slower on the other two regions, so it is not a clear default replacement for the conservative byte-budget policy. Final recommendation: keep the current hybrid default (256 KiB target / 512 KiB max / 4096 records / 1,000,000 bp span) and do not promote span_tight
- Benchmark scripts validate BAM input, `alignx index` preflight, and `alignx view` vs `samtools view` stdout parity
- Benchmark scripts default to WSL release builds
- Benchmark scripts emit raw timing TSV plus median/p95/outlier summary TSV
- Remote HG002 chr1:1M-2M engineering benchmark completed for `alignx view` vs `samtools view`
- AXF1 view profiling hook: `ALIGNX_PROFILE_AXF1=1` emits AXF1-specific stderr TSV counters for chunk selection, selective decode, output-column decode, filtering, formatting, writing, and byte totals without changing SAM stdout
- AXF1 profiling preflight completed on missmi-server00 for HG002 `chr1:1000000-1010000`; stdout SHA matched `samtools view`, and the first snapshot shows output-column decode dominating selective decode and formatting time
- `alignx coverage <bam|axf1> <region>` subcommand: POS-only selective column decode for per-base coverage, with `--output-mode summary|tsv` and `ALIGNX_PROFILE_COVERAGE=1` profiling hook
- AXF1 coverage path reads only POS+CIGAR columns; BAM coverage path uses HTSlib full-record parse as baseline comparison
- `Axf1FileReader` persistent ifstream and `read_chunk_columns_selective()` for column-only I/O: reads only chunk header + requested column payloads instead of full chunk byte range
- AXF1 selective column I/O coverage benchmark on HG002: 2.97x faster (chr1:1M-2M), 1.16x faster (chr1:121M-142M), 2.22x faster (chrY:20M-21M) vs BAM full-record parse
- Read filter support: `--flag-exclude` and `--min-mapq` for view, coverage, and pileup subcommands; AXF1 path adds FLAG+MAPQ to selective filter column list only when filter is active
- `alignx pileup <bam|axf1> <region>` subcommand: outputs per-base depth in TSV format compatible with `samtools depth`; internally routes to the coverage engine with `--output-mode tsv`
- `scripts/bench_pileup.sh` pileup benchmark harness: compares `samtools depth`, `alignx pileup <bam>`, `alignx pileup <axf1>` with SHA-based correctness preflight, warmup, repeats, and median/p95/outlier summary TSV
- Pileup integration tests: `PileupFullRangeFidelity` (hardcoded expected depth for chrToy:101-160), `PileupAxf1MatchesBamPileup` (BAM→AXF1→pileup byte-identical), `PileupAxf1FilterMatchesBamFilter` (`--flag-exclude 16` parity across BAM and AXF1 paths)

Remaining implementation targets:
- Round-trip fidelity: BAM → AXF → BAM → diff
  - AXF1 round-trip smoke now supports `scripts/smoke_axf_roundtrip.sh --format AXF1`; a direct AXF export path is still not implemented
  - Toy AXF1 roundtrip smoke passed with `scripts/smoke_axf_roundtrip.sh --format AXF1` on `tests/toy_data/toy_alignment.sorted.bam`
- Benchmark: AXF coverage (POS only) vs BAM full-record parse on chr1
  - Timed benchmark completed on missmi-server00 for chr1:1000000-2000000, chr1:121000000-142000000, and chrY:20000000-21000000; AXF1 view was correct but slower than BAM-backed view on all tested regions, so the v0.3 success criterion is not met yet
  - AXF1 profiling hooks and remote correctness preflight are now in place; the next optimization pass should target output-column decode cost first

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
alignx coverage <input.axf1|bam> <region>               # per-base coverage (POS-only for AXF1)
alignx pileup   <input.axf1|bam>  <region>             # per-base depth TSV (samtools depth compatible)
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
