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

Remote HG002 correctness smoke coverage now includes a small chr1 region:
`chr1:1000000-1010000` converted to AXF1 and compared against both
BAM-backed `alignx view` and `samtools view`. The stdout SHA256 matched across
all three outputs. This is not a benchmark result.

Metadata inspection for that file reported 7 chunks, 64 total records, max 11
records per chunk, max 27,665 bp span, and max 294,520-byte chunk length. These
values are below the implemented hard caps.

Boundary smoke for the same region-converted AXF1 file confirmed the intended
subset-cache semantics: queries inside `chr1:1000000-1010000` matched the full
BAM and `samtools view`, while queries outside that conversion region returned
only records present in the AXF1 subset.

AXF1 v2 now records `source_path`, `conversion_region`, and `is_subset` in file
metadata so tools can distinguish full-input caches from region-converted subset
caches without decoding chunk payloads.

Remote AXF1 v2 metadata smoke on the same HG002 chr1 region confirmed
`version=2`, `is_subset=true`, the expected source BAM path and conversion
region, unchanged chunk stats, and byte-identical stdout against BAM-backed
`alignx view` and `samtools view`.

Remote AXF1 POS delta-varint smoke on the same HG002 chr1 region confirmed
that all 7 chunks used POS codec id `1`, with byte-identical stdout against
BAM-backed `alignx view` and `samtools view`. The resulting file was
1,858,052 bytes, with max 294,497-byte chunk length.

Remote AXF1 FLAG bit-pack smoke on the same HG002 chr1 region confirmed that
all 7 chunks used FLAG codec id `2` and POS codec id `1`, with byte-identical
stdout against BAM-backed `alignx view` and `samtools view`. The resulting file
was 1,857,974 bytes, with max 294,483-byte chunk length.

Remote AXF1 MAPQ RLE smoke on the same HG002 chr1 region confirmed that all 7
chunks used MAPQ codec id `3`, FLAG codec id `2`, and POS codec id `1`, with
byte-identical stdout against BAM-backed `alignx view` and `samtools view`. The
resulting file was 1,857,924 bytes, with max 294,474-byte chunk length.
The finalized `scripts/inspect_axf1_metadata.py --column-codecs` path was
checked against this AXF1 file and reported `mapq_rle`, `flag_bitpack`, and
`pos_delta_varint` on all 7 chunks without decoding payloads.

AXF1 v2 metadata corruption coverage rejects invalid subset flags, truncated
source/conversion-region strings, and metadata/index overlap in both C++ reader
paths. The Python metadata inspector was checked against the same corruptions.

## Tuning Plan

Before any benchmark or profiling run, freeze a small comparison matrix so the
results stay interpretable:

### Fixed Query Set

- `chr1:1000000-1010000`
  Already verified as a correctness smoke and useful as the baseline region.
- one larger chr1 interval
  Use a broader interval to see whether chunk counts or read amplification
  change with more records.
- one QUAL-heavy interval
  Use a region where `quality` still dominates payload bytes.
- one sparse interval
  Use a region where genomic span is likely to dominate flush behavior.

### Candidate Policy Variants

Treat the current defaults as the baseline:

| Variant | target bytes | max bytes | max records | max span |
|---|---:|---:|---:|---:|
| baseline | 256 KiB | 512 KiB | 4096 | 1,000,000 bp |
| smaller chunks | 128 KiB | 256 KiB | 2048 | 500,000 bp |
| denser chunks | 512 KiB | 1 MiB | 8192 | 1,000,000 bp |
| span-biased | 256 KiB | 512 KiB | 4096 | 250,000 bp |

The goal is not to search a huge parameter space. The goal is to determine
whether the current defaults are already close enough, or whether a single
adjustment direction clearly improves query behavior.

### Metrics To Capture

- chunk count;
- average and max chunk size;
- average and max chunk span;
- `alignx view` wall time;
- stdout parity against `samtools view`;
- codec distribution for POS / CIGAR / SEQ / QUAL;
- selected-column decode behavior when QUAL is wrapped.

### Execution Rule

Do not run the benchmark/profiling step until the user has explicitly
confirmed that the machine is available. The config below is only a planning
artifact.

### Metadata-Only Snapshot

The current planning matrix was generated with
`scripts/plan_axf1_chunk_sizing.sh --manifest-out benchmarks/results/axf1_chunk_sizing_plan.tsv`.
That manifest stays in the repo as a command matrix, not as a benchmark
result.

Metadata-only comparison of two existing HG002 chr1:1000000-1010000 AXF1 files
shows the expected shape change without any timing claim:

| File | Version | Chunk count | Max chunk length | Quality codec | Quality bytes |
|---|---:|---:|---:|---|---:|
| `hybrid.axf1` | 1 | 7 | 294,520 | `raw` | 907,624 |
| `zstd.axf1` | 2 | 7 | 127,168 | `qual_pack_compressed` | 533,367 |

The chunk count and span stayed the same because the input region and hybrid
chunk policy were unchanged. The payload shape changed because the zstd-wrapped
quality column shrank substantially. This is a metadata observation, not a
benchmark or throughput claim.

### Remote Variant Sweep Status

The refreshed `missmi-server00` binary at `/mypool/alignx/bin/alignx` now
contains the chunk-policy env override hook added in `3a621a7`. A remote
correctness sweep on `chr1:1000000-2000000` produced distinct metadata across
the policy variants:

