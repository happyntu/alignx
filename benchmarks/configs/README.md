# Benchmark Configs

Benchmark config files describe reproducible benchmark runs and the datasets
they require.

## Naming

Use phase-oriented names so results remain easy to compare across roadmap
milestones:

```text
phase<phase>_<target>_<description>.yaml
```

Examples:

- `phase1_view_chr1_samtools.yaml`
- `phase2_depth_chr1_mosdepth.yaml`
- `phase3_axf_columnar_parse.yaml`

## Expected Content

Each config should identify:

- input dataset or manifest path
- query region or workload definition
- alignx binary or build preset
- comparison tool, such as `samtools` or `mosdepth`
- metrics to collect, such as latency, throughput, and peak RSS
- output path under `benchmarks/results/`

Large BAM/CRAM datasets must not be committed. Put reproducible dataset
instructions or manifests here instead.
