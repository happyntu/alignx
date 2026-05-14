# AXF Reader and Query Follow-up

This note records the next implementation direction after the AXF0 MVP closed
loop. It is a design follow-up, not a benchmark result.

## Current AXF0 Path

The current AXF0 MVP query flow is correctness-first:

```text
alignx view sample.axf chr20:10000000-10010000
  -> query::write_axf_region_sam()
  -> format::read_axf_file()
  -> AxfFile::query_blocks()
  -> row-preserving SAM payload filter
  -> stdout
```

Current implementation characteristics:

- `format::read_axf_file()` reads the entire AXF file into memory.
- AXF reference metadata and block index entries are parsed from that in-memory
  byte vector.
- Each block payload is copied into `AxfBlock::payload` during file read.
- `AxfFile::query_blocks()` linearly scans all blocks.
- `query::write_axf_region_sam()` filters records inside the already-loaded
  row-preserving SAM payloads and writes atomic stdout after successful
  validation.

This is acceptable for toy and small-region correctness smoke checks. It is not
the target shape for production AXF region queries.

## Limitations

- Full-file read makes memory use scale with AXF file size, not query size.
- Eager payload copy defeats the intended region-query advantage of indexed
  AXF access.
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

## Later Columnar Path

After the seekable AXF0 reader is stable, replace row-preserving payloads with
minimal column streams. At that point, region query can evolve from:

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
- add unit tests that prove non-overlapping payload ranges are not read;
- keep malformed payload behavior atomic: error returns must not write partial
  stdout;
- keep `convert --region` using the same 1-based closed input and 0-based
  half-open overlap semantics as AXF view.
