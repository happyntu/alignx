# AXF1 Compressed Payload Wrapper Design

Status: partially implemented, 2026-05-15. AXF1 currently has a
dependency-free `qual_pack_compressed` reader path for a stored payload envelope;
writers still emit the smaller base codec directly and do not apply zstd/LZ4.

## Motivation

AXF1 now has several lossless, chunk-local column transforms:
`pos_delta_varint`, `flag_bitpack`, `mapq_rle`, `cigar_token`,
`seq_2bit_literal`, `qual_rle`, and `qual_pack`.

`qual_pack` improved the HG002 chr1 small-region QUAL payload, but QUAL remains
the dominant payload column:

```text
work_dir: /mypool/alignx/tmp/axf1_qual_pack_smoke_hg002_chr1_1000000_1010000_20260515
region:   chr1:1000000-1010000
records:  64
stdout:   6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389
quality:  qual_pack:7, 794,762 bytes, 75.468% of column payload bytes
```

The next compression step should not be a one-off `quality_zstd` codec id.
Many columns may benefit from a generic post-transform compression layer, but
AXF1 must preserve chunk-local random access, selected-column decode, and clear
unsupported-codec behavior.

## Goals

- Preserve lossless, byte-identical reconstruction.
- Preserve chunk-local independent decode and selected-column reads.
- Allow any column transform payload to be optionally wrapped by a generic
  compression envelope.
- Keep raw fallback for non-beneficial compressed payloads.
- Keep unsupported required compression easy to reject before partial output.
- Define format semantics before adding a dependency such as zstd.

## Non-Goals

- Do not implement zstd, LZ4, rANS, or arithmetic coding in this note.
- Do not introduce lossy quality-score binning.
- Do not implement FQZComp-like QUAL context modeling.
- Do not make compression-ratio or throughput claims before scheduled
  benchmark/profiling work.
- Do not require GPU or SIMD to decode compressed payloads.

## Design Options

| Option | Strength | Risk / Cost | Decision |
|---|---|---|---|
| One codec id per compressed column, e.g. `qual_pack_zstd` | Simple reader dispatch | Explodes codec ids and duplicates wrapper semantics | Reject |
| Add compression flags to `ColumnEntry` | Compact metadata | Changes the fixed column-entry layout and v2 reader assumptions | Defer |
| Payload envelope inside the existing payload bytes | Avoids column-entry layout changes; works with existing offset/length | Needs one wrapper codec id per base column codec or a wrapper marker | Recommended |
| File-level compression | Smaller files | Breaks selected-column random access | Reject |
| Chunk-level compression | Simple | Forces all columns in a chunk to be decompressed together | Reject for AXF1 columnar goals |

## Recommended Direction: Payload Envelope

The recommended direction is a payload envelope that lives inside the existing
column payload byte range. The `ColumnEntry` still identifies one required codec
id, and the payload begins with a small envelope header when that codec supports
wrapping.

For v2 compatibility, this requires assigning explicit wrapper codec ids rather
than changing the existing `ColumnEntry` structure. A wrapper codec id is still
column-specific in what it decodes, but the envelope structure and compression
algorithm registry are shared.

Example codec id naming:

```text
qual_pack_compressed
cigar_token_compressed
tags_raw_compressed
```

The repeated suffix is less elegant than a `ColumnEntry` flag, but it keeps the
current chunk metadata layout stable. A future AXF1 v3 can move compression
into column-entry flags if the wrapper proves useful.

## Payload Shape

```text
CompressedPayload:
  base_codec_id       uvarint
  compression_id      uvarint
  uncompressed_size   uvarint
  compressed_size     uvarint
  compressed_bytes    repeated compressed_size u8
```

Rules:

- `base_codec_id` is the transform payload that appears after decompression,
  such as `qual_pack` or `cigar_token`.
- `compression_id` is from a small registry.
- `uncompressed_size` is required so the reader can preflight allocations.
- `compressed_size` must equal the remaining payload bytes.
- Decompressed bytes are passed to the existing base codec decoder.

Initial compression registry:

| id | Name | Status |
|---:|---|---|
| 0 | stored | Implemented dependency-free validation path; not emitted by default writers |
| 1 | zstd | Planned optional feature-gated implementation |
| 2 | lz4 | Future fast-profile candidate |

The first implemented algorithm is `stored`, which preserves the envelope
semantics without adding a compression dependency. Future zstd/LZ4 support must
reuse the same envelope validation and fallback rules rather than creating
one-off `*_zstd` codec ids.

## Writer Rules

- Produce the base transform payload first.
- Compress the base payload only if the algorithm is available and configured.
- Use the compressed wrapper only if total envelope payload is smaller than the
  base payload.
- Fall back to the base codec if compression is unavailable, unsupported for the
  selected profile, or non-beneficial.
- Current writers do this unconditionally: they still emit the smaller base
  codec directly and never write `qual_pack_compressed`.
- Never apply lossy transforms in this wrapper.
- Keep each column payload independently compressed; do not share dictionaries
  across columns or chunks in the first implementation.

## Reader Validation

- Reject truncated envelope fields.
- Reject unknown `base_codec_id`.
- Reject unknown or unsupported `compression_id`.
- Reject `uncompressed_size` above implementation limits.
- Reject `compressed_size` mismatch or trailing bytes.
- Reject decompressor output size mismatch.
- Decode the decompressed bytes with the existing base codec validation.
- Preserve atomic stdout behavior for malformed compressed payloads.

