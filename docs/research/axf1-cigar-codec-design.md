# AXF1 CIGAR Codec Design

Status: implemented for `cigar_token`, 2026-05-14. Dictionary, delta, and
reference-aware CIGAR codecs remain design-only.

## Goals

- Preserve byte-identical reconstruction of the SAM `CIGAR` field.
- Keep chunk-local independent decode, matching the AXF1 codec model used by
  `POS`, `FLAG`, `MAPQ`, and `SEQ`.
- Support selective `POS + CIGAR` decode for `alignx view` region filtering.
- Keep raw fallback for `*`, malformed input strings, unsupported operations,
  and non-beneficial payloads.
- Avoid any reference-dependent semantics in the first CIGAR codec.

## Non-Goals

- Do not implement a reference-delta or alignment-normalizing CIGAR codec.
- Do not infer sequence edits, `MD` tags, or `NM` tags.
- Do not canonicalize equivalent CIGAR strings. The decoded string must match
  the stored string exactly.
- Do not make compression-ratio or performance claims before benchmark work is
  explicitly scheduled.

## Current State

AXF1 currently stores `CIGAR` as raw length-prefixed strings. The query path
already depends on CIGAR selectively:

- `alignx view` first decodes only `POS` and `CIGAR` for overlapping chunks.
- `query::cigar_reference_span()` computes the reference span from the CIGAR
  string.
- Only records whose `POS` plus CIGAR reference span overlaps the query region
  trigger full output-column decode.
- The final SAM output prints the CIGAR string directly.

This means a CIGAR codec must support fast selected-column decode and exact
string reconstruction. It must not weaken the current malformed-CIGAR error
behavior or atomic stdout contract.

Implementation status: AXF1 now writes `cigar_token` for chunks whose stored
CIGAR strings are valid, concrete, non-empty, non-`*`, have supported SAM
operations, and produce a smaller payload than raw length-prefixed strings. It
falls back to raw strings for `*`, empty CIGAR, unsupported operations, missing
lengths, trailing numeric text, zero lengths, leading-zero lengths, overflow,
or non-beneficial payloads. WSL `ctest` and toy
`scripts/smoke_axf1_codecs.sh` coverage were checked on 2026-05-14.
The remote HG002 chr1 small-region smoke also passed on 2026-05-15, with
`cigar_token` used on all 7 chunks.

Toy CIGAR token smoke, run locally in WSL on 2026-05-14:

- BAM: `tests/toy_data/toy_alignment.sorted.bam`
- Region: `chrToy:1-250`
- Output directory: `/tmp/alignx_axf1_cigar_codec_smoke`
- Records: 2
- `axf1 view`, `alignx view` on BAM, and `samtools view` stdout SHA256:
  `e62402c0450decf357ef797750af1dfb0be065eeeb3b87f157953a7f7ae1feb9`
- Codec distribution: `cigar_token`, `seq_2bit_literal`, `pos_delta_varint`,
  and `flag_bitpack` on the toy chunk. Toy MAPQ remained raw because RLE was
  not smaller for this input.

This was a correctness smoke only, not a benchmark or profiling run.

Remote HG002 CIGAR token smoke, run on `missmi-server00` on 2026-05-15:

- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr1:1000000-1010000`
- Output directory:
  `/mypool/alignx/tmp/axf1_cigar_codec_smoke_hg002_chr1_1000000_1010000_20260515`
- AXF1 output: `sample.axf1`
- AXF1 file size: 1,172,296 bytes
- Records: 64
- `axf1 view`, `alignx view` on BAM, and `samtools view` stdout SHA256:
  `6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`
- Codec distribution: `cigar_token`, `seq_2bit_literal`, `pos_delta_varint`,
  `flag_bitpack`, and `mapq_rle` on all 7 chunks.

This was a correctness smoke only, not a benchmark or profiling run.

## First Implementation Candidate: `cigar_token`

The recommended first codec is a self-contained token stream:

```text
CigarTokenPayload:
  record repeated record_count

record:
  op_count    uvarint
  op repeated op_count:
    length    uvarint
    op_code   u8
