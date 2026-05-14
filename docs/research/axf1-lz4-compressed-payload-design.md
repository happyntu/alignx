# AXF1 LZ4 Compressed Payload Decision

Status: deferred, 2026-05-15.

## Context

AXF1 now has a generic compressed payload envelope and an implemented zstd path
for `qual_pack_compressed` quality payloads:

- `compression_id = 0`: stored envelope, dependency-free reader validation path
- `compression_id = 1`: zstd, optional `ALIGNX_ENABLE_ZSTD`, reader and explicit
  quality writer with size-based fallback
- `compression_id = 2`: reserved for LZ4 as a future fast-profile candidate

The open question is whether LZ4 should be implemented next.

## Comparison

| Candidate | Strength | Cost / Risk |
|---|---|---|
| zstd | Good compression ratio, already integrated behind an optional feature gate, remote correctness smoke passed | Decode cost may affect query latency and needs measurement before default use |
| LZ4 | Very fast decode and likely useful for a future latency-first profile | Weaker compression ratio; adds another optional dependency, CMake path, reader/writer tests, CLI policy, and remote smoke matrix |
| QUAL context model | More likely to improve long-read quality streams than another generic byte compressor | Larger format and validation design; must preserve lossless reconstruction and selected-column decode |

## Decision

Do not implement LZ4 immediately.

Keep `compression_id = 2` reserved for LZ4, but treat it as a deferred
fast-profile option. The current zstd implementation already proves the generic
envelope, optional dependency gate, writer size fallback, selected-column read,
and remote correctness smoke path. Adding LZ4 now would mostly duplicate that
integration work without profiling data showing that zstd decode is a bottleneck
for AXF1 region queries.

## Rationale

- AXF1's near-term objective is region-query correctness and eventually query
  latency, not accumulating compression algorithms.
- zstd already provides the first real compressed payload implementation and
  reduces the HG002 chr1 small-region quality payload under an explicit writer
  option.
- LZ4 is plausible for a future `fast` profile, but should be justified by
  profiling zstd-wrapped AXF1 query/decode behavior first.
- If the remaining size problem is primarily QUAL entropy, a QUAL-specific
  context model is likely more valuable than adding another generic byte
  compressor.

## Future Activation Criteria

Implement LZ4 only if at least one of these becomes true:

- zstd-wrapped AXF1 correctness is stable, but profiling shows zstd decode is a
  material bottleneck for common region-query or selected-column workloads.
- A latency-first profile is introduced where file size is secondary and decode
  speed is the dominant requirement.
- A target column has transformed payloads where LZ4 wins the size/speed tradeoff
  compared with raw/base and zstd under measured workloads.

If implemented later, LZ4 should reuse the existing envelope rules:

- optional `ALIGNX_ENABLE_LZ4`, default `OFF`;
- `compression_id = 2`;
- clear unsupported-compression errors when disabled;
- writer size fallback against the regular best base payload;
- no default writer behavior change until correctness and benchmark/profiling
  evidence justify it.

## Roadmap Impact

LZ4 is not the next implementation target. Prefer one of these next:

- observe zstd-wrapped AXF1 query/decode behavior when benchmark/profiling is
  explicitly scheduled; or
- design the next lossless QUAL-specific compression step.
