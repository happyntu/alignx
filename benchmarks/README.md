# Benchmarks

## Directory layout

| Path | Purpose |
|---|---|
| `configs/` | Benchmark run definitions and dataset manifests |
| `results/` | Raw benchmark outputs (gitignored for large files) |
| `plots/` | Summary tables and figures from results |

## Phase targets

| Phase | Benchmark | Comparison |
|---|---|---|
| v0.1 | BAM region query latency (`chr1:1M-2M`) | samtools view |
| v0.2 | Coverage calculation (`chr1`) | samtools depth, mosdepth |
| v0.3 | AXF columnar read (FLAG/MAPQ/POS only) | BAM full-record parse |
| v0.4 | BAM→AXF→BAM round-trip fidelity | md5sum / diff |
| v1.0 | Full benchmark suite | samtools, htslib, CRAM |

## Running benchmarks

Do not start benchmark timing, repeated runs, or profiling until the user has
explicitly confirmed that the machine is available. `check_benchmark_input.sh`
is a preflight correctness and environment check; `bench_region_query.sh` is a
benchmark because it runs timed repeats.

```bash
# Build first
mamba run -n alignx-dev cmake --preset wsl-release
mamba run -n alignx-dev cmake --build --preset wsl-release

# Example: toy region query benchmark
./scripts/check_benchmark_input.sh \
  --samtools samtools \
  --alignx build/wsl-release/alignx \
  --input tests/toy_data/toy_alignment.sorted.bam \
  --region chrToy:1-250

./scripts/bench_region_query.sh \
  --alignx build/wsl-release/alignx \
  --samtools samtools \
  --input tests/toy_data/toy_alignment.sorted.bam \
  --region chrToy:1-250 \
  --warmup 1 \
  --repeats 5 \
  --output benchmarks/results/phase1_view_chrtoy_samtools.tsv
```

Both scripts verify that `alignx index` can materialize a projected `.axf.idx`
from the BAM sidecar `.bai` / `.csi` before timing `alignx view`. The generated
index is written to a temporary directory unless `--axf-index-output` is passed.
Each benchmark run writes the raw timing TSV and a summary TSV with average,
median, p95, min/max, max-over-median, and outlier counts. By default the
summary path is `<output without .tsv>.summary.tsv`; override it with
`--summary-output`.

For a caller-provided real BAM:

```bash
export ALIGNX_BENCH_BAM=/path/to/sample.bam
# Optional: let HTSlib use worker threads for BAM/CRAM I/O.
# Leave unset or set to 0 for the single-threaded baseline.
export ALIGNX_HTS_THREADS=2

./scripts/check_benchmark_input.sh \
  --alignx build/wsl-release/alignx \
  --input "$ALIGNX_BENCH_BAM" \
  --region chr1:1000000-2000000

./scripts/bench_region_query.sh \
  --alignx build/wsl-release/alignx \
  --input "$ALIGNX_BENCH_BAM" \
  --region chr1:1000000-2000000 \
  --alignx-hts-threads 2 \
  --warmup 1 \
  --repeats 5 \
  --output benchmarks/results/phase1_view_chr1_samtools.tsv
```

## Preflight Workflow

Use this checklist before requesting or starting a benchmark run:

- Build a release binary first: `build/wsl-release/alignx` for local WSL, or
  `/mypool/alignx/bin/alignx` for `missmi-server00`.
- Confirm the input BAM is indexed with a nearby `.bai` or `.csi`.
- Run `scripts/check_benchmark_input.sh` for the exact BAM and region. This
  checks `samtools quickcheck`, header readability, region record count, region
  view success, and `alignx index` preflight.
- Confirm stdout parity is enforced by `scripts/bench_region_query.sh`; the
  script fails if `alignx view` differs from `samtools view`.
- Choose output paths before timing. Raw TSV and summary TSV should go under
  `benchmarks/results/` for local toy/smoke runs or `/mypool/alignx/results/`
  for remote large-data runs.
- Ask the user before running timed repeats, profiling, or real-data benchmark
  commands.

## Remote Large-Data Workflow

For HG002 or other large BAM/CRAM datasets, prefer `missmi-server00` and the
large `/mypool` filesystem instead of local WSL storage. The local Codex session
should orchestrate over SSH.

Recommended remote paths:

| Purpose | Path |
|---|---|
| alignx binary | `/mypool/alignx/bin/alignx` |
| benchmark scripts snapshot | `/mypool/alignx/bin/check_benchmark_input.sh`, `/mypool/alignx/bin/bench_region_query.sh` |
| samtools binary | `/home/happyntu/miniconda3/envs/hg002sv/bin/samtools` |
| HTSlib runtime libs | `/home/happyntu/miniconda3/envs/hg002sv/lib` |
| large input data | `/mypool/alignx/data/` or existing `/mypool/biotools-benchmark-data/` |
| references | `/mypool/alignx/refs/` |
| raw and summary TSV | `/mypool/alignx/results/` |
| logs | `/mypool/alignx/logs/` |
| scratch work | `/mypool/alignx/tmp/` |

Known HG002 data used by previous correctness smokes:

```text
/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam
```

Remote preflight example, still not a benchmark:

```bash
LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib \
/mypool/alignx/bin/check_benchmark_input.sh \
  --alignx /mypool/alignx/bin/alignx \
  --samtools /home/happyntu/miniconda3/envs/hg002sv/bin/samtools \
  --input /mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam \
  --region chr1:1000000-2000000
```

Last verified remote HG002 preflight:

- Date: 2026-05-14
- Host: `missmi-server00`
- Region: `chr1:1000000-2000000`
- `samtools view -c`: 3143 records
- `alignx index` preflight: passed
- Projected AXF index metadata: 195 references, 1,293,079 intervals
- No timed repeats, profiling, or benchmark TSV were produced.

Remote benchmark command shape, run only after explicit confirmation:

```bash
LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib \
/mypool/alignx/bin/bench_region_query.sh \
  --alignx /mypool/alignx/bin/alignx \
  --samtools /home/happyntu/miniconda3/envs/hg002sv/bin/samtools \
  --input /mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam \
  --region chr1:1000000-2000000 \
  --warmup 1 \
  --repeats 5 \
  --output /mypool/alignx/results/phase1_view_chr1_samtools.tsv
```

## Reporting conventions

- Raw timing results → `benchmarks/results/`
- Summary tables and plots → `benchmarks/plots/`
- Do not commit large BAM/CRAM datasets; use manifests in `configs/`.
