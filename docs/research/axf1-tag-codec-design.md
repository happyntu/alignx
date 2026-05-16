# AXF1 TAG Codec Design: Per-Stream Decomposition

**Date:** 2026-05-16

## Motivation

TAGs are stored as raw SAM-format tab-separated strings (`NM:i:0\tMD:Z:10\tHP:i:1`).
Each record repeats tag keys, types, and separators. For HG002 PacBio haplotag data
with 5-6 tags per record, this overhead is ~30 bytes/record of redundant key/type text
plus ASCII-encoded integers that could be binary varints.

Per-stream decomposition stores tag keys and types once in the header, then encodes
each tag's values in a type-specific stream. Integer tags use zigzag varint encoding;
string-type tags use length-prefixed raw bytes.

## Design: `tags_per_stream` (codec ID 10)

### Wire format

```
[HEADER]
  tag_count                        : varint_u64
  for i=0..tag_count-1:
    tag_key[i]                     : 2 bytes
    tag_type[i]                    : 1 byte

[PRESENCE BITMAPS]
  for i=0..tag_count-1:
    all_present_flag               : 1 byte (0x01=all, 0x00=partial)
    if 0x00:
      bitmap                       : ceil(record_count/8) bytes

[VALUE STREAMS]
  for i=0..tag_count-1:
    present_count                  : varint_u64
    if type == 'i':
      zigzag_varint(value) x present_count
    else:
      (varint_u64(length) + bytes) x present_count
```

### Tag order

Canonical order is determined by the first occurrence of each tag key across
records (scanning sequentially). All records' tags must appear as a subsequence
of the canonical order. If any record violates this, the encoder falls back to raw.

### Encoding algorithm

1. Parse each record's tags string into `(key, type, value)` triples.
2. Build canonical key order from first occurrence of each key.
3. Verify all records' tag sequences are subsequences of canonical order.
4. Build per-tag presence bitmaps and value arrays.
5. Emit header, presence bitmaps, and type-specific value streams.
6. If encoded payload is not smaller than raw, fall back to raw.

### Integer encoding

SAM `i`-type values are parsed to `int64_t` and encoded as zigzag varints:
`zigzag(n) = (n << 1) ^ (n >> 63)`. This maps small-magnitude integers
(positive or negative) to small unsigned values. HP:i:1 encodes as a single
varint byte (value 2) vs 6 bytes of ASCII `HP:i:1`.

### Fallback

Compares per-stream payload size against raw and picks the smallest, matching
the pattern used by all other AXF1 codecs.

## Deferred alternatives

- **String tag dictionary**: Dictionary-encode string-type tags (MD, RG) for
  chunks with many repeated values. Deferred until per-stream baseline is measured.
- **Per-tag delta encoding**: Delta-varint for monotonic integer tags (AS scores).
  Low priority since zigzag varint already handles these well.
