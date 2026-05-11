# Toy Data

Small deterministic alignment fixtures live here.

## Policy

- Keep committed fixtures small.
- Prefer synthetic reads over downloaded clinical or production datasets.
- Large BAM, CRAM, reference, and benchmark files must not be committed.

## Current Fixtures

- `toy_alignment.sam` - minimal SAM fixture for Phase 0 scaffold tests and future BAM materialization.
- `toy_alignment.sorted.bam` - deterministic BGZF/BAM fixture generated from `scripts/materialize_toy_bam.py`.
- `toy_alignment.sorted.bam.bai` - minimal BAI index for the mapped `chrToy` records.
- `toy_alignment.sorted.bam.csi` - minimal BGZF-compressed CSI index for the mapped `chrToy` records.

Regenerate the binary fixtures with:

```powershell
python scripts/materialize_toy_bam.py
```
