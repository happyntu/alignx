# Project Goals

## One-line positioning

> A high-performance, C++23-based alignment storage and query engine that remains compatible
> with SAM/BAM/CRAM, while introducing a GPU/SIMD-friendly columnar format (`.axf`) and a
> next-generation index for faster genomic region queries and downstream analysis.

## What alignx is NOT (in v0.x)

- Not a full replacement for samtools — it is a complement and accelerator.
- Not guaranteed to compress smaller than CRAM — lossless QUAL is hard to beat.
- Not a new aligner — it stores and queries alignments, it does not create them.

## Success criteria per version

| Version | Success criterion |
|---|---|
| v0.1 | `alignx view bam region` faster than `samtools view` on large BAM |
| v0.2 | `alignx pileup` coverage matches samtools depth exactly |
| v0.3 | AXF coverage (POS only) 2x faster than BAM full-record parse |
| v0.4 | BAM → AXF → BAM round-trip: zero diff on SAM output |
| v1.0 | Published benchmark: AXF vs BAM vs CRAM across 4 workloads |

## Target users

| User | Pain point alignx solves |
|---|---|
| Pipeline engineer | Repeatedly querying same BAM regions → AXF as accelerated cache |
| Cloud genomics | Object storage random access cost → HTTP-range-friendly AXF index |
| Methods developer | Needs fast coverage/pileup for variant caller → POS-only columnar read |
| GPU computing | Wants GPU-accelerated decompression → SIMD/CUDA-friendly layout (Phase 3+)|
