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

## AXF1 codec correctness smoke

Use `scripts/smoke_axf1_codecs.sh` to verify AXF1 conversion, AXF1 view output,
BAM-backed `alignx view`, and `samtools view` for the same region. The script
also records AXF1 column codec distribution through
`scripts/inspect_axf1_metadata.py --column-codecs`. It performs no timing,
repeats, profiling, or benchmark reporting, and it should not be used as a
performance result.

```bash
mamba run -n alignx-dev cmake --build --preset wsl-release
mkdir -p /tmp/alignx_axf1_codec_smoke
scripts/smoke_axf1_codecs.sh \
  --alignx build/wsl-release/alignx \
  --samtools samtools \
  --inspector scripts/inspect_axf1_metadata.py \
  --input tests/toy_data/toy_alignment.sorted.bam \
  --region chrToy:1-250 \
  --work-dir /tmp/alignx_axf1_codec_smoke \
  --expect-codec pos=pos_delta_varint \
  --expect-codec flag=flag_bitpack \
  --expect-codec cigar=cigar_token \
  --expect-codec sequence=seq_2bit_literal \
  --expect-codec quality=qual_pack
```

For zstd-enabled AXF1 quality writer checks, add
`--axf1-quality-compression zstd` and assert the wrapper codec when the selected
region is expected to benefit from it:

```bash
scripts/smoke_axf1_codecs.sh \
  --alignx /mypool/alignx/bin/alignx_zstd_quality_size_policy_3e7f980 \
  --samtools samtools \
  --inspector /mypool/alignx/bin/inspect_axf1_metadata.py \
  --input /mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam \
  --region chr1:1000000-1010000 \
  --work-dir /mypool/alignx/tmp/axf1_zstd_quality_size_policy_script_smoke_hg002_chr1_1000000_1010000_20260515 \
  --axf1-quality-compression zstd \
  --expect-codec quality=qual_pack_compressed
```

Use `--expect-codec column=codec` when a smoke region is expected to use a
specific codec for every chunk of that column. The option may be repeated and
accepts either column names or numeric column ids, and either codec names or
numeric codec ids. If a column falls back to raw for any chunk, the expectation
fails and the script prints the actual codec distribution.

For large BAMs, copy the script and inspector to `missmi-server00` and use a
work directory under `/mypool/alignx/tmp`. The HG002 chr1 small-region script
smoke on 2026-05-14 used
`/mypool/alignx/tmp/axf1_codec_script_smoke_hg002_chr1_1000000_1010000_20260514`
and confirmed byte-identical SAM stdout plus POS/FLAG/MAPQ codec distribution
of `pos_delta_varint`, `flag_bitpack`, and `mapq_rle` on all 7 chunks.
The toy smoke path has also verified SEQ `seq_2bit_literal` distribution after
the SEQ 2-bit literal codec was added.
It has also verified CIGAR `cigar_token` distribution after the CIGAR token
codec was added.
It has also verified QUAL `qual_rle` distribution after the QUAL byte RLE codec
was added.
It has also verified QUAL `qual_pack` distribution after the QUAL alphabet
bit-pack codec was added.

