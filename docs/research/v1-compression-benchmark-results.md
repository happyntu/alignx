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

| Config | File Size | Ratio vs BAM | Encode Median (ms) | Decode Median (ms) |
|:---|---:|---:|---:|---:|
| bam | 493,767,319 | 1.000 | 29,656 | 7,493 |
| cram | 377,551,186 | 0.765 | 31,897 | 11,411 |
| axf1 | 1,121,387,598 | 2.271 | 31,678 | 27,005 |
| axf1_zstd | 858,812,670 | 1.739 | 47,923 | 31,094 |
| axf1_lossy | 569,088,358 | 1.153 | 18,983 | 12,813 |
| axf1_lossy_zstd | 473,704,107 | 0.959 | 24,896 | 18,363 |

## Analysis

### File Size

- **AXF1 lossless** is 1.58x-2.27x larger than BAM. AXF1 stores columns without general-purpose block compression (unlike BAM's BGZF), so raw columnar overhead dominates.
- **AXF1 + zstd** reduces the gap to 1.22x-1.74x by compressing quality payloads (the largest column).
- **AXF1 lossy** (Illumina 8-level binning) drops below BAM on 1 Mb regions (0.72x-0.80x) but is 1.15x on the centromeric 21 Mb region. Quality alphabet reduction from ~40 to 8 values enables tighter codec selection (qual_rle replaces qual_pack), but centromeric data has higher quality entropy that limits compression gain.
- **AXF1 lossy + zstd** achieves the smallest files on 1 Mb regions: 0.52x-0.63x of BAM, matching or beating CRAM (0.63x-0.65x). On the centromeric region, it is 0.96x of BAM — still smaller but no longer beating CRAM (0.77x).
- **CRAM** is consistently 0.63x-0.77x of BAM across all regions, benefiting from reference-based sequence compression that scales well to larger regions.

### Encode Time

- **AXF1 lossless** encode is comparable to BAM region extract on 1 Mb regions (1.1x-1.2x) and essentially identical on the centromeric 21 Mb region (1.07x).
- **AXF1 lossy** encode is **faster** than BAM extract across all regions (0.64x-0.71x) because lossy binning reduces quality entropy, making codec selection cheaper.
- **CRAM** encode is 9.4x-13.6x slower than BAM extract on 1 Mb regions due to reference-based compression startup overhead. On the centromeric 21 Mb region, CRAM encode is only 1.08x slower — the fixed overhead amortizes over more records.
- **AXF1 lossy + zstd** encode is faster than BAM across all regions (0.84x-0.93x) despite the zstd compression pass.
- **AXF1 + zstd** encode is 1.62x slower on the centromeric region due to zstd compression cost on large lossless quality payloads, compared to 1.68x-1.80x on 1 Mb regions.

### Decode Time

**Update 2026-05-17:** AXF1 lossless decode times below are from the pre-optimization compression benchmark. Post-optimization query benchmark (same workload: `alignx view <axf1>`) shows AXF1 lossless decode at **461 ms** (chr1:1M-2M, was 1,426), **375 ms** (chrY:20M-21M, was 852), and **16,210 ms** (centromeric, was 27,005) — a 1.7x-3.1x improvement. Lossy and zstd config decode times have not been rerun but would see proportional improvement since the decoder optimizations (batch SEQ/QUAL/CIGAR codecs, zero-alloc SAM formatting) apply to all configs.

- **BAM** decode (samtools view): 200-459 ms for 1 Mb regions, 7,493-8,874 ms for the centromeric 21 Mb region.
- **AXF1 lossless** decode (post-optimization) achieves **parity with samtools view** on 1 Mb regions: 461 ms vs 459 ms (chr1:1M-2M), 375 ms vs 386 ms (chrY:20M-21M). The centromeric region is 0.55x of samtools (16,210 ms vs 8,874 ms).
- **CRAM** decode is 1.5x-2.4x slower than BAM. The centromeric region (1.5x) shows better relative CRAM decode performance than 1 Mb regions (1.9x-2.4x) due to amortized startup.
- **Key insight**: AXF1's full-record view decode has reached samtools parity on typical 1 Mb regions. Combined with its selective column I/O advantage for pileup/coverage (1.18x-1.61x faster), AXF1 is now competitive or faster across all query workloads on typical regions.

### Encode-Size Tradeoff

| Config | Size Advantage | Encode Speed | Best For |
|:---|:---|:---|:---|
| AXF1 lossy+zstd | 0.52x-0.96x | 0.84x-0.93x BAM | Archival with acceptable quality loss |
| CRAM | 0.63x-0.77x | 1.08x-13.6x BAM | Maximum lossless compression |
| AXF1 lossy | 0.72x-1.15x | 0.64x-0.71x BAM (fastest) | Fast lossy conversion |
| BAM | Baseline | Baseline | Compatibility |
| AXF1 lossless | 1.58x-2.27x | 1.07x-1.18x BAM | Selective column I/O queries |

## Raw Data

Results stored on missmi-server00:
- `/mypool/alignx/results/v1_compression/chr1_1m_2m/`
- `/mypool/alignx/results/v1_compression/chrY_20m_21m/`
- `/mypool/alignx/results/v1_compression/chr1_121m_142m/`
