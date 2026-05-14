# AXF1 Minimal Columnar Design

This note sketches the first AXF1 columnar vertical slice after the AXF0
row-preserving staging path. It is an implementation planning note, not a
replacement for ADR-002 or ADR-004.

## Source Decisions

- ADR-002 defines the long-term AXF target: chunked columnar alignment storage
  with independent column payloads and a region index.
- ADR-004 defines the compression strategy: CPU-first correctness baseline,
  optional SIMD/GPU acceleration, lossless by default, and chunk-independent
  codecs.
- ADR-005 defines the AXF0 staging contract: 0-based half-open coordinates,
  stable query output, atomic errors, and SAM-compatible region input.

AXF1 should preserve the query and coordinate semantics that AXF0 already
proved, while replacing row-preserving SAM payloads with independently encoded
columns.

## Goals

- Convert the toy BAM fixture into an AXF1 file.
- Query the AXF1 file by region and emit SAM stdout matching the current BAM and
  AXF0 correctness expectations.
- Keep the first implementation lossless and CPU-only.
- Prove the file layout can read only the columns needed for the initial query
  path, even if the first codecs are simple raw/scaffold encodings.
- Keep AXF0 readers and tests intact; AXF1 is a new format path, not an in-place
  AXF0 mutation.

## Non-Goals

- No compression-ratio or performance claims.
- No benchmark/profiling work until correctness is stable.
- No GPU or SIMD-specific implementation.
- No CRAM export.
- No cloud range request implementation, although offsets should remain
  file-absolute and chunk-local enough to support it later.
- No lossy quality-score binning.

## Minimal Record Scope

The first AXF1 slice should support the mapped records already covered by the
toy BAM tests:

- required SAM fields: `QNAME`, `FLAG`, `RNAME`, `POS`, `MAPQ`, `CIGAR`,
  `RNEXT`, `PNEXT`, `TLEN`, `SEQ`, `QUAL`;
- optional tags may initially be stored as a raw per-record tag text column;
- unmapped records are skipped by the current converter and remain out of scope
  for the first slice;
- paired fields should be preserved as text/integer values even if no paired
  query logic is implemented yet.

This is enough to reconstruct SAM stdout for `alignx view` without pretending
the final specialized codecs are done.

## Format Strategy

Use a distinct magic/version so AXF0 and AXF1 are never ambiguous:

```text
FileHeader:
  magic          "AXF1"
  version        u32
  ref_count      u32
  chunk_count    u64
  index_offset   u64

ReferenceEntry repeated ref_count:
  name_length    u16
  name_bytes     [name_length]
  length         u32

Chunk payloads:
  ChunkHeader
  Column payload bytes
  ChunkFooter

Index at index_offset:
  ChunkIndexEntry repeated chunk_count
```

The first implementation can keep chunk headers explicit and redundant with the
index. Redundancy is acceptable while the format is still being validated.

## Chunk Contract

Each chunk is independently decodable and sorted by `ref_id`, then `start_pos`.

```text
ChunkHeader:
  ref_id          u32
  start_pos       i32   # 0-based inclusive
  end_pos         i32   # 0-based exclusive
  record_count    u32
  column_count    u16
  column_entries  [column_count]

ColumnEntry:
  column_id       u16
  codec_id        u16
  offset          u64   # relative to chunk payload start
  length          u64
```

The chunk interval is the union of contained mapped record reference spans. A
query first selects overlapping chunks, then decodes enough row-aligned columns
to reconstruct and filter matching records.

## Initial Columns

The minimum practical column set is:

| Column | Initial encoding | Notes |
|---|---|---|
| `QNAME` | raw string table or length-prefixed strings | Dictionary can come later. |
| `FLAG` | raw `u16` array | Bit-pack later. |
| `RNAME` | implicit chunk `ref_id` for mapped records | Cross-reference records can be handled later. |
| `POS` | raw `i32` array or varint delta | Keep 0-based internally; print 1-based SAM POS. |
| `MAPQ` | raw `u8` array | RLE later. |
| `CIGAR` | length-prefixed strings | Op/length streams later. |
| `RNEXT` | length-prefixed strings or sentinel encoding | Preserve stdout first. |
| `PNEXT` | raw `i32`/`u32` array | Preserve SAM semantics. |
| `TLEN` | raw `i32` array | Preserve SAM semantics. |
| `SEQ` | length-prefixed strings | Reference-delta later. |
| `QUAL` | length-prefixed strings | Lossless raw first. |
| `TAGS` | length-prefixed trailing SAM text | Per-tag streams later. |

All record-aligned columns must have exactly `record_count` values. The reader
should reject missing required columns, column length mismatches, and payload
ranges outside the file/chunk.

## Query Flow

```text
alignx view sample.axf1 chr1:1M-2M
  -> parse SAM-style 1-based closed region
  -> load AXF1 references and chunk index
  -> select chunks by 0-based half-open overlap
  -> decode POS and CIGAR for candidate chunks
  -> filter records by parsed SAM record span
  -> decode remaining output columns only for matching records
  -> format SAM stdout atomically
```

The first implementation may decode all columns for selected toy chunks if that
keeps the slice small, but the public design should keep the door open for
selective output-column decoding.

## Error and Output Contract

AXF1 should inherit the AXF0 query behavior:

- malformed region strings return an error with empty stdout;
- missing references return an error with empty stdout;
- valid no-hit regions return success with empty stdout;
- malformed chunk/column payloads return an error with empty stdout;
- query output is newline-terminated SAM text;
- query result order is stable in chunk order, then record order.

