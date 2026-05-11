# Data

This directory holds small committed fixtures and local-only benchmark data.

## Layout

```text
data/
  toy/        Small deterministic toy fixtures that may be committed
```

## Policy

- Do commit small deterministic fixtures needed by tests or examples.
- Do not commit large BAM, CRAM, SAM, genome, or benchmark datasets.
- Put large downloaded or generated datasets under ignored paths such as
  `data/raw/` or document them with manifests in `benchmarks/configs/`.
- Keep benchmark outputs in `benchmarks/results/`, not in `data/`.

Phase 0 still needs toy BAM fixtures for `tests/toy_data/` or `data/toy/`.
