# AXF1 SEQ Codec Design

Status: design note, 2026-05-14. No format change is implemented by this
note.

## Goals

- Preserve lossless reconstruction of the SAM `SEQ` field.
- Keep chunk-local independent decode, matching the existing AXF1 codec model.
- Keep raw fallback for any sequence that is not a good candidate.
- Avoid adding a required external reference dependency to decode the first SEQ
  codec.
- Preserve AXF1 v2 compatibility expectations: new codec ids can be added at
  the column level, and readers must reject unsupported required codec ids
  instead of silently mis-decoding payloads.

## Non-Goals

- Do not implement reference-delta SEQ compression as the first SEQ codec.
- Do not require a FASTA file to read current AXF1 output.
- Do not compress `QUAL`, `CIGAR`, or `TAGS` in this design.
- Do not make compression-ratio or performance claims before benchmark work is
  explicitly scheduled.

## Current State

The AXF1 `SEQ` column is currently stored as raw length-prefixed strings. The
first column codecs already implemented are deliberately narrow:

- `POS`: delta varint with raw fallback for non-monotonic chunks.
- `FLAG`: bit-pack with raw fallback when bit-packing is not smaller.
- `MAPQ`: run-length encoding with raw fallback when RLE is not smaller.

`SEQ` should follow the same conservative pattern: implement a simple,
self-contained, correctness-gated codec first, then consider reference-aware
compression only after the metadata and alignment semantics are designed.

## First Implementation Candidate: `seq_2bit_literal`

The recommended first SEQ codec is a chunk-local 2-bit literal codec for
uppercase `A`, `C`, `G`, and `T` sequences.

Payload shape:

```text
Seq2BitLiteralPayload:
  record repeated record_count

record:
  length      uvarint
  bases       ceil(length * 2 / 8) bytes
```

Base mapping:

| Base | Bits |
|---|---:|
| `A` | 0 |
| `C` | 1 |
| `G` | 2 |
| `T` | 3 |

Pack bases LSB-first within each record. Padding bits in the final byte are
ignored on decode but should be written as zero for deterministic output.

Writer rules:

- Use the codec only when every record sequence is uppercase `A/C/G/T`.
- Fall back to raw strings if any sequence is `*`, empty where not expected, has
  `N`, IUPAC ambiguity codes, lowercase bases, or any other non-ACGT byte.
- Fall back to raw strings if the encoded payload is not smaller than the raw
  string column payload.
- Do not normalize or transform the stored SAM/BAM sequence. Decode must
  reconstruct exactly the same `SEQ` field that `alignx view` would print.

Reader validation:

- Decode exactly `record_count` records.
- Reject truncated varints, truncated packed bases, excessive decoded lengths,
  trailing bytes, and decoded record-count mismatches.
- Keep malformed payload errors atomic with respect to stdout, matching the
  existing AXF1 view contract.

This codec is useful because it is self-contained and lossless. It should also
usually beat raw length-prefixed strings for long-read `A/C/G/T` data without
introducing the operational risks of reference-dependent decode.

## Why Not Reference-Delta First

Reference-delta SEQ compression is attractive, but it needs more format design
than a literal 2-bit codec:

- AXF1 v2 `source_path` is only an audit hint. It is not a stable reference
  identity mechanism.
- A reference-aware codec needs reference dictionary, contig lengths, checksums
  or hashes, and preferably BAM/SAM header identity metadata.
- Decode behavior must be clear when the FASTA is missing, moved, different, or
  unavailable on another machine.
- CIGAR semantics must be handled exactly: insertions, deletions, skips, soft
  clips, hard clips, padding, mismatches, and reference skips cannot be treated
  as a simple byte diff.
- Strand/orientation rules must be explicit. The stored `SEQ` value must remain
  byte-identical to SAM output, not merely equivalent to the reference-forward
  alignment.
- Optional tags such as `MD` and `NM` are useful hints, but they are not enough
  to define a robust, standalone reconstruction contract.
- Tests must cover mismatches, `N`, indels, clipping, reverse-strand records,
  secondary records, and supplementary records before a reference-delta codec is
  trusted.

For these reasons, reference-delta should be deferred until AXF1 has accepted
reference identity metadata and an explicit CIGAR/strand reconstruction design.

## Future Reference-Delta Direction

If reference-delta SEQ compression is added later, it should be opt-in per
column/chunk and retain raw or literal fallback for records that are poor
candidates.

Likely prerequisites:

- typed metadata for reference identity, such as reference name, per-contig
  length/checksum, FASTA URI or path as a hint, and BAM/SAM header SHA-256;
- an explicit rule for whether the codec is self-contained or requires the
  external reference for decode;
- CIGAR-driven reconstruction tests for all operation types used by supported
  alignments;
- remote smoke checks proving byte-identical `alignx view` stdout against both
  BAM-backed alignx output and `samtools view`.

## Acceptance Criteria for `seq_2bit_literal`

- Unit tests cover ACGT round-trip, raw fallback for ambiguity/lowercase/`*`,
  malformed payload rejection, and selected-column view parity.
- WSL `ctest` passes.
- `scripts/smoke_axf1_codecs.sh` passes on toy data and the remote HG002
  correctness region when the user has approved real-data smoke work.
- `scripts/inspect_axf1_metadata.py --column-codecs` reports the SEQ codec
  distribution by readable name.

## Recommendation

Implement `seq_2bit_literal` before any reference-delta SEQ codec. Defer
reference-delta until reference identity metadata and exact CIGAR/strand
semantics are designed and accepted.
