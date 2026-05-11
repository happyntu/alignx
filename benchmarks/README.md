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
# Build release first
cmake --preset linux-release
cmake --build --preset linux-release

# Example: region query benchmark
./scripts/bench_region_query.sh \
  --input data/toy/toy.bam \
  --region chr1:1000000-2000000 \
  --output benchmarks/results/region_query_$(date +%Y%m%d).tsv
```

## Reporting conventions

- Raw timing results → `benchmarks/results/`
- Summary tables and plots → `benchmarks/plots/`
- Do not commit large BAM/CRAM datasets; use manifests in `configs/`.
