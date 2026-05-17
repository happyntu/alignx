# AXF1 QUAL AVX2 Decode Investigation

**Date:** 2026-05-17
**Status:** Negative result — no improvement

## Hypothesis

AVX2 SIMD can accelerate `qual_pack` bit-extraction in `decode_qual_pack_flat()`. The hot loop extracts `bit_width`-bit codes from a packed byte stream using per-value uint64 loads, shifts, and masks.

## Approaches Tested

### 1. PSHUFB + LUT (SIMD intrinsics)

16 values per iteration using two overlapping uint64 loads, scalar shift extraction, then `_mm_shuffle_epi8` for LUT-based alphabet lookup (alphabet ≤ 16) or scalar LUT for larger alphabets.

**Result:** 14-19% slower than baseline.

| Region | Baseline | PSHUFB+LUT |
|:---|---:|---:|
| chr1:1M-2M | 44 ms | 50 ms |
| chrY:20M-21M | 35 ms | 41 ms |
| chr1:121M-142M | 854 ms | 1012 ms |

**Root cause:** HG002 PacBio quality alphabets have ~70 unique values per chunk (bit_width=7), so the PSHUFB path (limited to 16-entry LUT) is never used. The 256-byte LUT fallback adds per-record `memset(256)` + copy overhead that pollutes L1 cache.

**Bug found:** The batch OR-based validation `(c0 | c1 | ... | c15) >= asize` produces false positives when individual valid codes OR to a value ≥ alphabet size. Must use max-based validation instead.

### 2. Unrolled scalar (8 values per uint64 load)

For bit_width=7: load one uint64, extract 8 codes with fixed shifts (0, 7, 14, ..., 49), advance pointer by 7 bytes. No SIMD intrinsics.

**Result:** Neutral (within noise).

| Region | Baseline | Unrolled |
|:---|---:|---:|
| chr1:1M-2M | 44 ms | 45 ms |
| chrY:20M-21M | 35 ms | 36 ms |
| chr1:121M-142M | 854 ms | 949 ms |

**Root cause:** The compiler already optimizes the original loop effectively. Out-of-order execution handles the `bit_pos` dependency chain. Branch prediction handles the always-false validation check. The per-value `memcpy` compiles to a single unaligned load. The alphabet vector (~70 bytes) stays hot in L1.

### 3. General unrolled for arbitrary bit_width (NOT deployed)

Bug identified: for bit_width values where `vals_per_word * bit_width` is not a multiple of 8 (e.g., bit_width=3, 5, 6), the byte advancement `(vals_per_word * bit_width) / 8` truncates, causing bit misalignment on subsequent iterations. Only bit_width = 2, 4, 8 produce correct results with this approach. Was never deployed because HG002 uses bit_width=7 which takes the specialized path.

## Key Findings

1. **QUAL decode is NOT the bottleneck.** At ~1 GB/s effective throughput (centromeric), the pipeline is limited by SAM formatting and string output, not bit-extraction.

2. **Alphabet size matters for SIMD strategy.** PSHUFB-based LUT only works for alphabet ≤ 16. PacBio quality data typically has 60-80 unique values per chunk, ruling out this approach. Illumina data with lossy binning (8 values) would benefit, but lossy mode shifts codec selection to `qual_rle` anyway.

3. **Compiler auto-vectorization with `-mavx2`** may provide marginal benefit across the entire binary without explicit intrinsics. Not measured in isolation.

## Recommendation

Keep the original scalar loop in `decode_qual_pack_flat()`. Future SIMD work should target higher-ROI paths:
- **SEQ 2-bit decode** (consteval LUT is already fast, but PDEP/PEXT could help)
- **POS delta-varint decode** (sequential dependency limits parallel extraction)
- **SAM formatting** (the actual bottleneck — integer-to-string, tab-separated field assembly)

## Environment

- Server: Intel Xeon E5-2696 v4 @ 2.20 GHz (88 cores), AVX2
- Data: HG002 PacBio SequelII 15kb/20kb, GRCh38, haplotag phased
- Compiler: GCC 13+ with `-O2 -mavx2 -mbmi2`
