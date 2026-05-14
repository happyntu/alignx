# AXF1 Compressed Payload Wrapper Design

Status: partially implemented, 2026-05-15. AXF1 currently has a
dependency-free `qual_pack_compressed` reader path for a stored payload envelope;
zstd envelope decode is available when built with `ALIGNX_ENABLE_ZSTD=ON`, and
the writer can explicitly emit zstd-wrapped `qual_pack_compressed` payloads for
quality columns. Default writers still emit the smaller base codec directly.

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

- Do not implement LZ4, rANS, arithmetic coding, or zstd dictionaries in this
  note.
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
| 1 | zstd | Implemented reader and explicit quality writer when built with `ALIGNX_ENABLE_ZSTD=ON` |
| 2 | lz4 | Future fast-profile candidate |

The first implemented algorithm is `stored`, which preserves the envelope
semantics without adding a compression dependency. Future zstd/LZ4 support must
reuse the same envelope validation and fallback rules rather than creating
one-off `*_zstd` codec ids.

## Writer Rules

- Produce the base transform payload first.
- Compress the base payload only if the algorithm is available and configured.
- Default writers still emit the smaller base codec directly and never write
  `qual_pack_compressed`.
- With explicit `zstd` quality compression, the writer first computes the
  regular best quality codec, then attempts a zstd `qual_pack_compressed`
  wrapper when `qual_pack` can encode the quality column.
- The writer emits the wrapper only when the complete envelope payload is
  smaller than the regular best quality payload. Otherwise it falls back to the
  regular quality codec selection.
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
  Implemented as a build option.
- When the option is `OFF`, do not search for zstd and do not link zstd.
- When the option is `ON`, detect zstd with CMake config first and pkg-config
  second, mirroring the HTSlib pattern where practical.
- If `ALIGNX_ENABLE_ZSTD=ON` and zstd is unavailable, fail configure with a
  clear message.
- If detected, link `alignx_lib` to the selected zstd target and define
  `ALIGNX_HAVE_ZSTD`.
  Implemented.

This is stricter than HTSlib's current optional behavior because enabling zstd
is an explicit request to produce or consume zstd-compressed AXF1 payloads.

### Reader Behavior

- `compression_id = 1` means zstd-compressed payload bytes inside the existing
  envelope.
- Builds with `ALIGNX_HAVE_ZSTD` decompress the payload, require output size to
  exactly match `uncompressed_size`, then pass the bytes to the base codec
  decoder. Implemented for reader decode.
- Builds without `ALIGNX_HAVE_ZSTD` must reject `compression_id = 1` with a
  clear "unsupported AXF1 compressed payload compression" style error.
  Implemented.
- Existing uncompressed AXF1 files and `stored` envelopes remain readable
  without zstd.
- Reader errors must preserve atomic stdout behavior; no partial view output.

### Writer Behavior

- Do not change default writer output when zstd support lands.
- Add an explicit writer policy and CLI flag; keep the default on the base-codec
  path until correctness and benchmark/profiling work justify changing it.
- Produce the base codec payload first.
- Try zstd only for columns whose wrapper codec id is explicitly supported, such
  as `qual_pack_compressed`.
- Current implementation emits the wrapper when explicitly requested, zstd is
  enabled, `qual_pack` can encode the column, and the complete envelope payload
  is smaller than the regular best quality payload.
- If `qual_pack` cannot encode the column or the zstd envelope is not smaller,
  the writer falls back to the regular best quality codec.
- Builds without zstd reject explicit zstd writer requests clearly.
- Never apply lossy quality-score binning in the zstd wrapper path.

### Test Matrix

- Configure/build with `ALIGNX_ENABLE_ZSTD=OFF`; uncompressed and `stored`
  compressed-envelope tests pass.
- With zstd off, a fixture containing `compression_id = 1` is rejected cleanly.
- Configure/build with `ALIGNX_ENABLE_ZSTD=ON` in an environment that provides
  zstd; zstd envelope round-trip passes for one column.
- Corruption tests cover bad compressed bytes, decompressed-size mismatch, and
  base-codec validation after successful decompression.
- Writer tests cover explicit zstd emission, disabled-build rejection, and
  non-beneficial zstd envelope fallback.
- Metadata tools continue to report wrapper codec names without needing zstd.

### CLI Exposure

The public CLI flag is intentionally explicit:

```bash
alignx convert input.bam -o output.axf1 --format AXF1 --axf1-quality-compression zstd
```

The default remains `--axf1-quality-compression none`, which preserves the
current base-codec selection until benchmark/profiling work justifies a change.

## Why This Before QUAL Context Models

QUAL context models are likely necessary for stronger compression, but they are
format- and validation-heavy. A generic wrapper is a smaller design step because
it can reuse existing lossless transforms, selected-column decode, and raw
fallback rules. It also benefits TAGS, QNAME, CIGAR, and noisy QUAL payloads
without creating separate entropy-coding decisions for every column.

## Remote HG002 Smoke

Remote HG002 zstd quality writer correctness smoke on 2026-05-15 used:

- Host: `missmi-server00`
- Input:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- Region: `chr1:1000000-1010000`
- Work directory:
  `/mypool/alignx/tmp/axf1_zstd_quality_writer_smoke_hg002_chr1_1000000_1010000_20260515`
- Command shape:
  `alignx convert --format AXF1 --region chr1:1000000-1010000 --axf1-quality-compression zstd`

The smoke compared `alignx view` on the zstd AXF1 output, `alignx view` on the
source BAM, and `samtools view` on the source BAM. All three SAM outputs had
SHA-256 `6caf2d4a3142f62d51d3f4d64216de1372ebe3c629dbbc95581f1cd71f815389`
for 64 records.

Metadata-only inspection reported `quality` as `qual_pack_compressed` on all 7
chunks. Payload summary for the zstd AXF1 output:

```text
quality:  qual_pack_compressed:7, 533,367 bytes, 67.369% of column payload bytes
sequence: seq_2bit_literal:7, 227,010 bytes, 28.673% of column payload bytes
cigar:    cigar_token:7, 20,689 bytes, 2.613% of column payload bytes
```

The AXF1 output was 798,039 bytes. This is a correctness smoke, not a benchmark
or a general compression-ratio claim.

After adding the writer size policy, the same remote correctness smoke was rerun
on 2026-05-15 with binary
`/mypool/alignx/bin/alignx_zstd_quality_size_policy_3e7f980` and work directory
`/mypool/alignx/tmp/axf1_zstd_quality_size_policy_smoke_hg002_chr1_1000000_1010000_20260515`.
The result remained byte-identical with the same stdout SHA-256, `quality` still
used `qual_pack_compressed` on all 7 chunks, and the AXF1 output remained
798,039 bytes. This confirms that the size-policy fallback keeps the zstd
wrapper for this region because it is smaller than the regular best quality
payload.

## Acceptance Criteria for a Future Implementation

- Unit tests cover a compressed payload round-trip for at least one column.
  Implemented for `qual_pack_compressed` with `stored` and zstd.
- Unit tests cover default raw/base writer behavior, explicit zstd writer
  behavior, and non-beneficial zstd fallback.
- Malformed tests cover truncated envelope, unknown compression id,
  uncompressed-size mismatch, compressed-size mismatch, decompressor failure,
  and trailing bytes. Implemented for truncated envelope fields, unsupported
  compression, stored-size mismatch, truncated stored bytes, trailing bytes,
  unsupported base codec, corrupt zstd payloads, and zstd decompressed-size
  mismatch.
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
