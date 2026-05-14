# AXF Reader and Query Follow-up

This note records the next implementation direction after the AXF0 MVP closed
loop. It is a design follow-up, not a benchmark result.

## Current AXF0 Path

The current AXF0 MVP query flow is correctness-first:

```text
alignx view sample.axf chr20:10000000-10010000
  -> query::write_axf_region_sam()
  -> format::AxfFileReader::open()
  -> AxfFileReader::query_blocks()
  -> AxfFileReader::read_payload() for each hit
  -> row-preserving SAM payload filter
  -> stdout
```

Current implementation characteristics:

- `format::AxfFileReader` is the preferred AXF query API for production call
  sites.
- `format::read_axf_index_metadata()` reads reference metadata and block index
  entries without materializing payloads; it is a lower-level parser helper.
- `format::read_axf_block_payload()` reads payload bytes lazily by offset and
  length for query hits; it is a lower-level payload helper.
- `AxfFileIndex::query_blocks()` uses a per-reference end-sorted candidate index
  to skip blocks ending before the query start, then returns hits in the stable
  start-sorted block order.
- `query::write_axf_region_sam()` filters records inside the already-loaded
  row-preserving SAM payloads and writes atomic stdout after successful
  validation.

This is acceptable for toy and small-region correctness smoke checks. It is still
not the target shape for production AXF region queries because block lookup is
linear and payloads are row-preserving SAM text.

## Limitations

- Linear block scan is fine for small MVP files but will not scale to many
  blocks.
- The row-preserving SAM payload is deliberately not columnar and cannot support
  selective-column I/O.
- AXF0 should not be used for final compression-ratio or performance claims.

## Next Implementation Step

Add a seekable AXF0 reader before attempting AXF performance comparisons:

```text
format::AxfFileReader
  -> open(path)
  -> read header
  -> read reference metadata
  -> read block index entries only
  -> query(ref_id, start, end)
  -> read only overlapping payload byte ranges
```

The first reader can stay AXF0-specific and row-preserving. The goal is to
establish the correct access pattern:

- metadata and index are loaded up front;
- payload bytes are read lazily by offset and length;
- only overlapping block payloads are read for a region query;
- tests continue to compare BAM view stdout vs AXF view stdout.

## Implementation Status

Step 1 is implemented:

- `format::AxfBlockIndexEntry` stores block metadata plus payload offset/length.
- `format::AxfFileIndex` stores references plus block index entries.
- `format::AxfFileIndex` stores per-reference block ranges for query-time
  candidate narrowing.
- `format::AxfFileIndex` stores a per-reference end-sorted block index because
  the primary `ref_id/start_pos/end_pos` order does not make `end_pos`
  monotonic.
- `format::read_axf_index_metadata(path)` reads header, reference metadata, and
  block index entries without materializing `AxfBlock::payload`.
- Metadata validation checks magic/version, index offset, block reference ids,
  block intervals, and payload byte ranges.

Step 2 is implemented:

- `format::read_axf_block_payload(path, block)` reads a single block payload by
  offset/length from the AXF file.
- `format::AxfFileReader` wraps metadata loading, block queries, and lazy payload
  reads behind a reusable reader object.
- `alignx view` for AXF input uses `format::AxfFileReader`, queries overlapping
  block entries, and only reads payloads for those hits.
- Output filtering still validates each SAM line against the requested region, so
  stdout remains compatible with the prior full-file reader path.

## Public API Boundary

- Preferred query path: `format::AxfFileReader::open(path)`,
  `AxfFileReader::query_blocks(ref_id, start, end)`, then
  `AxfFileReader::read_payload(block)`.
- Compatibility/full-file path: `format::read_axf_file(path)` remains available
  for round-trip tests and code that explicitly needs materialized payloads.
- Lower-level helpers: `format::read_axf_index_metadata(path)` and
  `format::read_axf_block_payload(path, block)` remain available for focused
  parser/payload tests, but new query code should prefer `AxfFileReader`.

## Correctness Smoke Checks

Remote HG002 seekable AXF view smoke passed on 2026-05-14:

- Host: `missmi-server00`
- Work directory:
  `/mypool/alignx/tmp/axf_seekable_smoke_hg002_chr20_10000000_10010000`
- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr20:10000000-10010000`
- Records: 107
- BAM stdout bytes: 3,133,962
- AXF stdout bytes: 3,133,962
- AXF file bytes: 3,138,290
- BAM vs AXF diff bytes: 0
- Convert/BAM view/AXF view stderr bytes: 0/0/0

Remote HG002 AXF query-index smoke passed on 2026-05-14 after adding
per-reference ranges, end-sorted candidate indices, and index encapsulation:

- Host: `missmi-server00`
- Work directory:
  `/mypool/alignx/tmp/axf_index_smoke_hg002_chr20_10000000_10010000_20260514`
- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr20:10000000-10010000`
- Records: 107
- BAM stdout bytes: 3,133,962
- AXF stdout bytes: 3,133,962
- AXF file bytes: 3,138,290
- BAM vs AXF diff bytes: 0
- Convert/BAM view/AXF view stderr bytes: 0/0/0

## Later Columnar Path

After the seekable AXF0 reader is stable, replace row-preserving payloads with
minimal column streams. The first AXF1 correctness slice is sketched in
`docs/research/axf1-minimal-columnar-design.md`. At that point, region query can
evolve from:

```text
read overlapping SAM payloads -> parse SAM lines -> print SAM
```

to:

```text
read overlapping column streams -> apply column filters -> format SAM
```

Only the columnar path should be used for selective-column I/O claims.

## Testing Expectations

Before changing format layout or adding codecs:

- keep existing AXF0 toy and HG002 correctness smoke checks passing;
- keep unit tests that prove non-overlapping payload ranges are not parsed;
- keep malformed payload behavior atomic: error returns must not write partial
  stdout;
- keep `convert --region` using the same 1-based closed input and 0-based
  half-open overlap semantics as AXF view.
