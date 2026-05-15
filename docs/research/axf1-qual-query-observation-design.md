# AXF1 QUAL Query-Impact Observation Design

Status: design note, 2026-05-15.

## Motivation

AXF1 QUAL already has two implemented lossless paths:

- `qual_rle` as the first narrow QUAL codec;
- `qual_pack` as the next alphabet bit-pack codec;
- `qual_pack_compressed` as a generic compressed-payload wrapper when
  `ALIGNX_ENABLE_ZSTD=ON`.

The remaining question is not whether QUAL can be compressed at all. It is how
to decide the next meaningful step after the current wrapper path:

- keep tuning the generic wrapper family,
- or move to a QUAL-specific model that uses local quality structure.

That decision should be driven by measured query impact, not by codec
complexity alone.

## Goals

- Define the future measurements that matter for AXF1 QUAL work.
- Keep the decision focused on region-query latency and selected-column decode.
- Separate correctness-only smoke checks from benchmark/profiling work.
- Make the next QUAL direction explicit before any further implementation.

## Non-Goals

- Do not run benchmark or profiling workloads in this note.
- Do not change the current writer policy.
- Do not add a new QUAL codec here.
- Do not make compression-ratio claims.

## Questions To Answer

1. Does `qual_pack_compressed` materially change `alignx view` latency on
   realistic HG002 region queries?
2. Is QUAL decode the limiting factor, or does the overhead sit elsewhere in
   the query path?
3. Does selected-column decode stay cheap enough when QUAL is wrapped?
4. Is the generic wrapper already sufficient, or is a QUAL-specific model the
   next better step?

## Candidate Measurement Axes

Use the same query family across every candidate so results stay comparable:

- `alignx view` on AXF1 with quality selected;
- `alignx view` on AXF1 with a reduced column set where possible;
- BAM-backed `alignx view` as the baseline;
- `samtools view` as the external baseline.

Track at least:

- wall time;
- CPU time;
- peak RSS;
- bytes decoded per column;
- codec distribution in QUAL;
- stdout parity.

## Suggested Future Region Set

When benchmark/profiling is explicitly authorized, use a small fixed region set
instead of one-off ad hoc intervals:

- HG002 chr1 small-region smoke interval already used for correctness:
  `chr1:1000000-1010000`
- a larger chr1 interval for more representative query cost;
- one interval where QUAL remains wrapper-heavy;
- one interval where QUAL is mostly base codec fallback.

The exact intervals should be frozen before timing starts so the comparison is
repeatable.

## Decision Rules

If profiling shows that `qual_pack_compressed` materially hurts query latency
without delivering enough size benefit, the next step should be a QUAL-specific
model that preserves selected-column decode but reduces decode work.

If the wrapper cost is small and the size benefit is consistent, keep the
generic wrapper path and only expand the compression registry when justified.

If QUAL remains the dominant payload but generic compression does not improve
the real query path enough, prioritize QUAL-specific residual or context
coding over additional generic compressors.

## Relationship To Existing Notes

- `docs/research/axf1-qual-next-codec-design.md` chooses `qual_pack` as the
  next narrow QUAL codec.
- `docs/research/axf1-compressed-payload-wrapper-design.md` defines the shared
  compressed-payload envelope.
- `docs/research/axf1-lz4-compressed-payload-design.md` reserves `compression_id=2`
  but defers LZ4 until profiling shows a fast-profile need.

This note sits after those decisions and defines how to decide the next step.

## Current Recommendation

Do not implement another generic compressor yet. The next design work should be
to measure whether the existing zstd wrapper changes query behavior enough to
justify keeping it as the long-term QUAL path, or whether a QUAL-specific model
should replace it.
