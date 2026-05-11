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

For a caller-provided real BAM:

```bash
export ALIGNX_BENCH_BAM=/path/to/sample.bam

./scripts/check_benchmark_input.sh \
  --alignx build/wsl-release/alignx \
  --input "$ALIGNX_BENCH_BAM" \
  --region chr1:1000000-2000000

./scripts/bench_region_query.sh \
  --alignx build/wsl-release/alignx \
  --input "$ALIGNX_BENCH_BAM" \
  --region chr1:1000000-2000000 \
  --warmup 1 \
  --repeats 5 \
  --output benchmarks/results/phase1_view_chr1_samtools.tsv
```

## Reporting conventions

- Raw timing results → `benchmarks/results/`
- Summary tables and plots → `benchmarks/plots/`
- Do not commit large BAM/CRAM datasets; use manifests in `configs/`.