## First Implementation Slice

Status as of 2026-05-14: this slice is implemented through opt-in CLI routing.
`alignx convert` still defaults to AXF0, while `alignx convert --format AXF1`
writes raw-column MVP files. `alignx view` detects AXF0 vs AXF1 from file
magic, so `.axf1` is no longer required for the view path, although tests may
continue to use it as a readability cue while the format is unstable.

The AXF1 query path now uses a metadata-first reader: it streams the file
header/reference/index metadata, selects overlapping chunks by 0-based
half-open interval overlap, and decodes only those chunk payloads. This keeps
malformed non-overlapping chunk payloads from affecting unrelated region
queries and preserves atomic stdout behavior for overlapping malformed chunks.
Within each selected chunk, `alignx view` first decodes only `POS` and `CIGAR`
to identify records that overlap the requested region. Full output columns are
decoded only if the chunk contains at least one matching record. This is still a
raw-codec correctness scaffold, not a benchmark claim.

The AXF1 converter now emits deterministic MVP chunks instead of one chunk per
reference. The current implementation uses a deliberately small max-record rule
to force multi-chunk toy coverage; production chunk sizing by byte budget,
genomic span, or compression block behavior remains future work.

Current converter chunk policy:

- the Phase 1 converter is mapped-record only and skips unmapped records or
  records whose reference span is invalid;
- chunks are emitted deterministically as records are accepted, preserving BAM
  input order for the toy correctness path;
- the max-record threshold is intentionally tiny so tests exercise multi-chunk
  view behavior without large fixtures;
- this threshold is not a performance or format recommendation. Production
  chunking should use a hybrid policy that considers encoded byte size, genomic
  span, record count, and independent column decode cost.

1. Add AXF1 data structs and format read/write tests in files that do not
   disturb AXF0.
2. Add writer/reader round-trip tests using synthetic toy records.
3. Add an internal AXF1 query/view helper and tests for no-hit, missing
   reference, malformed payload, and newline-stable output.
4. Add `toy BAM -> AXF1 -> view` stdout parity against the existing BAM view
   expectation after the format and query helpers are stable.
5. Add CLI routing only after the internal AXF1 view path is covered.
6. Keep AXF0 tests running unchanged.

Suggested implementation boundary:

- new format code: `src/format/axf1_file.hpp/.cpp`;
- new format tests: `tests/unit/test_axf1_file.cpp`;
- new query code: `src/query/axf1_view.hpp/.cpp`, because AXF1 decodes columns
  while AXF0 parses row-preserving SAM payloads;
- new query tests: `tests/unit/test_axf1_view.cpp`;
- converter entry point: `convert::convert_bam_to_axf1_mvp()` is available for
  toy BAM -> AXF1 correctness work;
- CLI routing is opt-in: `.axf` remains AXF0, `.axf1` routes to AXF1 view, and
  conversion requires `--format AXF1`.

## Current Codebase Entry Points

- `src/format/axf_file.hpp/.cpp` owns AXF0 read/write, metadata parsing, lazy
  payload reading, and `AxfFileReader`. AXF1 should not extend these types
  directly; keep AXF0 compatibility isolated.
- `src/query/axf_view.hpp/.cpp` owns AXF0 region query and SAM payload filtering.
  AXF1 should use a separate view helper because it filters from row-aligned
  columns instead of parsing SAM payload text.
- `src/convert/bam_to_axf.hpp/.cpp` emits AXF0 by default and AXF1 through the
  separate `convert_bam_to_axf1_mvp()` function.
- `src/cli/cli.cpp` detects AXF0/AXF1 by file magic before falling back to the
  BAM/HTSlib view path. `.axf`/`.axf1` files with unknown magic are rejected
  instead of being treated as BAM.
- `format::Axf1FileReader` is the preferred AXF1 query API. It loads
  references and chunk index metadata first, then reads and decodes selected
  chunk byte ranges on demand. `read_axf1_file()` remains the full-file
  round-trip helper.
- `Axf1FileReader::read_chunk_columns()` supports selective raw-column decode
  for query filtering. Current `alignx view` uses it for `POS` and `CIGAR`,
  then falls back to full chunk decode for chunks with matching output records.
- `CMakeLists.txt` already globs `src/format/*.cpp`, `src/query/*.cpp`,
  `src/convert/*.cpp`, and `tests/unit/*.cpp`, so the proposed AXF1 source and
  test files are automatically added to `alignx_lib` and `unit_tests`.

## Open Questions

- Should AXF1 tests keep using `.axf1` as a readability cue while the format is
  unstable, or should they converge on `.axf` now that view routing is
  magic-based?
- What production chunk-sizing rule should replace the current deterministic
  MVP max-record split: byte budget, max genomic span, record count, or a hybrid
  tuned to independent column decode?
- Should optional tags remain one raw `TAGS` column for v0, or should common tags
  such as `NM` and `MD` get early per-tag streams?
- Should `RNAME` be implicit from chunk `ref_id` only for the first slice, or
  should the format immediately support records whose displayed `RNAME` differs
  from chunk reference semantics?

## Testing Expectations

Before replacing AXF0 in any workflow:

- keep AXF0 toy, WSL, and remote HG002 correctness smoke checks passing;
- require AXF1 round-trip and view parity tests on the toy BAM fixture;
- require AXF1 multi-reference and multi-chunk ordering tests;
- require malformed AXF1 payload tests to prove stdout atomicity;
- require no-hit and missing-reference behavior to match AXF0;
- do not run benchmark/profiling until the AXF1 correctness slice is stable.
