# v1.0 Compression Benchmark Results

## Date

2026-05-16

## Setup

- **Host**: missmi-server00 (ZFS `/mypool` storage)
- **BAM**: HG002 PacBio SequelII 15kb/20kb merged, GRCh38, haplotag 10x
- **Reference**: GCA_000001405.15_GRCh38_no_alt_analysis_set.fasta
- **Tools**: alignx (v1.0 codec stack, ZSTD enabled), samtools (hg002sv conda env)
- **Script**: `scripts/bench_compression.sh`
- **Protocol**: 1 warmup + 5 timed repeats per config per region
- **Correctness**: lossless configs verified by SHA-256 parity with BAM baseline; lossy configs verified by record count parity; CRAM verified by record count (CRAM recomputes MD/NM tags)

## Regions

| Region | Span | Records | Character |
|:---|:---|:---|:---|
| chr1:1,000,000-2,000,000 | 1 Mb | 3,143 | Typical euchromatic |
| chrY:20,000,000-21,000,000 | 1 Mb | 2,034 | Y chromosome |
| chr1:121,000,000-142,000,000 | 21 Mb | 58,168 | Centromeric (pending) |

## Configs

| Config ID | Description |
|:---|:---|
| `bam` | `samtools view -b -h` region extract (BGZF compressed) |
| `cram` | `samtools view -C -h --reference` region extract (CRAM v3) |
| `axf1` | AXF1 lossless, no zstd |
| `axf1_zstd` | AXF1 lossless + zstd quality compression |
| `axf1_lossy` | AXF1 + Illumina 8-level quality binning |
| `axf1_lossy_zstd` | AXF1 + lossy binning + zstd quality compression |

## Results

### chr1:1,000,000-2,000,000 (3,143 records)

| Config | File Size | Ratio vs BAM | Encode Median (ms) | Decode Median (ms) |
|:---|---:|---:|---:|---:|
| bam | 31,982,136 | 1.000 | 1,503 | 284 |
| cram | 20,761,364 | 0.649 | 14,072 | 673 |
| axf1 | 50,498,344 | 1.579 | 1,768 | 1,426 |
| axf1_zstd | 38,922,272 | 1.217 | 2,698 | 1,518 |
| axf1_lossy | 23,120,174 | 0.723 | 1,074 | 544 |
| axf1_lossy_zstd | 16,682,468 | 0.522 | 1,404 | 801 |

### chrY:20,000,000-21,000,000 (2,034 records)

| Config | File Size | Ratio vs BAM | Encode Median (ms) | Decode Median (ms) |
|:---|---:|---:|---:|---:|
| bam | 18,238,976 | 1.000 | 988 | 200 |
| cram | 11,553,406 | 0.633 | 13,478 | 376 |
| axf1 | 33,509,398 | 1.837 | 1,085 | 852 |
| axf1_zstd | 24,238,110 | 1.329 | 1,661 | 987 |
| axf1_lossy | 14,521,862 | 0.796 | 694 | 382 |
| axf1_lossy_zstd | 11,517,292 | 0.632 | 884 | 580 |

### chr1:121,000,000-142,000,000 (58,168 records)

*Pending — benchmark running on missmi-server00 via nohup.*

Preliminary file sizes from Phase 2:

| Config | File Size | Ratio vs BAM |
|:---|---:|---:|
| bam | 493,767,321 | 1.000 |
| cram | 377,551,189 | 0.765 |
| axf1 | 1,121,387,598 | 2.271 |
| axf1_zstd | 858,812,670 | 1.739 |
| axf1_lossy | 569,088,358 | 1.153 |
| axf1_lossy_zstd | 473,704,107 | 0.959 |

## Analysis

### File Size

- **AXF1 lossless** is 1.58x-2.27x larger than BAM. AXF1 stores columns without general-purpose block compression (unlike BAM's BGZF), so raw columnar overhead dominates.
- **AXF1 + zstd** reduces the gap to 1.22x-1.74x by compressing quality payloads (the largest column).
- **AXF1 lossy** (Illumina 8-level binning) drops below BAM on small regions (0.72x-0.80x). Quality alphabet reduction from ~40 to 8 values enables much tighter codec selection (qual_rle replaces qual_pack on most chunks).
- **AXF1 lossy + zstd** achieves the smallest files: 0.52x-0.63x of BAM, matching or beating CRAM (0.63x-0.65x) on the two completed regions.
- **CRAM** is consistently 0.63x-0.77x of BAM, benefiting from reference-based sequence compression.

### Encode Time

- **AXF1 lossless** encode is comparable to BAM region extract (1.1x-1.2x slower).
- **AXF1 lossy** encode is **faster** than BAM extract (0.70x-0.71x) because lossy binning reduces quality entropy, making codec selection cheaper.
- **CRAM** encode is 9.4x-13.6x slower than BAM extract due to reference-based compression overhead.
- **AXF1 lossy + zstd** encode is still faster than BAM (0.89x-0.93x) despite the zstd compression pass.

### Decode Time

- **BAM** decode (samtools view) is the fastest at 200-284 ms for 1 Mb regions. BAM benefits from highly optimized HTSlib parsing.
- **AXF1 lossless** decode is 4.3x-5.0x slower than BAM. The AXF1 view path decodes all columns and formats SAM strings, which is not yet optimized.
- **AXF1 lossy** decode is 1.9x slower than BAM — significantly better than lossless because the reduced quality alphabet leads to faster RLE decoding.
- **CRAM** decode is 1.9x-2.4x slower than BAM due to reference decompression overhead.
- **Key insight**: AXF1's selective column I/O advantage (demonstrated in Phase 1-2 coverage/pileup benchmarks at 1.5x-3x faster) does not appear in the `view` workload because view must decode and format all columns. AXF1's decode advantage is in column-selective workloads.

### Encode-Size Tradeoff

| Config | Size Advantage | Encode Speed | Best For |
|:---|:---|:---|:---|
| AXF1 lossy+zstd | Smallest (0.52x-0.63x) | Faster than BAM | Archival with acceptable quality loss |
| CRAM | Small (0.63x-0.77x) | 10x slower than BAM | Maximum lossless compression |
| AXF1 lossy | Smaller than BAM (0.72x-0.80x) | Fastest encode | Fast lossy conversion |
| BAM | Baseline | Baseline | Compatibility |
| AXF1 lossless | Larger (1.58x-2.27x) | ~1.1x BAM | Selective column I/O queries |

## Raw Data

Results stored on missmi-server00:
- `/mypool/alignx/results/v1_compression/chr1_1m_2m/`
- `/mypool/alignx/results/v1_compression/chrY_20m_21m/`
- `/mypool/alignx/results/v1_compression/chr1_121m_142m/` (pending)
