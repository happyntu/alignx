# AXF1 Cloud-Ready Index Assessment

**Date:** 2026-05-16

## Summary

The AXF1 on-disk format already satisfies the v0.4 "HTTP-range-friendly chunk
map" requirement. No format changes are needed. The remaining work is an HTTP
client transport layer, which is deferred to Phase 2+.

## Evidence

### File-absolute byte offsets

Each `Axf1ChunkIndexEntry` stores:
- `chunk_offset`: absolute byte position in the file (not a BGZF virtual offset)
- `chunk_length`: exact byte size of the chunk payload

A single HTTP Range header retrieves one chunk:
`Range: bytes={chunk_offset}-{chunk_offset+chunk_length-1}`

### Contiguous chunk layout

Chunks are written sequentially in file order. There are no gaps, no interleaved
metadata, and no BGZF block boundaries to navigate. Adjacent chunks can be
coalesced into a single range request.

### Index at EOF

The file header stores `index_offset`, pointing to the chunk index at the end of
the file. A client can:
1. Read the fixed 28-byte header to learn `index_offset` and `chunk_count`.
2. Fetch `[index_offset, file_size)` in one range request to load the full index.
3. Select overlapping chunks and fetch each with targeted range requests.

### Selective column I/O

`read_chunk_columns_selective()` computes precise byte ranges for individual
column payloads within a chunk. This enables sub-chunk range requests when only
specific columns (e.g., POS+CIGAR for coverage) are needed.

## What remains (Phase 2+)

- HTTP/S3/GCS client abstraction in `Axf1FileReader` (currently uses `std::ifstream`)
- Range request coalescing for multi-chunk queries
- Retry and caching policy for cloud latency
- Authentication and credential management for cloud providers

## Design references

- ADR-003 §v1.0+ describes the target cloud-friendly index
- `docs/research/axf1-minimal-columnar-design.md` notes that offsets should
  remain file-absolute and chunk-local for future cloud support
- `docs/research/axf1-chunk-sizing-policy.md` sizes chunks (256 KiB target) to
  fit efficiently in single HTTP range requests
