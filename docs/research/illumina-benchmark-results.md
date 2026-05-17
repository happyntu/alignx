# Illumina Short-Read Benchmark Results

## Date

2026-05-17

## Setup

- **Host**: missmi-server00 (ZFS `/mypool` storage)
- **BAM**: HG002 NIST Illumina 2x250bp paired-end, novoalign, GRCh38 (~300x coverage)
- **Source**: GIAB FTP `NIST_Illumina_2x250bps/novoalign_bams/HG002.GRCh38.2x250.bam`
- **Tools**: alignx v1.0 (PNEXT fix applied), samtools 1.23.1 (hg002sv conda env)
- **Protocol**: 1 warmup + 5 timed repeats per tool per region
- **Correctness**: SHA-256 parity verified between `samtools view -F 4` and `alignx view` (AXF1 stores mapped records only; 2.5% of Illumina records are unmapped mates positioned at mapped mate location)

## Regions

| Region | Span | Mapped Records | Total Records (incl. unmapped) |
|:---|:---|---:|---:|
| chr1:1,000,000-2,000,000 | 1 Mb | 271,381 | 278,355 |
| chrY:20,000,000-21,000,000 | 1 Mb | 120,493 | 122,972 |
| chr1:121,000,000-142,000,000 | 21 Mb | 4,068,017 | 4,554,488 |

## File Sizes

| Region | BAM | AXF1 Lossless | Ratio |
|:---|---:|---:|---:|
| chr1:1M-2M | 45 MB | 121 MB | 2.69x |
| chrY:20M-21M | 18 MB | 54 MB | 3.00x |
| chr1:121M-142M | 685 MB | 1,800 MB | 2.63x |

AXF1 lossless is 2.6x-3.0x larger than BAM for Illumina data (vs 1.6x-2.3x for PacBio). The larger ratio reflects Illumina's higher record density: 300x short-read coverage produces ~89x more records than 10x PacBio long-read coverage in the same region, increasing per-record metadata overhead relative to BAM's BGZF block compression.

## View Benchmark

| Region | samtools view -F4 (ms) | alignx view AXF1 (ms) | Speedup |
|:---|---:|---:|---:|
| chr1:1M-2M | 438 | 137 | **3.2x** |
| chrY:20M-21M | 190 | 64 | **3.0x** |
| chr1:121M-142M | 6,339 | 1,642 | **3.9x** |

AXF1 view achieves **3.0x-3.9x faster** than samtools on Illumina data. The speedup is slightly lower than PacBio (3.8x-5.4x) because:
1. Short-read SEQ/QUAL columns are smaller per record (250bp vs 15-20kb), reducing the relative benefit of columnar decode
2. Illumina data has simpler TAG structures (fewer auxiliary tags per record), so the TAG column codec overhead is proportionally larger
3. The centromeric region (3.9x) benefits more from parallel chunk decode due to higher chunk count (>1000 chunks at 4096 records/chunk max)

## Pileup Benchmark

| Region | samtools depth (ms) | alignx pileup AXF1 (ms) | Ratio |
|:---|---:|---:|---:|
| chr1:1M-2M | 372 | 305 | 1.2x faster |
| chrY:20M-21M | 230 | 249 | 0.92x (slower) |
| chr1:121M-142M | 4,955 | 5,337 | 0.93x (slower) |

AXF1 pileup shows minimal advantage on Illumina data compared to PacBio (where it achieves 1.1x-1.4x on 1 Mb regions). The reasons:
1. **Simple CIGARs**: Illumina 2x250bp reads have predominantly simple CIGAR strings (e.g., `250M`, `245M5S`), so CIGAR parsing is not a bottleneck that selective column I/O can avoid
2. **High record density**: 4096 records per chunk × many chunks means the per-record coverage accumulation dominates, not I/O
3. **Small per-record gain**: Skipping SEQ+QUAL saves only ~500 bytes/record for Illumina (vs ~15-20 KB for PacBio), a smaller fraction of total work

## Comparison: PacBio vs Illumina Speedup

| Workload | PacBio 10x (15-20kb) | Illumina 300x (2x250bp) |
|:---|:---|:---|
| View (1 Mb) | 4.8x-5.4x | 3.0x-3.2x |
| View (21 Mb centromeric) | 3.8x | 3.9x |
| Pileup (1 Mb) | 1.1x-1.4x | 0.92x-1.2x |
| Pileup (21 Mb centromeric) | 0.90x | 0.93x |

**Key insight**: AXF1's columnar decode advantage is data-type dependent. Long-read data benefits more from selective column I/O (large per-record payloads to skip). Short-read data benefits primarily from the parallel fused decode path for view, not from selective I/O for pileup.

## Raw Data

Results stored on missmi-server00:
- `/mypool/alignx/results/illumina_benchmark/`
- `/mypool/alignx/logs/bench_illumina_centromeric.log`
