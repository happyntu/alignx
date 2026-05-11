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
mamba run -n alignx-dev cmake --build --preset wsl-debug

# Example: toy region query benchmark
./scripts/bench_region_query.sh \
  --alignx build/wsl-debug/alignx \
  --samtools samtools \
  --input tests/toy_data/toy_alignment.sorted.bam \
  --region chrToy:1-250 \
  --output benchmarks/results/phase1_view_chrtoy_samtools.tsv
```

## Reporting conventions

- Raw timing results → `benchmarks/results/`
- Summary tables and plots → `benchmarks/plots/`
- Do not commit large BAM/CRAM datasets; use manifests in `configs/`.