```

Suggested operation mapping:

| Operation | Code |
|---|---:|
| `M` | 0 |
| `I` | 1 |
| `D` | 2 |
| `N` | 3 |
| `S` | 4 |
| `H` | 5 |
| `P` | 6 |
| `=` | 7 |
| `X` | 8 |

Writer rules:

- Parse each CIGAR string as decimal positive length followed by one supported
  operation.
- Use the codec only when every record has a valid, non-`*`, non-empty CIGAR.
- Fall back to raw strings for `*`, empty CIGAR, zero-length operations,
  trailing numeric text, missing lengths, unsupported operations, integer
  overflow, or any payload that is not smaller than raw length-prefixed strings.
- Reconstruct exactly the same decimal length spelling produced by the stored
  CIGAR. Because BAM/SAM CIGAR lengths have no leading-zero semantic form in
  normal output, the writer should reject or raw-fallback leading-zero lengths
  unless existing upstream code can prove they cannot appear.

Reader validation:

- Decode exactly `record_count` records.
- Reject truncated `op_count`, truncated operation length, truncated op code,
  unknown op code, zero operation length, excessive length, excessive operation
  count, and trailing bytes.
- Reconstruct CIGAR strings using canonical decimal lengths and operation bytes.
- Preserve atomic stdout behavior for malformed payloads.

## Why Not CIGAR Delta First

CIGAR delta or dictionary schemes may eventually give better compression, but
they should not be the first implementation:

- Delta coding across records can complicate chunk independence and random
  access if not carefully bounded.
- Common-pattern dictionaries require codec metadata and dictionary reset rules.
- CIGAR normalization can accidentally change byte output even when alignment
  semantics are equivalent.
- `alignx view` already uses `CIGAR` on the filtering path, so simple and robust
  decode is more important than ratio in this phase.

The token stream is a better first step because it is self-contained,
deterministic, and easy to validate.

## Acceptance Criteria for `cigar_token`

- Unit tests cover `10M`, mixed operations such as `5M1I4M`, all supported SAM
  operations, and exact round-trip output.
- Raw fallback tests cover `*`, empty CIGAR, unsupported operation, missing
  length, trailing numeric text, zero operation length, and leading-zero lengths
  if they can reach the writer.
- Malformed encoded payload tests cover truncated varints, unknown op codes,
  zero lengths, operation-count mismatches, and trailing bytes.
- Selected-column read tests prove `read_chunk_columns(..., {cigar})` decodes
  tokenized CIGAR without decoding output columns.
- WSL `ctest` passes.
- Toy `scripts/smoke_axf1_codecs.sh` passes and inspector reports
  `cigar_token` when the toy payload benefits.
- Remote HG002 smoke is run only after user confirmation and remains
  correctness-only, not benchmark/profiling.

## Second Implementation: `cigar_dict`

Status: implemented 2026-05-16.

`cigar_dict` (codec ID 11) stores a sorted chunk-local dictionary of unique
CIGAR strings plus a per-record varint index stream. The encoder tries raw,
`cigar_token`, and `cigar_dict`, picking the smallest payload.

```text
CigarDictPayload:
  dict_count          varint_u64
  entry repeated dict_count:
    entry_length      varint_u64
    entry_bytes       entry_length bytes
  index repeated record_count:
    dict_index        varint_u64
```

No front compression — CIGAR strings do not share meaningful prefixes.

`cigar_dict` is effective for Illumina-style data where many records share
the same CIGAR pattern (e.g., `151M`). For PacBio long reads with unique
CIGARs per record, `cigar_dict` is never smaller than `cigar_token` and the
encoder falls back automatically.

HG002 PacBio chr1:1M-2M smoke (2026-05-16): 324/324 chunks kept
`cigar_token` as expected. Three-way SAM stdout SHA parity confirmed.

## Recommendation

Keep raw fallback broad and preserve exact SAM CIGAR output semantics.
`cigar_token` handles per-record tokenization; `cigar_dict` handles
repeated-pattern deduplication. Reference-delta CIGAR codecs remain deferred.
