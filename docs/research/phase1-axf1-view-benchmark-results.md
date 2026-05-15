# Phase 1 AXF1 View Benchmark Results

Status: engineering benchmark completed on 2026-05-15.

This note records the first timed comparison between the current AXF1
columnar region-view path and the BAM-backed `alignx view` path on HG002
chr1-style regions. It is a benchmark record, not a success-criterion claim.

## Scope

- Host: `missmi-server00`
- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- AXF1 writer policy: current hybrid default
- Regions:
  - `chr1:1000000-2000000`
  - `chr1:121000000-142000000`
  - `chrY:20000000-21000000`
- Warmup: 1
- Repeats: 5
- Raw TSV:
  `/mypool/alignx/results/axf1_view_chr1_hg002_20260515/phase1_axf1_view_chr1_hg002.tsv`
- Summary TSV:
  `/mypool/alignx/results/axf1_view_chr1_hg002_20260515/phase1_axf1_view_chr1_hg002.summary.tsv`

The run compared:

- `bam_view`: `alignx view <bam> <region>`
- `axf1_view`: `alignx view <axf1> <region>`
- `samtools`: `samtools view <bam> <region>` as a parity reference

## Results

| Region | BAM median ms | AXF1 median ms | Samtools median ms |
|---|---:|---:|---:|
| `chr1:1000000-2000000` | 412.150 | 1,979.302 | 469.233 |
| `chr1:121000000-142000000` | 6,738.874 | 39,514.777 | 9,214.946 |
| `chrY:20000000-21000000` | 324.831 | 1,460.295 | 496.481 |

All measured runs preserved stdout parity against the BAM-backed baseline.

## Interpretation

The current AXF1 region-view path is correct, but it is not yet faster than the
BAM-backed path on these tested HG002 regions. The AXF1 code path still decodes
substantially more work per query than the BAM parser baseline, so the
`v0.3` benchmark goal of outperforming full-record BAM parsing is not met yet.

That is still a useful result: it establishes a reproducible benchmark harness
and gives a concrete baseline for later AXF1 query-path optimization work.

## AXF1 profiling preflight

Status: correctness-only preflight completed on 2026-05-16.

- Binary:
  `/mypool/alignx/bin/alignx_axf1_profile_preflight_20260516`
- Host: `missmi-server00`
- AXF1 input:
  `/mypool/alignx/tmp/axf1_chunk_sizing_span_sweep_20260515/baseline_chr1_1000000-1010000.axf1`
- Region: `chr1:1000000-1010000`
- Records: `64`
- Stdout SHA-256:
  `6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`

The new `ALIGNX_PROFILE_AXF1=1` hook preserved stdout parity and emitted the
following AXF1-specific breakdown:

| chunks_selected | chunks_with_matches | records_scanned | records_matched | selective_decode_ms | full_decode_ms | format_ms |
|---:|---:|---:|---:|---:|---:|---:|
| 7 | 7 | 64 | 64 | 7.168 | 147.566 | 5.452 |

Additional profile counters from the same run:

- `open_ms=0.772`
- `ref_lookup_ms=0.001`
- `chunk_query_ms=0.021`
- `filter_ms=0.126`
- `write_ms=0.744`
- `total_ms=162.221`
- `selective_bytes_read=1,054,776`
- `full_chunk_bytes_read=1,054,776`
- `selective_payload_bytes=20,803`
- `full_payload_bytes=1,053,110`
- `stdout_bytes=1,851,159`

This is not a timed benchmark result. It is a profiling-format and correctness
preflight used to decide where the next AXF1 query-path optimization work should
focus.