Remote HG002 SEQ 2-bit literal smoke on 2026-05-14 used
`/mypool/alignx/tmp/axf1_seq_codec_smoke_hg002_chr1_1000000_1010000_20260514`
and confirmed byte-identical SAM stdout plus SEQ `seq_2bit_literal`
distribution on all 7 chunks.
Remote HG002 CIGAR token smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_cigar_codec_smoke_hg002_chr1_1000000_1010000_20260515`
and confirmed byte-identical SAM stdout plus CIGAR `cigar_token`
distribution on all 7 chunks.
For the HG002 chr1 small-region codec smoke, the current expected codecs are
`pos_delta_varint`, `flag_bitpack`, `mapq_rle`, `cigar_token`, and
`seq_2bit_literal`. QUAL may still fall back to raw on HG002 regions unless
`qual_rle` or `qual_pack` is smaller for every chunk.
Remote HG002 QUAL byte RLE smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_qual_rle_smoke_hg002_chr1_1000000_1010000_20260515`
and confirmed byte-identical SAM stdout with the same stdout SHA-256
(`6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`)
for 64 records. `quality` fell back to raw on all 7 chunks, so `qual_rle` is
not currently an expected HG002 codec for this region.
Remote HG002 QUAL alphabet bit-pack smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_qual_pack_smoke_hg002_chr1_1000000_1010000_20260515`
and confirmed byte-identical SAM stdout with the same stdout SHA-256
(`6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`)
for 64 records. `quality` used `qual_pack` on all 7 chunks.
Remote HG002 expected-codec smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_expect_codec_smoke_hg002_chr1_1000000_1010000_20260515`
and asserted all five expected codecs with byte-identical SAM stdout
(`6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`)
for 64 records. The AXF1 output was 1,172,296 bytes.
Remote HG002 zstd quality writer smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_zstd_quality_writer_smoke_hg002_chr1_1000000_1010000_20260515`
with `alignx convert --format AXF1 --axf1-quality-compression zstd`.
It confirmed byte-identical SAM stdout with the same stdout SHA-256
(`6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`)
for 64 records. `quality` used `qual_pack_compressed` on all 7 chunks because
the zstd envelope was smaller than the regular best quality payload, and the AXF1
output was 798,039 bytes.
After adding size-policy fallback, the same correctness smoke was rerun with
`/mypool/alignx/bin/alignx_zstd_quality_size_policy_3e7f980` under
`/mypool/alignx/tmp/axf1_zstd_quality_size_policy_smoke_hg002_chr1_1000000_1010000_20260515`.
The result remained byte-identical with the same stdout SHA-256, `quality` still
used `qual_pack_compressed` on all 7 chunks, and the AXF1 output remained
798,039 bytes.
The reusable script path was verified on 2026-05-15 under
`/mypool/alignx/tmp/axf1_zstd_quality_size_policy_script_smoke_hg002_chr1_1000000_1010000_20260515`
using `scripts/smoke_axf1_codecs.sh --axf1-quality-compression zstd
--expect-codec quality=qual_pack_compressed`. It reported `OK axf1_codecs`,
`axf1_quality_compression=zstd`, the same stdout SHA-256, and 64 records.

## AXF1 column payload summary

Use `scripts/summarize_axf1_columns.py` to summarize column payload sizes from
AXF1 metadata without decoding payloads. This is an observability tool for
choosing the next codec target; it is not a benchmark.

```bash
scripts/summarize_axf1_columns.py /tmp/alignx_axf1_codec_smoke/sample.axf1
```

The output is TSV with total payload bytes, chunk count, codec distribution,
average bytes per chunk, and percent of total column payload for each column.
The 2026-05-15 HG002 chr1 small-region AXF1 summary showed `quality` as the
dominant remaining payload column at 77.843% of column payload bytes, followed
by `sequence` at 19.470% and `cigar` at 1.774%. After `qual_rle`, the same
HG002 region still falls back to raw for QUAL, so this points to a future
lossless QUAL codec beyond simple byte RLE.
After `qual_pack`, the same region used `qual_pack` on all 7 chunks and reduced
`quality` payload from 907,624 bytes to 794,762 bytes, or 75.468% of column
payload bytes.
With explicit zstd quality compression, the same region used
`qual_pack_compressed` on all 7 chunks and reduced `quality` payload to
533,367 bytes, or 67.369% of column payload bytes.

## AXF0 and AXF1 development status

AXF0 is the row-preserving MVP path. It stores SAM-line payloads in indexed AXF
blocks so conversion, view routing, region filtering, and stdout parity can be
validated before the columnar codec stack is complete.

AXF1 is the columnar correctness scaffold. It writes independently encoded
columns, supports metadata-first lazy region view, and selectively decodes
`POS` plus `CIGAR` before full output columns. `POS` uses delta-varint encoding
for monotonic chunks and raw fallback otherwise; `FLAG` uses bit-packing when
smaller than raw `u16`; `MAPQ` uses RLE when smaller than raw `u8`. The current converter is
mapped-record only: unmapped records and invalid reference spans are skipped
until unmapped AXF1 semantics are designed.

`alignx view` detects AXF0 vs AXF1 by file magic before using extension-based
assumptions. `.axf1` remains useful in tests and temporary files as a readability
cue, but the view path is not dependent on that extension.

AXF1 chunks currently use a deliberately tiny MVP max-record split to force
multi-chunk correctness coverage on toy fixtures. Do not treat that threshold as
a performance policy. Production chunk sizing still needs a byte budget,
genomic span, record count, or hybrid policy.
The current tuning plan is documented in
`docs/research/axf1-chunk-sizing-policy.md`, and the benchmark-config skeleton
for later HG002 comparison runs lives in
`benchmarks/configs/phase1_axf1_chunk_sizing_hg002.yaml`.

Before running benchmarks, repeated timing, profiling, or real-data performance
work, notify the user and wait for confirmation. Lightweight builds, unit tests,
and toy correctness smokes are allowed without benchmark confirmation.
