# AXF1 Next QUAL Codec Design

Status: implemented, 2026-05-15. `qual_pack` is codec id 7.

## Motivation

`qual_rle` is implemented as codec id 6 and works on low-entropy toy data, but
the HG002 chr1 small-region correctness smoke showed raw fallback on all 7
chunks:

```text
work_dir: /mypool/alignx/tmp/axf1_qual_rle_smoke_hg002_chr1_1000000_1010000_20260515
region:   chr1:1000000-1010000
records:  64
stdout:   6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389
quality:  raw:7, 907,624 bytes, 77.843% of column payload bytes
```

This means simple byte RLE is a correctness scaffold, not a sufficient
compression answer for HG002-style long-read quality streams. The next QUAL
codec should still be lossless, chunk-local, and easy to validate.

## Goals

- Preserve byte-identical SAM `QUAL` reconstruction.
- Keep raw fallback for `*`, empty strings, malformed payloads, and
  non-beneficial payloads.
- Keep decode chunk-local and independent.
- Avoid cross-chunk dictionaries, reference dependencies, and lossy quality
  binning.
- Choose a next codec that can be implemented and tested before more complex
  entropy or context models.

## Non-Goals

- Do not implement lossy Illumina 8-level binning as a default codec.
- Do not implement FQZComp-like context modeling yet.
- Do not add rANS/arithmetic coding yet.
- Do not wrap individual columns in zstd until the format has a generic
  compressed-payload wrapper and dependency policy.
- Do not make compression-ratio or throughput claims before scheduled
  benchmark work.

## Candidate Codecs

| Candidate | Strength | Risk / Cost | Decision |
|---|---|---|---|
| Raw strings | Exact and universal | Dominates payload | Keep fallback |
| Byte RLE (`qual_rle`) | Simple and implemented | HG002 fallback raw | Keep scaffold |
| Per-record alphabet pack | Simple, self-contained | Weak when each read has broad alphabet | Good toy/edge codec, but likely limited |
| Per-chunk alphabet pack | Better amortization than per-record alphabet | Needs chunk metadata and fallback rules | Recommended next |
| Delta + zigzag varint | Captures smooth local changes | Can expand noisy data; signed-byte validation | Secondary candidate |
| Per-cycle predictor + residual | Targets quality-position structure | More complex validation and metadata | Later |
| Generic compressed column wrapper | Helps broad noisy data | Requires wrapper design and dependency policy | Separate design |
| FQZComp-like model | Strong specialized compression | High complexity and harder correctness isolation | v1.0+ |

## Implemented Codec: `qual_pack`

The implemented next codec is a lossless chunk-local alphabet bit-packing codec
with raw fallback. The writer chooses the smallest payload among raw strings,
`qual_rle`, and `qual_pack`.

Payload shape:

```text
QualPackPayload:
  alphabet_count uvarint
  alphabet       repeated alphabet_count u8
  record repeated record_count:
    length       uvarint
    packed_codes bytes, ceil(length * bit_width / 8)
```

`bit_width` is derived from `alphabet_count`:

```text
1 value       -> 0 bits per quality byte
2 values      -> 1 bit
3-4 values    -> 2 bits
5-8 values    -> 3 bits
9-16 values   -> 4 bits
17-32 values  -> 5 bits
33-64 values  -> 6 bits
65-128 values -> 7 bits
129-256 values -> 8 bits, not useful; fall back raw unless header wins
```

Writer rules:

- Use only for concrete non-empty quality strings.
- Fall back raw if any record has `*` or an empty quality string.
- Build one chunk-local alphabet from all quality bytes.
- Preserve byte values exactly; do not offset, normalize, cap, or bin.
- Use canonical alphabet order: ascending unsigned byte value.
- Use the codec only when payload size is smaller than raw length-prefixed
  strings.
- Fall back raw if `alphabet_count` is zero or exceeds 128.

Reader validation:

- Reject truncated alphabet count, alphabet bytes, lengths, or packed bytes.
- Reject duplicate alphabet entries or non-ascending alphabet entries.
- Reject `alphabet_count == 0`.
- Reject unsupported `alphabet_count > 128`.
- Decode exactly `record_count` records.
- Reject excessive decoded lengths and trailing bytes.
- Preserve atomic stdout behavior for malformed payloads.

## Why `qual_pack` Before Context Models

`qual_pack` is not expected to solve every HG002-like quality stream, but it is
the next useful step because it is:

- lossless and self-contained;
- compatible with selected-column decode;
- easy to inspect from metadata and unit tests;
- independent of zstd/rANS dependency decisions;
- a better probe than RLE for whether quality alphabets are small enough to
  exploit without context modeling.

If HG002 still falls back raw, that result is useful: it narrows the remaining
path toward generic compressed payload wrappers or QUAL-specific context
models, without adding those complexities prematurely.

## Acceptance Criteria for `qual_pack`

- Unit tests cover small-alphabet round-trip and raw fallback for broad/noisy
  alphabets.
- Raw fallback tests cover `*`, empty quality, and non-beneficial payloads.
- Malformed tests cover truncated alphabet count, truncated alphabet, duplicate
  or unsorted alphabet, truncated length, truncated packed bytes, excessive
  decoded length, and trailing bytes.
- Selected-column read tests prove `read_chunk_columns(..., {quality})` decodes
  `qual_pack` without unrelated columns.
- `scripts/inspect_axf1_metadata.py` reports the codec name.
- WSL `ctest` passes.
- Toy codec smoke passes on a region where `quality=qual_pack` is expected.
- Remote HG002 smoke is correctness-only and run after user confirmation.

## Toy Smoke

Toy correctness smoke on 2026-05-15 used `/tmp/alignx_axf1_qual_pack_smoke` and
confirmed byte-identical SAM stdout
(`e62402c0450decf357ef797750af1dfb0be065eeeb3b87f157953a7f7ae1feb9`) for 2
records. The smoke asserted `quality=qual_pack` along with the existing
POS/FLAG/CIGAR/SEQ expected codecs. This is a correctness smoke, not a
benchmark.

## Remote HG002 Smoke

Remote HG002 correctness smoke on 2026-05-15 used
`/mypool/alignx/tmp/axf1_qual_pack_smoke_hg002_chr1_1000000_1010000_20260515`
for `chr1:1000000-1010000`. It preserved byte-identical SAM stdout
(`6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`) for 64
records and used `qual_pack` for `quality` on all 7 chunks.

Metadata-only summary:

| Column | Codec distribution | Payload bytes | Share |
|---|---|---:|---:|
| `quality` | `qual_pack:7` | 794,762 | 75.468% |
| `sequence` | `seq_2bit_literal:7` | 227,010 | 21.556% |
| `cigar` | `cigar_token:7` | 20,689 | 1.965% |
| `tags` | `raw:7` | 7,249 | 0.688% |
| `qname` | `raw:7` | 2,390 | 0.227% |

Compared with the previous raw QUAL payload for the same region, `qual_pack`
reduced `quality` from 907,624 bytes to 794,762 bytes. This is a correctness
smoke and metadata summary, not a benchmark.

## Deferred Work

If `qual_pack` does not improve HG002-style data, the next design should focus
on either:

- a generic compressed column wrapper with explicit dependency and metadata
  policy; or
- a QUAL-specific context model using read position/cycle and residual coding.

Both are larger format decisions than `qual_pack` and should be designed before
implementation.
