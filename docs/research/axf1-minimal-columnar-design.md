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
  version        u32   # current writer emits 2
  ref_count      u32
  chunk_count    u64
  index_offset   u64

ReferenceEntry repeated ref_count:
  name_length    u16
  name_bytes     [name_length]
  length         u32

FileMetadata:
  # present for version 2+
  is_subset          u8
  source_path_len    u32
  source_path        [source_path_len]
  region_len         u32
  conversion_region  [region_len]

Chunk payloads:
  ChunkHeader
  Column payload bytes
  ChunkFooter

Index at index_offset:
  ChunkIndexEntry repeated chunk_count
```

The first implementation can keep chunk headers explicit and redundant with the
index. Redundancy is acceptable while the format is still being validated.
Readers still accept legacy AXF1 v1 files without `FileMetadata` and treat them
as `is_subset=false` with empty source and region strings.
AXF1 v2 metadata corruption coverage currently rejects invalid subset flags,
truncated source path metadata, truncated conversion-region metadata, and
metadata that overlaps the chunk index. The C++ full-file reader and
metadata-only reader share this expectation, and `scripts/inspect_axf1_metadata.py`
was checked against the same corruptions.

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

The AXF1 converter now emits deterministic chunks using the first
production-oriented hybrid policy documented in
`docs/research/axf1-chunk-sizing-policy.md`: byte budget, genomic span, record
count, and reference-change flushes.

Current converter chunk policy:

- the Phase 1 converter is mapped-record only and skips unmapped records or
  records whose reference span is invalid;
- chunks are emitted deterministically as records are accepted, preserving BAM
  input order;
- the first production-MVP policy flushes by target/max uncompressed byte
  budget, max record count, max genomic span, and reference changes;
- the initial thresholds are implementation defaults, not benchmark-tuned
  performance claims.

Current correctness coverage:

- toy BAM -> AXF1 -> view stdout parity for mapped toy records;
- remote HG002 chr1 small-region AXF1 convert -> view stdout parity against
  both `alignx view` on BAM and `samtools view`;
- explicit mapped-only converter behavior: the toy unmapped `read003` is not
  written to AXF1;
- multi-chunk query of converted AXF1 output;
- synthetic multi-reference and multi-chunk query ordering;
- metadata-first lazy decode, including malformed non-overlapping chunks;
- atomic stdout behavior for malformed overlapping chunks;
- magic-based AXF0/AXF1 CLI view routing.

Completed implementation checklist:

- AXF1 data structs and format read/write tests were added without mutating
  AXF0.
- Writer/reader round-trip tests cover synthetic toy records.
- AXF1 v2 metadata round-trip and corruption tests cover both full-file and
  metadata-only readers.
- Internal AXF1 query/view tests cover no-hit, missing reference, malformed
  payload, newline-stable output, and atomic stdout behavior.
- Toy BAM -> AXF1 -> view stdout parity matches the existing BAM view
  expectation for mapped toy records.
- CLI view routing detects AXF0/AXF1 by magic after the internal AXF1 helper is
  covered.
- AXF0 tests continue to run unchanged alongside AXF1 tests.

Remote HG002 hybrid chunk smoke, run on `missmi-server00` on 2026-05-14:

- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr1:1000000-1010000`
- Output directory:
  `/mypool/alignx/tmp/axf1_hybrid_smoke_hg002_chr1_1000000_1010000_20260514`
- AXF1 output: `hg002_chr1_small.axf1`
- AXF1 file size: 1,858,056 bytes
- Records: 64
- `axf1 view`, `alignx view` on BAM, and `samtools view` stdout SHA256:
  `6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`
- Result: AXF1 stdout matched both BAM-backed alignx output and samtools output.

This was a correctness smoke only, not a benchmark or profiling run.

Chunk metadata inspection for the same AXF1 file used
`scripts/inspect_axf1_metadata.py`, which reads only the AXF1 header and chunk
index:

| Metric | Value |
|---|---:|
| Chunk count | 7 |
| Total records | 64 |
| Min records per chunk | 5 |
| Max records per chunk | 11 |
| Average records per chunk | 9.143 |
| Min span | 16,567 bp |
| Max span | 27,665 bp |
| Average span | 21,187 bp |
| Min chunk length | 161,244 bytes |
| Max chunk length | 294,520 bytes |
| Average chunk length | 264,790.857 bytes |

Sanity result: the HG002 small-region AXF1 output stayed below the current
hybrid hard caps of 4,096 records, 512 KiB chunk length, and 1,000,000 bp span.

Remote AXF1 v2 metadata smoke, run on `missmi-server00` on 2026-05-14:

- Output directory:
  `/mypool/alignx/tmp/axf1_v2_metadata_smoke_hg002_chr1_1000000_1010000_20260514`
