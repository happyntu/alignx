# ADR-005: AXF MVP Staging

| Field | Content |
|---|---|
| Status | **Accepted** |
| Date | 2026-05-13 |
| Decision makers | cklin |
| Scope | `src/format/`, `src/convert/`, `src/query/`, `src/cli/`, `tests/` |

---

## Context

ADR-002 defines the long-term AXF target as a columnar alignment format with
independent compressed columns. Phase 1 BAM-backed profiling showed that
low-risk BAM-path optimization can improve `alignx view`, especially with
HTSlib worker threads, but the remaining dominant cost is still BAM/BGZF
read/iteration rather than SAM formatting.

To move beyond the BAM path, alignx needs an AXF conversion and query loop.
Implementing the full columnar codec stack in one step would delay correctness
validation and make failures harder to isolate.

---

## Decision

AXF development will proceed through a minimal correctness-first MVP before the
full ADR-002 columnar codec design.

The MVP goal is a working closed loop:

```text
toy BAM -> alignx convert -> toy AXF -> alignx view -> SAM stdout
```

The first AXF MVP is allowed to store row-preserving SAM-line payloads inside
indexed genomic blocks. This is a staging format, not the final columnar layout.
It exists to validate file structure, block indexing, CLI wiring, and region
query correctness before introducing specialized column codecs.

---

## MVP Scope

### In Scope

- `alignx convert <input.bam> -o <output.axf>`
- `alignx view <input.axf> <region>`
- Toy BAM correctness:
  - `alignx view toy.axf chrToy:1-250`
  - stdout must match `samtools view toy.bam chrToy:1-250`
- A simple AXF file header:
  - magic/version
  - reference metadata needed for region parsing
  - block index offset
- Sorted genomic blocks:
  - reference id
  - block start/end positions
  - record count
  - payload offset/length
- Block payload:
  - initial MVP may store SAM records as newline-terminated UTF-8 text
  - records stay byte-compatible with `samtools view` output for supported fields
- File index:
  - interval -> payload offset
  - enough to skip non-overlapping blocks for region query
- Unit/integration tests for toy conversion and query.

### Out of Scope for MVP

- Full independent column streams from ADR-002.
- POS varint delta, CIGAR delta, quality codec, read-name dictionary, tag streams.
- CRAM input/output.
- AXF -> BAM/CRAM export.
- Lossless compression ratio claims.
- Paper-grade benchmark claims.
- Cloud object storage range request behavior.
- SIMD/GPU decode paths.

---

## Initial File Shape

The exact binary layout can evolve during implementation, but the MVP should be
small and explicit:

```text
FileHeader:
  magic          "AXF0"
  version        u32
  ref_count      u32
  block_count    u64
  index_offset   u64

ReferenceEntry repeated ref_count:
  name_length    u16
  name_bytes     [name_length]
  length         u32

Block payloads:
  newline-terminated SAM records for one genomic block

Index at index_offset:
  BlockEntry repeated block_count:
    ref_id        u32
    start_pos     i32   # 0-based inclusive
    end_pos       i32   # 0-based exclusive
    record_count  u32
    payload_offset u64
    payload_length u64
```

Coordinates inside the AXF index are 0-based half-open. CLI region strings remain
SAM-compatible 1-based closed intervals and are converted at query boundaries.
The AXF0 MVP parser currently supports only `ref:start-end` region strings and
does not support reference names containing `:`.

---

## Implementation Order

1. Add `src/format/axf_file.hpp/.cpp` for MVP header/index read/write.
2. Add `src/convert/bam_to_axf.hpp/.cpp` for BAM -> MVP AXF conversion.
3. Add `src/query/axf_view.hpp/.cpp` for AXF region query to SAM stdout.
4. Add `alignx convert <bam> -o <axf>`.
5. Route `alignx view <input.axf> <region>` to the AXF path while keeping the BAM
   path unchanged.
6. Add toy correctness tests before any real-BAM performance work.

## Implementation Status

As of 2026-05-14, the AXF0 closed loop is implemented:

- `src/format/axf_file.hpp/.cpp` reads and writes the AXF0 MVP file shape.
- `src/convert/bam_to_axf.hpp/.cpp` converts mapped BAM records into
  row-preserving AXF0 blocks.
- `src/query/axf_view.hpp/.cpp` handles AXF0 region query and SAM payload
  filtering.
- `alignx convert <bam> -o <axf>` and `alignx view <axf> <region>` are wired
  through the CLI.
- Tests cover toy conversion, AXF view, and BAM -> AXF -> view stdout parity.

This status does not change the long-term ADR-002 columnar target. AXF0 remains
a correctness staging format and should not be used for final compression-ratio
or performance claims.

---

## Consequences

### Positive

- Establishes conversion, indexing, and query plumbing quickly.
- Keeps correctness debugging separate from compression/codec work.
- Allows the CLI and tests to support AXF inputs before the final columnar codec
  stack is ready.
- Provides a concrete stepping stone toward ADR-002.

### Negative

- The MVP payload is not columnar and should not be benchmarked as the final AXF
  performance model.
- The MVP may be larger than BAM/CRAM.
- A later migration step is required from row-preserving payloads to true
  independent column streams.

---

## Follow-up Decision Point

After the toy AXF closed loop passes, decide whether to:

1. Replace SAM-line payloads with minimal column streams for POS/FLAG/MAPQ/CIGAR.
2. Keep SAM-line payloads briefly for CLI integration while implementing
   `pileup`/filter tests.
3. Start performance smoke tests on small real BAMs only after correctness is
   stable.
