# AXF1 Chunk Sizing Policy

Status: implemented as the first converter policy; thresholds still need
empirical tuning.

This note complements ADR-002, ADR-004, ADR-005, and
`docs/research/axf1-minimal-columnar-design.md`. It defines the first
production-oriented AXF1 chunk sizing policy. It is not a benchmark result.

## Current Status

The AXF1 converter uses the hybrid policy described below. The current
converter is mapped-only and skips unmapped or invalid-span records.

## Goals

- Preserve independently decodable chunks.
- Bound random-access read amplification for regional queries.
- Keep compression and future dictionary locality reasonable.
- Preserve stable output order and atomic error behavior.
- Keep chunk byte ranges friendly to cloud object storage and range requests.
- Stay CPU-first and lossless; do not assume GPU or SIMD availability.

## Non-Goals

- Final paper-grade threshold tuning.
- Codec-specific compression decisions.
- Lossy quality-score handling.
- Final unmapped-read chunking semantics.

## Candidate Policies

### Fixed Record Count

Flush after a fixed number of records.

Pros:
- Very simple.
- Easy to test.

Cons:
- Dense regions, long reads, or records with large auxiliary fields can create
  very large chunks.
- Sparse regions can create chunks with very wide genomic spans.

### Byte Budget

Flush when the pending raw or encoded chunk payload reaches a byte threshold.

Pros:
- Directly controls I/O size and cache behavior.
- Aligns well with range-request and object-storage reads.
- Raw column sizes are available before compression, so the writer can estimate
  this without depending on a final codec.

Cons:
- Does not by itself bound genomic overfetch in sparse regions.
- Compression ratio can vary, so compressed chunk size remains approximate.

### Genomic Span

Flush when the pending chunk interval would exceed a maximum reference span.

Pros:
- Bounds regional query overfetch.
- Useful for sparse regions.

Cons:
- Does not bound chunk bytes in ultra-dense regions.
- Needs careful handling for long records and structural events.

### Hybrid Policy

Flush on reference change or when any threshold would be exceeded:

- maximum uncompressed byte budget
- maximum record count
- maximum genomic span

This is the recommended first production-MVP policy.

## Implemented First Production-MVP Policy

Use a hybrid policy with conservative constants:

| Setting | Initial value | Purpose |
| --- | ---: | --- |
| Target uncompressed bytes | 256 KiB | Normal flush target after append |
| Maximum uncompressed bytes | 512 KiB | Hard cap before appending another record |
| Maximum records | 4096 | Guards tiny-record dense chunks |
| Maximum genomic span | 1,000,000 bp | Bounds sparse-region overfetch |

Rules:

1. Flush on reference change.
2. Before appending a record, flush the pending chunk if adding the record would
   exceed `max_uncompressed_bytes`, `max_records`, or `max_genomic_span`.
3. After appending a record, flush if the chunk reaches
   `target_uncompressed_bytes`.
4. If a single record exceeds the byte budget, emit it as a one-record chunk.
5. Define the chunk interval as the union `[min_start, max_end)` of records in
   the chunk.
6. Preserve BAM input order. The writer should either require coordinate-sorted
   input or explicitly document how interleaved references are handled.
7. Estimate bytes from the encoded/raw column payload before compression.

The 256 KiB target and 512 KiB cap are starting points, not final tuned values.
They are small enough to keep range reads and cache misses bounded, while still
large enough to amortize chunk headers and future compression dictionaries.
Empirical tuning should use HG002-style data later, with user confirmation
before benchmark or profiling runs.

## Tradeoffs

| Concern | Recommended handling |
| --- | --- |
| Regional query read amplification | Bound by genomic-span and byte thresholds |
| Compression ratio | Accept moderate dictionary resets for predictable access |
| Cloud range requests | Keep chunks in sub-MiB uncompressed ranges initially |
| Dense regions | Bound by byte and record thresholds |
| Sparse regions | Bound by genomic-span threshold |
| Long single records | Allow one-record oversized chunks |

## Implementation

The policy object lives in `src/convert/axf1_chunk_policy.hpp`:

```cpp
struct Axf1ChunkPolicy {
  std::size_t target_uncompressed_bytes = 256 * 1024;
  std::size_t max_uncompressed_bytes = 512 * 1024;
  std::size_t max_records = 4096;
  std::int64_t max_genomic_span = 1'000'000;
};
```

The converter tracks pending chunk state:

- reference id
- minimum start
- maximum end
- record count
- estimated uncompressed column bytes

The converter uses two checks:

- `should_flush_before_append(policy, pending, next_record)`
- `should_flush_after_append(policy, pending)`

The policy constants are internal and tests use small policy values to exercise
each flush rule. Add user-facing tuning flags only after the basic policy has
correctness coverage.

## Validation Plan

Focused tests with small thresholds cover:

- flush by maximum record count
- flush by maximum byte threshold
- flush by maximum genomic span
- flush on reference change
- single oversized record emits as one chunk
- decoded SAM parity for toy inputs remains unchanged

Before any performance claim, run a remote HG002 correctness smoke/preflight.
Notify the user before benchmark or profiling workloads.

## Open Questions

- Should future thresholds be based on compressed bytes, uncompressed bytes, or
  both?
- Should column codecs reset per chunk or use bounded shared dictionaries?
- Should max genomic span vary by reference class or input type?
- How should unmapped records be chunked once AXF1 supports them?

## Recommendation

Keep the hybrid policy as the converter default, then tune thresholds only after
correctness smoke checks and explicit benchmark/profiling confirmation.