| Variant | Chunk count | Max records/chunk | Max span | Max chunk length | Quality codec distribution |
|---|---:|---:|---:|---:|---|
| baseline | 324 | 11 | 32,398 bp | 174,145 bytes | `qual_rle:25, qual_pack:299` |
| smaller_chunks | 616 | 7 | 30,169 bp | 100,847 bytes | `qual_rle:89, qual_pack:527` |
| denser_chunks | 167 | 21 | 31,924 bp | 319,609 bytes | `qual_rle:8, qual_pack:159` |
| span_biased | 324 | 11 | 32,398 bp | 174,145 bytes | `qual_rle:25, qual_pack:299` |
| span_tight | 324 | 11 | 32,398 bp | 174,145 bytes | `qual_rle:25, qual_pack:299` |

The sweep remained correctness-only: `alignx view` and `samtools view` still
matched byte-for-byte for every variant. The observed differences show that the
byte-budget knobs are active and materially change chunk shape, while the span
cap did not bind on this interval.

Additional sparse-region sweeps confirmed the same pattern:

| Region | Variant | Chunk count | Max span | Max chunk length | Quality codec distribution |
|---|---:|---:|---:|---:|---|
| `chr1:121000000-142000000` | baseline | 6,878 | 167,372 bp | 193,973 bytes | `qual_rle:1797, qual_pack:5081` |
| `chr1:121000000-142000000` | span_biased | 6,878 | 167,372 bp | 193,973 bytes | `qual_rle:1797, qual_pack:5081` |
| `chrY:20000000-21000000` | baseline | 218 | 83,322 bp | 182,454 bytes | `qual_rle:63, qual_pack:155` |
| `chrY:20000000-21000000` | span_biased | 218 | 83,322 bp | 182,454 bytes | `qual_rle:63, qual_pack:155` |

These sweeps still stayed below the 250 kb span cap used by the
`span_biased` variant. At the current HG002 coverage pattern, the chunk byte
budget continues to dominate before the span threshold becomes visible.

### Span-Tight Sweep

To force the span rule to matter, a `span_tight` variant was added with
`max_genomic_span = 50,000 bp`. On the same HG002 intervals, this variant
finally split one of the wider chr1 regions differently:

| Region | Variant | Chunk count | Max span | Max chunk length | Quality codec distribution |
|---|---:|---:|---:|---:|---|
| `chr1:121000000-142000000` | baseline | 6,878 | 167,372 bp | 193,973 bytes | `qual_rle:1797, qual_pack:5081` |
| `chr1:121000000-142000000` | span_tight | 6,883 | 49,475 bp | 193,973 bytes | `qual_rle:1797, qual_pack:5086` |
| `chrY:20000000-21000000` | baseline | 218 | 83,322 bp | 182,454 bytes | `qual_rle:63, qual_pack:155` |
| `chrY:20000000-21000000` | span_tight | 218 | 38,009 bp | 181,103 bytes | `qual_rle:67, qual_pack:151` |

The important point is not the exact counts; it is that a sufficiently tight
span cap now produces a visible change on HG002. The earlier `250 kb`
candidate was simply too loose for the tested regions.

### Recommendation

For the current HG002-style data we have exercised, `50 kb` is the first span
setting that consistently moves chunk shape without causing a dramatic chunk
count explosion. The `250 kb` candidate is still effectively inert on the
tested regions, so it should not be treated as the preferred span-sensitive
default candidate.

Practical conclusion:

- keep the hybrid byte-budget and record-count thresholds as the primary
  controls;
- treat `50 kb` as the active span-sensitive comparison point;
- leave `250 kb` as a loose upper-bound experiment only if we need a less
  aggressive fallback later.

### Benchmark Prep

The remote preflight for the final benchmark regions has passed on
`missmi-server00` with the current `/mypool/alignx/bin/alignx` binary:

- `chr1:1000000-2000000`
- `chr1:121000000-142000000`
- `chrY:20000000-21000000`

The prepared benchmark comparison scope is now:

- baseline: `max_genomic_span = 1,000,000 bp`
- span_tight: `max_genomic_span = 50,000 bp`

Do not start timed repeats until the user explicitly confirms the machine is
available. The next step is a benchmark/profiling run, not another correctness
sweep.

Source identity remains intentionally lightweight in AXF1 v2. `source_path` is
only an audit hint. Future cache-validation metadata should prefer low-cost
fields such as file size, mtime, and BAM header SHA-256 before considering any
full-content hash.

If AXF1 metadata grows beyond v2, the preferred v3 direction is a typed
key/value metadata section with required/optional entry flags. Unknown optional
entries can be skipped; unknown required entries must be rejected before payload
decode.

Before any performance claim, run a separate remote HG002 benchmark with user
confirmation. Notify the user before benchmark or profiling workloads.

## Open Questions

- Should future thresholds be based on compressed bytes, uncompressed bytes, or
  both?
- Should column codecs reset per chunk or use bounded shared dictionaries?
- Should max genomic span vary by reference class or input type?
- How should unmapped records be chunked once AXF1 supports them?

## Recommendation

Keep the hybrid policy as the converter default, then tune thresholds only after
correctness smoke checks and explicit benchmark/profiling confirmation.
