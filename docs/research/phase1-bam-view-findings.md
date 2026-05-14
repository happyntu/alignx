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

A 30-repeat local smoke benchmark using the script-level
`--alignx-hts-threads 2` option showed:

| Tool | Runs | Median ms | P95 ms | Stdout bytes |
|---|---:|---:|---:|---:|
| alignx | 30 | 41.570 | 50.119 | 8,509,628 |
| samtools | 30 | 57.399 | 61.794 | 8,509,628 |

All runs had zero nonzero exits and identical stdout byte counts. This is about
a 1.38x median speedup for the current BAM-backed path on this smoke workload.

## Remote HG002 Chr1 Benchmark

After remote preflight passed on `missmi-server00`, a first timed Phase 1
single-thread baseline was run on 2026-05-14. This is an engineering benchmark
for the BAM-backed path; it is not a paper-grade result.

- Host: `missmi-server00`
- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr1:1000000-2000000`
- Command shape: `bench_region_query.sh --warmup 1 --repeats 5`
- Threads: single-thread baseline; no `--alignx-hts-threads`
- Raw TSV:
  `/mypool/alignx/results/phase1_view_chr1_samtools.tsv`
- Summary TSV:
  `/mypool/alignx/results/phase1_view_chr1_samtools.summary.tsv`

| Tool | Runs | Avg ms | Median ms | P95 ms | Min ms | Max ms | Stdout bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| alignx | 5 | 298.799 | 289.201 | 337.271 | 279.654 | 337.271 | 89,692,257 |
| samtools | 5 | 336.221 | 331.710 | 359.713 | 319.290 | 359.713 | 89,692,257 |

All runs had zero nonzero exits and identical stdout byte counts. The benchmark
script also diffed `alignx view` stdout against `samtools view` stdout for each
run. This run shows about a 1.15x median speedup for `alignx view` on this
specific HG002 chr1 region.

### HTSlib Threads=2 Follow-up

A follow-up run on the same host, BAM, and region used
`--alignx-hts-threads 2` for `alignx view` only. `samtools view` was unchanged.

- Command shape:
  `bench_region_query.sh --warmup 1 --repeats 5 --alignx-hts-threads 2`
- Raw TSV:
  `/mypool/alignx/results/phase1_view_chr1_samtools_threads2.tsv`
- Summary TSV:
  `/mypool/alignx/results/phase1_view_chr1_samtools_threads2.summary.tsv`

| Tool | Runs | Avg ms | Median ms | P95 ms | Min ms | Max ms | Stdout bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| alignx | 5 | 191.956 | 192.040 | 208.583 | 177.831 | 208.583 | 89,692,257 |
| samtools | 5 | 345.669 | 338.564 | 395.029 | 324.114 | 395.029 | 89,692,257 |

All runs had zero nonzero exits and identical stdout byte counts. Compared with
the single-thread baseline, `alignx` median latency improved from 289.201 ms to
192.040 ms, about 1.51x faster. Compared with the same-run `samtools` median,
the threads=2 `alignx` path was about 1.76x faster for this region.

## Decision

The BAM-backed `alignx view` path now has a useful low-risk improvement through
HTSlib threading, but the remaining dominant cost is still BAM/BGZF read and
iteration. Reaching larger speedups is likely to require the AXF/columnar path
instead of further optimizing SAM formatting.
