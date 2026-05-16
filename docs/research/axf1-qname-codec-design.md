# AXF1 QNAME Codec Design: Delta-Dictionary with Front Compression

**Date:** 2026-05-16

## Motivation

QNAME is currently stored as a raw string array (u32 length + bytes per record).
In typical genomics data, QNAMEs share long common prefixes:

- PacBio CCS: `m64011_190830_220126/45/ccs` — 21+ char shared movie prefix
- Illumina: `HWI-ST808:131:D099CACXX:1:1101:1234:5678` — shared instrument/run/lane

A chunk of 4096 records with 30-byte average QNAMEs stores ~139 KB raw. Front
compression of sorted unique names can reduce this to ~53 KB (2.6x).

## Design: `qname_dict` (codec ID 9)

### Wire format

```
[DICTIONARY]
  dict_size                           : varint_u64
  entry[0]                            : varint_u64(length) + bytes
  entry[i>0]                          : varint_u64(shared_prefix_len) + varint_u64(suffix_len) + suffix_bytes

[INDICES]
  index[j] for j=0..record_count-1   : varint_u64 (index into sorted dictionary)
```

### Encoding algorithm

1. Collect unique QNAMEs using `std::map<string_view, u32>` (auto-sorted).
2. Assign sorted indices 0..N-1.
3. Front-compress sorted dictionary: entry 0 = full string; entry i>0 =
   shared prefix length with entry i-1, then suffix.
4. Append per-record varint indices mapping each record to its dictionary entry.
5. If encoded payload is not smaller than raw, fall back to raw.

### Fallback

The writer compares dict payload size against raw and picks the smallest, matching
the pattern used by all other AXF1 codecs (POS delta-varint, FLAG bit-pack, etc.).

## Deferred alternatives

- **QNAME tokenizer**: Split by `:` or `/`, encode each field as a separate
  stream (instrument dictionary, coordinate varints, etc.). Higher complexity,
  deferred until dictionary compression is measured.
- **zstd wrapper**: Wrap dict payload in the existing compressed envelope.
  Can be added later if dictionary alone is insufficient.
