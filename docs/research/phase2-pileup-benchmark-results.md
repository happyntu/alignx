# Phase 2 Pileup Benchmark Results

## Date

2026-05-16

## Setup

- **Host**: missmi-server00 (ZFS mypool)
- **BAM**: HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam
- **AXF1**: converted with `alignx convert --format AXF1 --region` (hybrid chunk sizing default: 256 KiB target / 512 KiB max / 4096 records / 1,000,000 bp span)
- **Tools**: samtools depth -a, alignx pileup (BAM path), alignx pileup (AXF1 selective column path)
- **Protocol**: 3 warmup + 10 timed repeats; correctness preflight (SHA match) before timing
- **Benchmark script**: `scripts/bench_pileup.sh`

## Correctness

All three regions passed stdout SHA parity check:
- `samtools depth -a` == `alignx pileup <bam>` == `alignx pileup <axf1>` for each region.

## Timing Results (median wall time, ms)

| Region | samtools depth | alignx pileup BAM | alignx pileup AXF1 | AXF1 vs samtools | AXF1 vs BAM |
|:---|---:|---:|---:|:---|:---|
| chr1:1,000,000-2,000,000 | 277.7 | 391.2 | **188.8** | **1.47x faster** | **2.07x faster** |
| chr1:121,000,000-142,000,000 | 4,828.3 | 7,586.1 | **5,910.7** | 0.82x (slower) | **1.28x faster** |
| chrY:20,000,000-21,000,000 | 221.6 | 331.9 | **199.3** | **1.11x faster** | **1.67x faster** |

## Raw Summary Data

### chr1:1,000,000-2,000,000

```
tool                runs  avg_ms    median_ms  p95_ms    min_ms    max_ms
samtools_depth      10    277.234   277.698    282.298   272.547   282.298
alignx_pileup_bam   10    395.160   391.169    421.042   383.478   421.042
alignx_pileup_axf1  10    191.403   188.829    203.057   185.829   203.057
```

### chr1:121,000,000-142,000,000

```
tool                runs  avg_ms     median_ms   p95_ms     min_ms     max_ms
samtools_depth      10    4909.650   4828.255    5287.808   4765.417   5287.808
alignx_pileup_bam   10    7672.745   7586.072    8446.948   7515.684   8446.948
alignx_pileup_axf1  10    5946.047   5910.703    6343.094   5820.904   6343.094
```

### chrY:20,000,000-21,000,000

```
tool                runs  avg_ms    median_ms  p95_ms    min_ms    max_ms
samtools_depth      10    222.942   221.613    232.593   213.926   232.593
alignx_pileup_bam   10    334.872   331.876    348.694   327.717   348.694
alignx_pileup_axf1  10    200.289   199.252    220.883   190.362   220.883
```

## Analysis

1. **AXF1 selective column I/O is consistently faster than BAM full-record parse** across all three regions (1.28x–2.07x), confirming the selective-column advantage for pileup workloads that only need POS+CIGAR.

2. **AXF1 beats samtools depth on 1 Mb regions** (chr1:1M-2M at 1.47x, chrY:20M-21M at 1.11x), where the column-skip savings dominate.

3. **AXF1 is slower than samtools depth on the 21 Mb centromeric region** (0.82x). This region has sparse coverage (many zero-depth positions) and a large AXF1 file (1.1 GB). Likely bottlenecks:
   - Large chunk count increases chunk-index seek overhead
   - Output formatting for 21M lines dominates both tools' wall time
   - samtools depth benefits from highly optimized C pileup engine with zero-overhead field access

4. **alignx BAM path is consistently slower than samtools depth** (1.41x–1.57x slower), reflecting HTSlib record-by-record overhead plus SAM-field extraction cost in the current alignx BAM pipeline.

## Conclusion

The AXF1 selective column path delivers the expected pileup speedup on typical 1 Mb genomic windows. The centromeric region shows that very large sparse regions still favor samtools depth, likely due to output formatting cost rather than I/O. Further optimization should target:
- Reducing chunk-index iteration overhead for large AXF1 files
- Skipping zero-depth output lines (matching `samtools depth` default behavior) to reduce formatting cost
- Profiling the output-write bottleneck separately from decode cost
