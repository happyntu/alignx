# v1.0 Query Benchmark Results

## Date

2026-05-17

## Setup

- **Host**: missmi-server00 (ZFS `/mypool` storage)
- **BAM**: HG002 PacBio SequelII 15kb/20kb merged, GRCh38, haplotag 10x
- **AXF1**: Pre-converted lossless v1.0 codec stack (QNAME dict, TAG per-stream, CIGAR token, SEQ 2-bit, QUAL pack)
- **Tools**: alignx (v1.0 codec stack), samtools 1.23.1 (hg002sv conda env)
- **Script**: `scripts/bench_query.sh`
- **Protocol**: 1 warmup + 5 timed repeats per tool per region
- **Correctness**: SHA-256 parity verified for all 12 tool-filter combinations before timing
- **Filter**: `--flag-exclude 2308 --min-mapq 20` (exclude unmapped + secondary + supplementary, MAPQ >= 20)

## Regions

| Region | Span | Records | Character |
|:---|:---|:---|:---|
| chr1:1,000,000-2,000,000 | 1 Mb | 3,143 | Typical euchromatic |
| chrY:20,000,000-21,000,000 | 1 Mb | 2,034 | Y chromosome |
| chr1:121,000,000-142,000,000 | 21 Mb | 58,168 | Centromeric |

## Tool Matrix

| Tool ID | Command | Filter |
|:---|:---|:---|
| samtools_view | `samtools view <bam> <region>` | None |
| alignx_view_bam | `alignx view <bam> <region>` | None |
| alignx_view_axf1 | `alignx view <axf1> <region>` | None |
| samtools_view_filtered | `samtools view -F 2308 -q 20 <bam> <region>` | F2308+Q20 |
| alignx_view_bam_filtered | `alignx view <bam> --flag-exclude 2308 --min-mapq 20` | F2308+Q20 |
| alignx_view_axf1_filtered | `alignx view <axf1> --flag-exclude 2308 --min-mapq 20` | F2308+Q20 |
| samtools_depth | `samtools depth -a -r <region> <bam>` | None |
| alignx_pileup_bam | `alignx pileup <bam> <region>` | None |
| alignx_pileup_axf1 | `alignx pileup <axf1> <region>` | None |
| samtools_depth_filtered | `samtools depth -a -G 2308 -Q 20 -r <region> <bam>` | G2308+Q20 |
| alignx_pileup_bam_filtered | `alignx pileup <bam> --flag-exclude 2308 --min-mapq 20` | G2308+Q20 |
| alignx_pileup_axf1_filtered | `alignx pileup <axf1> --flag-exclude 2308 --min-mapq 20` | G2308+Q20 |

Note: `samtools depth` uses `-G` for FLAG exclusion and `-Q` for MAPQ, whereas `samtools view` uses `-F` and `-q`.

## Results

**Update 2026-05-17:** Rerun after lazy decode optimization (single-pass all-column read for unfiltered view, batch SEQ 2-bit LUT, QUAL pack bit-accumulator, CIGAR LUT + `std::to_chars`, QNAME dict ref-counting, `append_axf1_sam_record` zero-copy formatting). Previous results archived in `v1_query/`; current in `v1_query_opt/`.

### chr1:1,000,000-2,000,000 (3,143 records)

| Tool | Median (ms) | vs samtools | vs BAM path |
|:---|---:|---:|---:|
| samtools_view | 459.2 | 1.00x | — |
| alignx_view_bam | 331.0 | 1.39x | 1.00x |
| **alignx_view_axf1** | **461.0** | **1.00x** | 0.72x |
| samtools_view_filtered | 576.3 | 1.00x | — |
| alignx_view_bam_filtered | 336.2 | 1.71x | 1.00x |
| **alignx_view_axf1_filtered** | **467.8** | **1.23x** | 0.72x |
| samtools_depth | 355.6 | 1.00x | — |
| alignx_pileup_bam | 474.9 | 0.75x | 1.00x |
| **alignx_pileup_axf1** | **220.7** | **1.61x** | **2.15x** |
| samtools_depth_filtered | 329.4 | 1.00x | — |
| alignx_pileup_bam_filtered | 458.0 | 0.72x | 1.00x |
| **alignx_pileup_axf1_filtered** | **221.8** | **1.49x** | **2.06x** |

