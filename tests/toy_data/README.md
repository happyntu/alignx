# Toy Data

Small deterministic alignment fixtures live here.

## Policy

- Keep committed fixtures small.
- Prefer synthetic reads over downloaded clinical or production datasets.
- Large BAM, CRAM, reference, and benchmark files must not be committed.

## Current Fixtures

- `toy_alignment.sam` - minimal SAM fixture for Phase 0 scaffold tests and future BAM materialization.

Phase 1 should add a small sorted/indexed BAM fixture derived from this SAM file
or another deterministic generator.
