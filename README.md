# alignx

A columnar alignment format for fast genomic region queries. C++23 core with Python bindings.

alignx introduces AXF1, a columnar storage format for aligned sequencing reads. It stores each SAM field (POS, FLAG, MAPQ, SEQ, QUAL, CIGAR, QNAME, TAGS, ...) as an independent column with field-specific codecs, enabling selective column I/O — read only the columns your analysis needs.

## Performance

Benchmarked on HG002 (GRCh38) across PacBio SequelII and Illumina 2x250bp datasets:

| Workload | AXF1 vs samtools | Details |
|:---|:---|:---|
| Region view (PacBio) | **3.8x - 5.4x faster** | mmap + parallel chunk decode, 8 worker threads |
| Region view (Illumina) | **3.0x - 3.9x faster** | Same architecture, shorter reads |
| Pileup / depth | **3.0x - 4.1x faster** | Selective POS+CIGAR column I/O |
| Lossless file size | **1.02x BAM** | Per-column zstd compression |
| Lossy + zstd file size | **0.52x - 0.96x BAM** | Illumina 8-level quality binning |

## Installation

### Python (from source)

Requires: C++23 compiler (GCC 13+, Clang 17+, MSVC 2022+), CMake 3.25+, zlib, HTSlib.

```bash
pip install .                    # build and install
pip install ".[pandas]"          # with pandas support
pip install ".[dev]"             # with pytest + pandas
```

### C++ only

```bash
cmake --preset linux-release     # or windows-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

## Quick Start: Python

### Coverage as numpy array

```python
import alignx

cov = alignx.coverage("sample.axf1", "chr1:1000000-2000000")
print(cov.depth)          # numpy uint32 array
print(cov.depth.mean())   # average depth
```

### Region query as pandas DataFrame

```python
from alignx import query_region

df = query_region("sample.axf1", "chr7:55019017-55211628",
                  columns=["pos", "flag", "mapq", "qname"])
high_mapq = df[df["mapq"] >= 30]
```

### Convert BAM to AXF1 and query

```python
import alignx

alignx.convert("input.bam", "output.axf1",
               region="chr1:1000000-2000000",
               compression="zstd")

sam_text = alignx.view("output.axf1", "chr1:1500000-1600000")
```

## Quick Start: CLI

```bash
# Convert BAM/CRAM to AXF1
alignx convert sample.bam -o sample.axf1 --format AXF1

# Region query (SAM stdout)
alignx view sample.axf1 chr1:1000000-2000000

# Per-base depth (samtools depth compatible)
alignx pileup sample.axf1 chr7:55019017-55211628

# Coverage summary
alignx coverage sample.axf1 chr1:1000000-2000000

# Export back to BAM
alignx export sample.axf1 -o sample.bam

# Build index
alignx index sample.axf1
```

### Filtering

```bash
# Exclude unmapped + duplicates, require MAPQ >= 20
alignx view sample.axf1 chr1:1000000-2000000 --flag-exclude 1028 --min-mapq 20
```

### Compression options

```bash
# Per-column zstd (lossless, ~1.02x BAM)
alignx convert sample.bam -o sample.axf1 --format AXF1 --axf1-compression zstd

# Lossy quality binning + zstd (~0.5x BAM)
alignx convert sample.bam -o sample.axf1 --format AXF1 \
    --axf1-compression zstd --axf1-quality-lossy illumina8

# Reference-delta SEQ encoding
alignx convert sample.bam -o sample.axf1 --format AXF1 \
    --axf1-compression zstd --reference GRCh38.fa
```

## Format Overview

AXF1 stores aligned reads in **columnar chunks** (~256 KiB each). Each chunk contains:

- 11 independent column streams with field-specific codecs:
  POS (delta-varint), FLAG (bit-pack), MAPQ (RLE), SEQ (2-bit literal or reference-delta),
  QUAL (alphabet bit-pack), CIGAR (token stream or dictionary), QNAME (front-compressed dictionary),
  TAGS (per-key streams), plus mate_reference, mate_pos, template_length
- Optional per-column zstd compression envelope
- Chunk index at end-of-file with byte offsets for selective I/O

**Selective column I/O**: pileup reads only POS+CIGAR (2 of 11 columns). Filtered view reads only FLAG+MAPQ for pre-filtering, then decodes output columns for matched records.

**Parallel decode**: memory-mapped file with thread pool (up to 8 workers) for chunk-level parallelism.

## Build from Source

**Linux (GCC 13+ / Clang 17+):**
```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

**Windows (MSVC + vcpkg):**
```powershell
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release --output-on-failure
```

**WSL (Conda):**
```bash
mamba run -n alignx-dev cmake --preset wsl-release
mamba run -n alignx-dev cmake --build --preset wsl-release
mamba run -n alignx-dev ctest --preset wsl-release --output-on-failure
```

> `VCPKG_ROOT` must be set for vcpkg manifest mode.

### Python development

```bash
pip install -e . --no-build-isolation    # editable install
pytest tests/python/ -v                  # 57 tests
```

## Dependencies

| Library | Role | Required |
|:---|:---|:---|
| HTSlib | BAM / CRAM I/O | Yes (Linux), optional (Windows) |
| zlib | BGZF decompression | Yes |
| CLI11 | CLI argument parsing | Yes |
| pybind11 | Python bindings | Python builds only |
| GoogleTest | C++ unit tests | Test builds only |
| zstd | Column compression | Optional (`ALIGNX_ENABLE_ZSTD=ON`) |

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — module map and data flow
- [`docs/roadmap.md`](docs/roadmap.md) — version milestones and deliverables
- [`docs/adr/`](docs/adr/) — architectural decision records
- [`docs/research/`](docs/research/) — codec design notes, benchmark results, paper draft

## License

[MIT](LICENSE)