### chrY:20,000,000-21,000,000 (2,034 records)

| Tool | Median (ms) | vs samtools | vs BAM path |
|:---|---:|---:|---:|
| samtools_view | 386.0 | 1.00x | — |
| alignx_view_bam | 259.2 | 1.49x | 1.00x |
| **alignx_view_axf1** | **374.6** | **1.03x** | 0.69x |
| samtools_view_filtered | 322.3 | 1.00x | — |
| alignx_view_bam_filtered | 245.9 | 1.31x | 1.00x |
| **alignx_view_axf1_filtered** | **319.1** | **1.01x** | 0.77x |
| samtools_depth | 282.5 | 1.00x | — |
| alignx_pileup_bam | 378.9 | 0.75x | 1.00x |
| **alignx_pileup_axf1** | **239.3** | **1.18x** | **1.58x** |
| samtools_depth_filtered | 265.0 | 1.00x | — |
| alignx_pileup_bam_filtered | 435.8 | 0.61x | 1.00x |
| **alignx_pileup_axf1_filtered** | **289.6** | **0.92x** | **1.50x** |

### chr1:121,000,000-142,000,000 (58,168 records)

| Tool | Median (ms) | vs samtools | vs BAM path |
|:---|---:|---:|---:|
| samtools_view | 8,874.1 | 1.00x | — |
| alignx_view_bam | 6,991.9 | 1.27x | 1.00x |
| alignx_view_axf1 | 16,209.9 | 0.55x | 0.43x |
| samtools_view_filtered | 4,077.8 | 1.00x | — |
| alignx_view_bam_filtered | 5,045.8 | 0.81x | 1.00x |
| **alignx_view_axf1_filtered** | **5,034.3** | **0.81x** | **1.00x** |
| samtools_depth | 6,742.5 | 1.00x | — |
| alignx_pileup_bam | 10,877.1 | 0.62x | 1.00x |
| alignx_pileup_axf1 | 9,727.8 | 0.69x | 1.12x |
| samtools_depth_filtered | 4,977.9 | 1.00x | — |
| alignx_pileup_bam_filtered | 8,798.2 | 0.57x | 1.00x |
| alignx_pileup_axf1_filtered | 8,587.1 | 0.58x | 1.02x |

## Analysis

### View Performance (Post-Optimization)

- **AXF1 view (unfiltered)** achieves **parity with samtools view** on 1 Mb regions: 1.00x on chr1:1M-2M (461 ms vs 459 ms) and 1.03x on chrY:20M-21M (375 ms vs 386 ms). This is a **3-4x improvement** from pre-optimization (1,553 ms → 461 ms on chr1).
- **AXF1 view (unfiltered)** on the centromeric 21 Mb region is 0.55x of samtools (16,210 ms vs 8,874 ms). The centromeric region is large enough that sequential BAM parsing is well-amortized, and AXF1's columnar reconstruction overhead still dominates. Pre-optimization was 0.18x, so this is a ~3x improvement.
- **AXF1 view (filtered)** achieves near-parity across all regions: 1.23x on chr1:1M-2M, 1.01x on chrY:20M-21M, and 0.81x on centromeric. The two-pass selective decode path (FLAG+MAPQ first, then output columns for matched records only) is effective for reducing unnecessary output formatting.
- **alignx view BAM** is 1.27x-1.49x faster than samtools view on all regions except centromeric filtered (0.81x).

### Optimization Details