- AXF1 output: `hg002_chr1_small_v2.axf1`
- Version: 2
- `is_subset`: true
- `source_path`:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- `conversion_region`: `chr1:1000000-1010000`
- AXF1 file size: 1,858,194 bytes
- Chunk count: 7
- Total records: 64
- Max records per chunk: 11
- Max span: 27,665 bp
- Max chunk length: 294,520 bytes
- `axf1 view`, `alignx view` on BAM, and `samtools view` stdout SHA256:
  `6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`
- Result: AXF1 v2 metadata was readable without decoding payloads, and AXF1
  stdout matched both BAM-backed alignx output and samtools output.

This was a correctness smoke only, not a benchmark or profiling run.

## Region-Converted AXF1 Subset Semantics

`alignx convert --region` writes a subset AXF1 file containing records selected
from the source BAM for that conversion region. It is not a complete chromosome
or whole-BAM cache. Queries inside the conversion region should match the source
BAM for that region. Queries outside the conversion region are answered only
from records that were written into the AXF1 file, so they can differ from full
BAM queries.

AXF1 v2 records this in file metadata:

- `is_subset=false` and empty `conversion_region` for full-input conversion;
- `is_subset=true` and the original region string for `convert --region`;
- `source_path` records the input path used by the converter.

Remote boundary smoke for the HG002 AXF1 subset above:

| Label | Query region | AXF1 records | Full BAM records | AXF1 vs full BAM | Full BAM vs samtools |
|---|---|---:|---:|---|---|
| left_edge | `chr1:1000000-1000010` | 37 | 37 | match | match |
| middle | `chr1:1005000-1005010` | 38 | 38 | match | match |
| right_edge | `chr1:1009990-1010000` | 33 | 33 | match | match |
| full_convert_region | `chr1:1000000-1010000` | 64 | 64 | match | match |
| right_outside | `chr1:1010001-1011000` | 33 | 35 | subset differs | match |
| left_outside | `chr1:980000-990000` | 8 | 76 | subset differs | match |
| left_nohit_outside | `chr1:970000-980000` | 0 | 71 | subset differs | match |
| right_nohit_outside | `chr1:1030000-1031000` | 0 | 31 | subset differs | match |

For the outside regions with AXF1 records, the AXF1 output was verified to be a
subset of the full-BAM output. This is expected because long reads selected by
the conversion region can extend beyond the requested conversion interval.

Boundary smoke output directory:
`/mypool/alignx/tmp/axf1_hybrid_smoke_hg002_chr1_1000000_1010000_20260514/boundary_smoke`.

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
- `src/convert/axf1_chunk_policy.hpp/.cpp` owns the first AXF1 hybrid chunk
  sizing policy and its testable flush predicates.
- `scripts/inspect_axf1_metadata.py` inspects AXF1 header and chunk-index
  metadata, including v2 source/subset metadata, without decoding chunk
  payloads.
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
- `docs/research/axf1-chunk-sizing-policy.md` defines the recommended first
  production AXF1 chunk sizing policy.
- `CMakeLists.txt` already globs `src/format/*.cpp`, `src/query/*.cpp`,
  `src/convert/*.cpp`, and `tests/unit/*.cpp`, so the proposed AXF1 source and
  test files are automatically added to `alignx_lib` and `unit_tests`.

## Open Questions

- Should AXF1 tests keep using `.axf1` as a readability cue while the format is
  unstable, or should they converge on `.axf` now that view routing is
  magic-based?
- What final threshold values should the implemented hybrid chunk sizing policy
  use after empirical tuning?
- Should AXF1 metadata later include source identity stronger than path text,
  such as file size, mtime, content hash, or BAM header digest?
- Should optional tags remain one raw `TAGS` column for v0, or should common tags
  such as `NM` and `MD` get early per-tag streams?
- Should `RNAME` be implicit from chunk `ref_id` only for the first slice, or
  should the format immediately support records whose displayed `RNAME` differs
  from chunk reference semantics?

## Testing Expectations

Before replacing AXF0 in any workflow:

- keep AXF0 toy, WSL, and remote HG002 correctness smoke checks passing;
- require AXF1 round-trip and view parity tests on the toy BAM fixture;
- require explicit mapped-only converter tests until unmapped AXF1 support is
  designed;
- require AXF1 multi-reference and multi-chunk ordering tests;
- require malformed AXF1 payload tests to prove stdout atomicity;
- require malformed AXF1 metadata tests to prove both full-file and
  metadata-only readers reject corrupt file-level metadata;
- require no-hit and missing-reference behavior to match AXF0;
- do not run benchmark/profiling until the AXF1 correctness slice is stable.
