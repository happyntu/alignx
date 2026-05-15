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
