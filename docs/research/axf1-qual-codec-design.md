# AXF1 QUAL Codec Design

Status: design note, 2026-05-15. No format change is implemented by this note.

## Motivation

After the POS, FLAG, MAPQ, CIGAR, and SEQ codecs, `QUAL` is the dominant
remaining AXF1 payload column in the HG002 chr1 small-region smoke output.

Metadata-only summary for
`/mypool/alignx/tmp/axf1_expect_codec_smoke_hg002_chr1_1000000_1010000_20260515/sample.axf1`
showed:

| Column | Payload bytes | Share |
|---|---:|---:|
| `quality` | 907,624 | 77.843% |
| `sequence` | 227,010 | 19.470% |
| `cigar` | 20,689 | 1.774% |
| `tags` | 7,249 | 0.622% |
| `qname` | 2,390 | 0.205% |

This makes QUAL the next codec target before QNAME or TAGS.

## Goals

- Preserve byte-identical reconstruction of the SAM `QUAL` field.
- Keep chunk-local independent decode, matching the AXF1 codec model.
- Keep raw fallback for `*`, empty values, malformed payloads, and
  non-beneficial payloads.
- Keep the default profile lossless. No quality-score binning is allowed unless
  the user explicitly chooses a lossy profile in a future version.
- Avoid context models, external dictionaries, and cross-chunk state in the
  first implementation.

## Non-Goals

- Do not implement Illumina 8-level lossy binning in Phase 1.
- Do not add FQZComp-like context modeling in the first implementation.
- Do not zstd-compress the whole QUAL column until the format has a generic
  compressed-payload wrapper design.
- Do not make compression-ratio or performance claims before benchmark work is
  explicitly scheduled.

## Current State

AXF1 currently stores `QUAL` as raw length-prefixed strings. The converter
copies SAM field 10 into `Axf1Record::quality`, and `alignx view` writes that
stored value directly back to SAM output. This means the first QUAL codec must
preserve the exact string, including `*` if it appears.

Unlike POS, MAPQ, or CIGAR, QUAL is often the largest column and may have weak
local repetition. A simple codec may not help every dataset, so broad raw
fallback is required.

## Codec Candidates

| Candidate | Strength | Risk / Cost | Phase 1 decision |
|---|---|---|---|
| Raw strings | Simple and exact | Very large payload | Current fallback |
| Byte RLE | Simple, self-contained, fast | Helps only repeated quality bytes | Good first codec |
| Delta + zigzag varint | Captures smooth changes | Can expand noisy qualities | Defer until RLE evidence is known |
| Per-record fixed-width pack | Works if alphabet is small | Needs per-chunk alphabet or bit width metadata | Possible later |
| Per-cycle/read-group context model | Better ratio | Much more complex and dataset-specific | v1.0+ |
| FQZComp-like model | Strong specialized compression | Complex and harder to validate | v1.0+ |
| zstd payload wrapper | Broad compression | Needs generic compressed column wrapper and dependency policy | Later design |
| Lossy binning | Large size reduction | Not lossless; changes output | Not default; future opt-in only |

## First Implementation Candidate: `qual_rle`

The recommended first QUAL codec is a chunk-local byte RLE stream with raw
fallback.

Payload shape:

```text
QualRlePayload:
  record repeated record_count

record:
  length      uvarint
  run repeated until length decoded:
    run_len   uvarint
    value     u8
```

Writer rules:

- Use only for concrete non-empty quality strings.
- Fall back to raw strings if any record has `*` or an empty quality string.
- Encode each record independently, so malformed one-record runs cannot bleed
  into the next record.
- Use the codec only when the encoded payload is smaller than the raw
  length-prefixed string payload.
- Do not normalize, cap, bin, offset, or otherwise transform quality bytes.

Reader validation:

- Decode exactly `record_count` records.
- Reject truncated lengths, truncated run lengths, truncated run values,
  zero-length runs, run lengths that exceed the declared record length,
  excessive decoded lengths, decoded length mismatches, and trailing bytes.
- Preserve atomic stdout behavior for malformed payloads.

## Why RLE First

Byte RLE is not the final QUAL compression answer, but it is the right first
implementation:

- It is lossless and self-contained.
- It follows the existing AXF1 pattern of narrow codecs with raw fallback.
- It is easy to unit test against malformed payloads.
- It provides a correctness scaffold for future QUAL-specific codecs without
  introducing entropy coding or lossy policy decisions.

If HG002 small-region QUAL does not use RLE because it is not smaller, that is a
valid result. The codec should still be useful for low-entropy toy data or
datasets with long repeated quality runs.

## Acceptance Criteria for `qual_rle`

- Unit tests cover repeated-quality round-trip and raw fallback for mixed/noisy
  qualities.
- Raw fallback tests cover `*`, empty quality, and non-beneficial payloads.
- Malformed payload tests cover truncated length, truncated run length,
  truncated run value, zero run length, count mismatch, excessive run length,
  and trailing bytes.
- Selected-column read tests prove `read_chunk_columns(..., {quality})` decodes
  RLE QUAL without decoding unrelated columns.
- WSL `ctest` passes.
- Toy `scripts/smoke_axf1_codecs.sh` passes; expected-codec checks should only
  require `qual_rle` for toy regions designed to benefit.
- Remote HG002 smoke is run only after user confirmation and remains
  correctness-only, not benchmark/profiling.

## Recommendation

Implement `qual_rle` as the first QUAL codec with broad raw fallback. Defer
lossy binning, context modeling, FQZComp-like compression, and zstd-wrapped
column payloads until the format has explicit profile and metadata support for
those choices.