## Dependency Policy

Compression algorithms are optional implementation dependencies, not format
requirements for uncompressed AXF1 files.

Before enabling zstd:

- CMake must detect zstd explicitly using the existing optional-dependency
  pattern: try `find_package(zstd CONFIG QUIET)` first, then a pkg-config
  fallback if needed.
- Builds without zstd must still read uncompressed AXF1 files.
- Builds without zstd must reject zstd-wrapped required columns with a clear
  error, not silently fall back.
- Test coverage must include both supported and unsupported-compression paths if
  practical.

## zstd Integration Mini-Design

The first real compression algorithm should be zstd behind an explicit feature
gate. It must not make zstd a required dependency for normal AXF1 builds.

### CMake Policy

- Add an `ALIGNX_ENABLE_ZSTD` option, default `OFF`.
- When the option is `OFF`, do not search for zstd and do not link zstd.
- When the option is `ON`, detect zstd with CMake config first and pkg-config
  second, mirroring the HTSlib pattern where practical.
- If `ALIGNX_ENABLE_ZSTD=ON` and zstd is unavailable, fail configure with a
  clear message.
- If detected, link `alignx_lib` to the selected zstd target and define
  `ALIGNX_HAVE_ZSTD`.

This is stricter than HTSlib's current optional behavior because enabling zstd
is an explicit request to produce or consume zstd-compressed AXF1 payloads.

### Reader Behavior

- `compression_id = 1` means zstd-compressed payload bytes inside the existing
  envelope.
- Builds with `ALIGNX_HAVE_ZSTD` decompress the payload, require output size to
  exactly match `uncompressed_size`, then pass the bytes to the base codec
  decoder.
- Builds without `ALIGNX_HAVE_ZSTD` must reject `compression_id = 1` with a
  clear "unsupported AXF1 compressed payload compression" style error.
- Existing uncompressed AXF1 files and `stored` envelopes remain readable
  without zstd.
- Reader errors must preserve atomic stdout behavior; no partial view output.

### Writer Behavior

- Do not change default writer output when zstd support lands.
- Add a hidden/internal writer policy first, not a user-facing promise, so
  correctness and compatibility are validated before CLI exposure.
- Produce the base codec payload first.
- Try zstd only for columns whose wrapper codec id is explicitly supported, such
  as `qual_pack_compressed`.
- Emit the wrapper only when `envelope_size + zstd_payload_size` is smaller than
  the base payload.
- Fall back to the base codec when zstd is unavailable, disabled, unsupported
  for that column, or non-beneficial.
- Never apply lossy quality-score binning in the zstd wrapper path.

### Test Matrix

- Configure/build with `ALIGNX_ENABLE_ZSTD=OFF`; uncompressed and `stored`
  compressed-envelope tests pass.
- With zstd off, a fixture containing `compression_id = 1` is rejected cleanly.
- Configure/build with `ALIGNX_ENABLE_ZSTD=ON` in an environment that provides
  zstd; zstd envelope round-trip passes for one column.
- Corruption tests cover bad compressed bytes, decompressed-size mismatch, and
  base-codec validation after successful decompression.
- Writer tests cover smaller-than-base emission and non-beneficial fallback.
- Metadata tools continue to report wrapper codec names without needing zstd.

### CLI Exposure

Do not add a public compression flag until toy and remote correctness smokes are
stable. A future user-facing flag should be explicit, for example
`--axf1-compression zstd`, and the default should remain the current base-codec
selection until benchmark/profiling work justifies a change.

## Why This Before QUAL Context Models

QUAL context models are likely necessary for stronger compression, but they are
format- and validation-heavy. A generic wrapper is a smaller design step because
it can reuse existing lossless transforms, selected-column decode, and raw
fallback rules. It also benefits TAGS, QNAME, CIGAR, and noisy QUAL payloads
without creating separate entropy-coding decisions for every column.

## Acceptance Criteria for a Future Implementation

- Unit tests cover a compressed payload round-trip for at least one column.
  Implemented for `qual_pack_compressed` with `stored`.
- Unit tests cover raw/base fallback when compression is not smaller.
  Implemented by keeping writers on the base codec path.
- Malformed tests cover truncated envelope, unknown compression id,
  uncompressed-size mismatch, compressed-size mismatch, decompressor failure,
  and trailing bytes. Implemented for truncated envelope fields, unsupported
  compression, stored-size mismatch, truncated stored bytes, trailing bytes, and
  unsupported base codec. Decompressor failure remains future zstd/LZ4 work.
- Selected-column tests prove only requested compressed columns are decompressed.
  Implemented for `quality`.
- Python metadata tools report wrapper codec names. Implemented for
  `qual_pack_compressed`.
- WSL `ctest` passes. Verified on 2026-05-15 with `ctest --preset wsl-debug`.
- Toy smoke remains byte-identical to BAM and samtools stdout.
- Remote correctness smoke is run after user confirmation and remains
  correctness-only, not benchmark/profiling.

## Deferred Work

- AXF1 v3 `ColumnEntry` compression flags.
- Shared dictionaries across chunks.
- zstd dictionary training.
- rANS/arithmetic coding for specific streams.
- QUAL-specific FQZComp-like context models.
- Lossy quality-score binning under an explicit lossy profile.