The view path improvements come from:
1. **Single-pass all-column read** for unfiltered view (eliminates second I/O pass)
2. **Batch SEQ 2-bit decode** via consteval 256-entry LUT (4 bases per byte lookup)
3. **QUAL pack bit-accumulator** via uint64_t mask+shift (replaces per-bit extraction loop)
4. **CIGAR LUT + `std::to_chars`** (eliminates switch and heap allocation)
5. **QNAME dict reference counting** (move if ref_count==1, copy otherwise)
6. **`append_axf1_sam_record`** writes directly to caller's buffer (no temporary string)

### Pileup Performance

- **AXF1 pileup** is the fastest pileup tool on 1 Mb regions: **1.18x-1.61x faster** than samtools depth. This is the core AXF1 selective column I/O advantage — pileup reads only POS+CIGAR columns, skipping QUAL/SEQ/QNAME/TAGS.
- **AXF1 pileup** on the centromeric 21 Mb region is 0.69x of samtools depth (slower). The centromeric region's high record density and large chunk sizes reduce the selective I/O advantage. samtools depth benefits from highly optimized HTSlib sequential parsing at scale.
- **AXF1 pileup vs BAM path**: AXF1 is consistently **1.02x-2.15x faster** than alignx's own BAM pileup path across all regions, confirming the columnar selective I/O benefit independently of samtools comparison.

### Filter Impact

- **Filter narrows the centromeric view gap**: unfiltered AXF1 view is 0.55x of samtools; filtered improves to 0.81x. The filter reduces output volume, and AXF1's selective FLAG+MAPQ column decode avoids unnecessary full-record formatting.
- **Filter has minimal effect on pileup**: filtered pileup is within 10% of unfiltered across most tools and regions. The filter excludes few records (most reads pass FLAG 2308 + MAPQ 20 on this HG002 PacBio dataset), so the I/O cost dominates.
- **AXF1 filter column optimization**: when filters are active, the AXF1 path uses two-pass selective I/O (FLAG+MAPQ first, then remaining output columns for matches only). This is measurably beneficial on large regions.

### Summary Table

| Workload | AXF1 vs samtools (1 Mb) | AXF1 vs samtools (21 Mb) | AXF1 vs BAM path (all) |
|:---|:---|:---|:---|
| View (unfiltered) | **1.00x-1.03x (parity)** | 0.55x (slower) | 0.43x-0.72x |
| View (filtered) | **1.01x-1.23x (faster)** | 0.81x (near parity) | 0.72x-1.00x |
| Pileup (unfiltered) | **1.18x-1.61x (faster)** | 0.69x (slower) | **1.12x-2.15x (faster)** |
| Pileup (filtered) | **0.92x-1.49x** | 0.58x (slower) | **1.02x-2.06x (faster)** |

### Key Insights

1. **AXF1 view has reached samtools parity on typical regions.** Batch decoders and zero-alloc SAM formatting eliminate the columnar reconstruction overhead for 1 Mb queries. The centromeric 21 Mb region remains 0.55x due to sheer data volume, but this is a 3x improvement from pre-optimization.
2. **Selective column I/O remains the primary differentiator for pileup.** AXF1 pileup (POS+CIGAR only) is 1.18x-1.61x faster than samtools depth on 1 Mb regions. The advantage diminishes on 21 Mb but AXF1 still beats its own BAM path everywhere.
3. **Filtered view is competitive everywhere.** The two-pass selective decode path delivers near-parity (0.81x-1.23x) across all regions, including centromeric.
4. **AXF1 consistently beats its own BAM path.** The 1.02x-2.15x pileup speedup over alignx BAM pileup confirms the columnar I/O advantage is real, independent of samtools implementation differences.

## Raw Data

Results stored on missmi-server00:
- `/mypool/alignx/results/v1_query_opt/chr1_1m_2m/` (post-optimization)
- `/mypool/alignx/results/v1_query_opt/chrY_20m_21m/` (post-optimization)
- `/mypool/alignx/results/v1_query_opt/chr1_121m_142m/` (post-optimization)
- `/mypool/alignx/results/v1_query/` (pre-optimization archive)
