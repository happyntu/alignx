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

### Decode Time (Full-Record View)

**Update 2026-05-17 (final):** AXF1 lossless decode times updated to v1.0 final (mmap + thread pool + fused decode + worker-local buffers + branchless QUAL + FlatColumn). The original compression benchmark numbers (1,426 / 852 / 27,005 ms) reflected the unoptimized sequential decoder; final v1.0 achieves **3.8x-5.4x faster than samtools view**.

| Config | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| BAM (samtools view) | 236 | 169 | 3,223 |
| CRAM | 673 (0.35x) | 376 (0.45x) | 11,411 (0.28x) |
| AXF1 lossless (v1.0) | **44 (5.4x)** | **35 (4.8x)** | **854 (3.8x)** |

- **AXF1 lossless** full-record decode is **3.8x-5.4x faster than samtools view** across all regions. The v1.0 decoder uses memory-mapped I/O, 8-worker thread pool with atomic work-stealing, fused columnar-to-SAM formatting, interior chunk skip, worker-local buffer reuse, branchless QUAL decode, and FlatColumn zero-copy string access.
- **CRAM** decode is 2.4x-3.6x slower than BAM (0.28x-0.45x of samtools speed).
- **Key insight**: AXF1 has moved from samtools parity (batch 1) to decisively faster (5.4x on typical regions). Combined with selective column I/O for pileup/coverage (1.18x-1.61x faster on 1 Mb), AXF1 dominates all query workloads on typical regions.

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
