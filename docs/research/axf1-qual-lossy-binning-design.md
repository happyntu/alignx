# AXF1 QUAL Lossy Binning Design

## Context

AXF1 already has three lossless QUAL codecs (`qual_rle`, `qual_pack`, `qual_pack_compressed`).
The roadmap specifies lossy binning only under explicit lossy profile opt-in.

Lossy quality binning maps Phred quality scores from the full range (0-41+) to a small
set of representative values, reducing alphabet size and improving downstream compression.

## Illumina 8-Level Binning Scheme

| Phred Q range | Representative Q | ASCII (Q+33) |
|:---|:---|:---|
| 0-1 | 0 | `!` |
| 2-9 | 6 | `'` |
| 10-19 | 15 | `0` |
| 20-24 | 22 | `7` |
| 25-29 | 27 | `<` |
| 30-34 | 33 | `B` |
| 35-39 | 37 | `F` |
| 40+ | 40 | `I` |

This scheme is the standard Illumina 8-level binning used by bcl2fastq and DRAGEN.
Representative values are chosen at biologically meaningful thresholds (Q20/Q30 boundaries).

## Architecture Decisions

1. **Not a new codec**: Binning is pre-processing applied before codec selection.
   Binned quality values flow through existing codecs (`qual_rle`, `qual_pack`, etc.).

2. **No AXF1 metadata change**: v1.0 does not record lossy flag in the file.
   The user opts in via CLI flag and accepts that quality values are modified.

3. **Implementation location**: `encode_quality_column(records, options)` creates
   a binned copy of quality strings before passing to the no-options codec selection path.

4. **Combinable with zstd**: `--axf1-quality-lossy illumina8 --axf1-quality-compression zstd`
   applies binning first, then codec selection, then optional zstd wrapping.

## CLI

```bash
alignx convert <bam> -o <output.axf1> --format AXF1 --axf1-quality-lossy illumina8
```

Default is `none` (lossless). Only `illumina8` is implemented.

## HG002 Smoke Results

Region: chr1:1000000-2000000 (3143 records, 324 chunks)

Codec distribution with `--axf1-quality-lossy illumina8`:
- `qual_rle`: 295/324 chunks (binning reduces alphabet, making RLE effective)
- `qual_pack`: 29/324 chunks

Without lossy binning (lossless baseline):
- `qual_pack`: 324/324 chunks (full alphabet favors bit-packing over RLE)

The shift from `qual_pack` to `qual_rle` is expected: with only 8 unique quality
values, adjacent-byte runs become common, and RLE achieves better compression
than alphabet bit-packing for most chunks.

## Smoke Script

`scripts/smoke_axf1_codecs.sh` supports `--axf1-quality-lossy none|illumina8`.
When lossy is active, SHA comparison is skipped (quality values are modified);
instead, the script verifies record count parity between AXF1 and BAM outputs.
