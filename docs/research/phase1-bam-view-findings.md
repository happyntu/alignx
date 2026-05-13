# Phase 1 BAM View Findings

This note records current Phase 1 observations for the BAM-backed
`alignx view <bam> <region>` path. These are engineering smoke results, not
paper-grade benchmark claims.

## Dataset and Region

- BAM: local HG001 small-region BAM from the biotools benchmark workspace.
- Region: `chr20:10000000-10010000`.
- Output size: 20,092 SAM records, 8,509,628 bytes.
- Correctness baseline: `alignx view` stdout matched `samtools view` byte-for-byte
  in all smoke/profiling runs.

## Profiling Observations

With single-threaded HTSlib I/O, `ALIGNX_PROFILE_VIEW=1` showed that the largest
cost is BAM read/iteration, not SAM formatting:

| Area | Observation |
|---|---|
| open/header/index/fetch | Small relative to total time |
| BAM read / iterator | Dominant cost |
| `sam_format1` formatting | Small, roughly a few milliseconds for this region |
| stdout write | Noticeable, but below BAM read cost |

This means low-risk Phase 1 optimization should focus on HTSlib/BGZF read
settings before attempting custom SAM formatting.

## HTSlib Threading

`ALIGNX_HTS_THREADS` and `alignx view --hts-threads <n>` configure HTSlib worker
threads via `hts_set_threads`.

A single-run profiling sweep on the same smoke region showed:

| HTS threads | read_ms | total_ms |
|---:|---:|---:|
| 0 | 58.23 | 79.37 |
| 1 | 26.11 | 45.53 |
| 2 | 16.08 | 32.34 |
| 4 | 16.71 | 32.11 |

For this region, two HTSlib worker threads were enough; four threads did not
materially improve the result.

## Current Smoke Benchmark

A 30-repeat local smoke benchmark with `ALIGNX_HTS_THREADS=2` showed:

| Tool | Runs | Median ms | P95 ms | Stdout bytes |
|---|---:|---:|---:|---:|
| alignx | 30 | 41.125 | 43.686 | 8,509,628 |
| samtools | 30 | 57.936 | 66.962 | 8,509,628 |

This is about a 1.4x median speedup for the current BAM-backed path on this
smoke workload.

## Decision

The BAM-backed `alignx view` path now has a useful low-risk improvement through
HTSlib threading, but the remaining dominant cost is still BAM/BGZF read and
iteration. Reaching larger speedups is likely to require the AXF/columnar path
instead of further optimizing SAM formatting.
