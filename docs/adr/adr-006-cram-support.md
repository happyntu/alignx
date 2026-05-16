# ADR-006: CRAM Support via HTSlib Format-Agnostic API

**Status:** Accepted  
**Date:** 2026-05-16

## Context

v0.4 requires CRAM reading support. The existing `BamReader` wraps HTSlib using
format-agnostic calls (`sam_open`, `sam_read1`, `sam_itr_next`, `sam_format1`)
that already handle BAM, CRAM, and SAM transparently.

## Decision

CRAM reading is supported through HTSlib's auto-detection — no dedicated CRAM
code path. `BamReader::open()` accepts `.cram` files without modification.

Key points:

- `sam_open(path, "r")` auto-detects BAM/CRAM/SAM via magic bytes.
- `sam_index_load()` loads `.bai`, `.csi`, or `.crai` indexes.
- All downstream record access uses format-agnostic `bam1_t` structures.
- CRAM adds `MD:Z` tags computed from the reference; these flow through AXF1
  unchanged and round-trip correctly.

## Reference Handling

- Test fixtures use `samtools view --output-fmt-option embed_ref=1` to embed the
  reference directly in the CRAM file, eliminating `REF_PATH` dependencies.
- Production CRAM files require the user to provide a `.crai` index and configure
  `REF_PATH`/`REF_CACHE` per HTSlib convention.

## Scope

- **In scope:** CRAM reading, CRAM → AXF1 conversion, CRAM → AXF1 → BAM round-trip.
- **Deferred:** CRAM writing. Requires a reference management policy (which
  reference to embed or reference, how to handle multi-reference files). This is
  not needed until v1.0 export improvements.

## Consequences

- Zero production code changes for CRAM reading.
- CRAM test fixtures (`.cram`, `.crai`, reference `.fa`) checked into `tests/toy_data/`.
- `convert_bam_to_axf1_mvp()` and the `convert` CLI subcommand accept CRAM input
  without any format-specific branching.
