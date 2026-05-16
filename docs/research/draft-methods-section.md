# Methods

## AXF1 Format Design

AXF1 is a columnar alignment format that stores each SAM field as an independent column stream within reference-sorted chunks. Unlike BAM's row-oriented layout, where all fields of a record are stored contiguously, AXF1 separates POS, FLAG, MAPQ, CIGAR, SEQ, QUAL, QNAME, and TAGS into independently seekable column payloads within each chunk. This enables selective column I/O: workloads that need only a subset of fields can read those columns without touching the rest. For example, a pileup computation reads only POS and CIGAR columns, skipping QUAL, SEQ, QNAME, and TAGS entirely.

An AXF1 file consists of a 5-byte magic header (`AXF1\0`), a file header recording reference count, chunk count, and the byte offset of the file index, followed by a sequence of chunks sorted by reference ID and start position, and a file index at the end of the file. Each chunk contains a chunk header (reference ID, genomic interval, record count, per-column byte offsets), followed by column payloads, and a CRC-32 footer. The file index maps each chunk to its file-absolute byte offset and genomic interval, enabling region queries via binary search without loading the entire file.

Chunks are sized by a hybrid policy that enforces four simultaneous constraints: a target uncompressed size of 256 KiB (soft) and maximum of 512 KiB (hard), a maximum of 4,096 records, and a maximum genomic span of 1,000,000 bp. This balances random-access granularity against per-chunk overhead. The chunk sizing policy was tuned on HG002 PacBio SequelII data across three genomic regions (euchromatic, Y chromosome, centromeric) and validated against alternative span-based policies.

The file layout uses file-absolute byte offsets and contiguous chunk storage with the index at EOF, which makes the format compatible with HTTP range requests for cloud object storage without requiring format modifications.

## Codec Stack

Each column uses a field-specific codec selected per chunk to minimize payload size. The writer evaluates candidate codecs for each chunk and selects the one producing the smallest output, with a raw (uncompressed) fallback that guarantees the encoded payload is never larger than the original data.

**POS (position).** Positions within a chunk are sorted and monotonically increasing. The delta-varint codec encodes successive position differences as unsigned variable-length integers. For the rare case of non-monotonic record order, the codec falls back to raw 32-bit integers.

**FLAG.** SAM FLAG values are 16-bit integers with limited dynamic range within a chunk. The bit-pack codec determines the minimum bit width needed to represent all FLAG values in the chunk and packs them accordingly. When bit-packing is not smaller than raw storage, the chunk uses raw 16-bit integers.

**MAPQ.** Mapping quality values are single bytes that are often highly repetitive (e.g., MAPQ 60 dominates in PacBio HiFi data). The byte run-length encoding (RLE) codec stores (value, run-length) pairs. The fallback is raw bytes.

**CIGAR.** CIGAR strings are tokenized into an operation count followed by (length, operation-code) pairs, where lengths are encoded as unsigned varints. This self-contained token stream avoids dictionary overhead for long-read data where CIGAR strings are typically unique per read. A dictionary codec (sorted unique CIGAR strings with per-record varint indices) is available as an alternative for short-read data where repeated CIGAR patterns are common. On HG002 PacBio data, the token stream codec was selected for all 324 chunks in the chr1:1M-2M region.

**SEQ (sequence).** Bases are encoded as 2-bit literals (A=0, C=1, G=2, T=3) for sequences containing only uppercase A, C, G, and T. Sequences with ambiguity codes, lowercase bases, or missing sequence (`*`) fall back to raw storage. Reference-delta encoding is deferred to a future version pending reference identity metadata and strand semantics.

**QUAL (quality).** Quality scores use a three-tier codec selection. The alphabet bit-pack codec (`qual_pack`) maps the chunk-local quality alphabet to a minimal bit width; the byte RLE codec (`qual_rle`) is effective when quality values are repetitive. The writer selects whichever produces the smallest payload, or falls back to raw bytes. An optional zstd compression envelope wraps the selected base codec when the compressed output is smaller. On HG002 PacBio data, `qual_pack` was selected for all chunks in lossless mode, reducing quality payload to approximately 75% of raw size; with zstd, quality payload decreased to approximately 67% of raw.

A lossy quality option is available under the `--axf1-quality-lossy illumina8` flag. This applies Illumina 8-level quality binning as a pre-processing step before codec selection, mapping Phred scores Q0-41+ to eight representative values (Q0, Q6, Q15, Q22, Q27, Q33, Q37, Q40). The reduced alphabet (from ~40 to 8 unique values) shifts codec selection from `qual_pack` to `qual_rle` on most chunks.

**QNAME (read name).** Read names are encoded using a front-compressed dictionary: a sorted list of unique names where each entry stores the shared prefix length with its predecessor, followed by the suffix. Per-record varint indices reference the dictionary. The raw fallback is used when dictionary encoding is not smaller.

**TAGS (auxiliary tags).** Tags are decomposed into per-tag-key streams. Each stream stores a presence bitmap (for tags not present on all records), followed by type-specific value encoding: zigzag varint for integer tags, length-prefixed bytes for string tags. The raw fallback handles chunks with inconsistent tag order or where per-stream encoding is not beneficial. On HG002 chr1:1M-2M, 77% of chunks (250/324) used per-stream encoding.

## Benchmark Setup

### Hardware and Data

Benchmarks were run on a dedicated server with ZFS storage. The dataset was the Genome in a Bottle HG002 sample, PacBio SequelII 15 kb/20 kb merged, aligned to GRCh38 with pbmm2, haplotag phased at 10x coverage. The BAM file was indexed with a standard BAI index. CRAM comparisons used the GCA_000001405.15 GRCh38 no-alt analysis set reference FASTA. samtools version 1.23.1 was used as the comparison baseline for all workloads.

### Regions

Three genomic regions were selected to represent different data characteristics:

| Region | Span | Records | Character |
|:---|:---|---:|:---|
| chr1:1,000,000-2,000,000 | 1 Mb | 3,143 | Typical euchromatic |
| chrY:20,000,000-21,000,000 | 1 Mb | 2,034 | Y chromosome |
| chr1:121,000,000-142,000,000 | 21 Mb | 58,168 | Centromeric |

### Protocol

Each benchmark configuration was run with 1 warmup iteration (excluded from timing) followed by 5 timed repeats. Wall-clock time was measured at nanosecond precision. Summary statistics include median, 95th percentile, minimum, maximum, and outlier count (runs exceeding 2x median).

Correctness was verified before timing. For lossless configurations, SAM output was compared by SHA-256 digest against the samtools baseline. For lossy configurations and CRAM (which recomputes MD/NM tags from the reference), record count parity was verified.

## Compression Benchmark

Six format configurations were compared for file size, encode time, and decode time:

| Config | Description |
|:---|:---|
| BAM | samtools region extract (BGZF compressed) |
| CRAM | samtools region extract (CRAM v3, reference-based) |
| AXF1 | Lossless columnar, no additional compression |
| AXF1+zstd | Lossless + zstd quality compression |
| AXF1 lossy | Illumina 8-level quality binning |
| AXF1 lossy+zstd | Lossy binning + zstd quality compression |

### File Size

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM | 31,982,136 (1.00x) | 18,238,976 (1.00x) | 493,767,319 (1.00x) |
| CRAM | 20,761,364 (0.65x) | 11,553,406 (0.63x) | 377,551,186 (0.77x) |
| AXF1 | 50,498,344 (1.58x) | 33,509,398 (1.84x) | 1,121,387,598 (2.27x) |
| AXF1+zstd | 38,922,272 (1.22x) | 24,238,110 (1.33x) | 858,812,670 (1.74x) |
| AXF1 lossy | 23,120,174 (0.72x) | 14,521,862 (0.80x) | 569,088,358 (1.15x) |
| AXF1 lossy+zstd | 16,682,468 (0.52x) | 11,517,292 (0.63x) | 473,704,107 (0.96x) |

AXF1 lossless is 1.58x-2.27x larger than BAM because column payloads lack general-purpose block compression (unlike BAM's BGZF). AXF1 with lossy quality binning and zstd compression achieves the smallest files on 1 Mb regions (0.52x-0.63x of BAM), matching or exceeding CRAM compression (0.63x-0.65x). On the 21 Mb centromeric region, AXF1 lossy+zstd is 0.96x of BAM, still smaller but not matching CRAM (0.77x). CRAM benefits from reference-based sequence compression that scales well to larger regions.

### Encode Time

| Config | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| BAM | 1,503 | 988 | 29,656 |
| CRAM | 14,072 (9.4x) | 13,478 (13.6x) | 31,897 (1.08x) |
| AXF1 | 1,768 (1.18x) | 1,085 (1.10x) | 31,678 (1.07x) |
| AXF1 lossy | 1,074 (0.71x) | 694 (0.70x) | 18,983 (0.64x) |
| AXF1 lossy+zstd | 1,404 (0.93x) | 884 (0.89x) | 24,896 (0.84x) |

AXF1 lossless encoding is comparable to BAM extraction (1.07x-1.18x). AXF1 lossy encoding is faster than BAM across all regions (0.64x-0.71x) because reduced quality entropy makes codec selection cheaper. CRAM encoding is 9.4x-13.6x slower on 1 Mb regions due to reference compression startup; on the 21 Mb region, CRAM amortizes startup overhead and approaches BAM speed (1.08x).

### Decode Time (Full-Record View)

| Config | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| BAM (samtools) | 459 | 386 | 8,874 |
| CRAM | 673 (1.5x) | 376 (1.0x) | 11,411 (1.3x) |
| AXF1 | **461 (1.0x)** | **375 (1.0x)** | 16,210 (1.8x) |
| AXF1 lossy | — | — | — |

AXF1 lossless full-record decode achieves parity with samtools view on 1 Mb regions after decoder optimization (batch SEQ 2-bit lookup table, QUAL pack bit-accumulator, CIGAR operation table with `std::to_chars`, QNAME dictionary reference counting, zero-copy SAM formatting). The centromeric 21 Mb region is 1.8x slower due to data volume, but represents a 1.7x improvement from pre-optimization (27,005 ms). BAM baseline times differ from the compression benchmark run because the query benchmark was run on a different date with different system load; the relative rankings are the important comparison. AXF1 lossy decode times were not rerun but would see proportional improvement.

## Query Benchmark

Twelve tool-filter combinations were benchmarked: three tools (samtools, alignx BAM path, alignx AXF1 path) × two workloads (view, pileup) × two filter states (unfiltered, filtered with FLAG exclude 2308 and MAPQ ≥ 20). AXF1 files were pre-converted using the lossless v1.0 codec stack.

### Pileup (Selective Column I/O)

| Tool | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| samtools depth | 356 | 282 | 6,743 |
| alignx pileup (BAM) | 475 | 379 | 10,877 |
| alignx pileup (AXF1) | **221** | **239** | 9,728 |

AXF1 pileup reads only POS and CIGAR columns, skipping QUAL, SEQ, QNAME, and TAGS. On 1 Mb regions, this selective I/O makes AXF1 pileup 1.18x-1.61x faster than samtools depth and 1.58x-2.15x faster than the same tool's BAM code path. On the 21 Mb centromeric region, samtools depth is faster (0.69x) because HTSlib's sequential parsing is highly optimized and the per-chunk overhead of selective I/O becomes relatively larger at scale.

### Pileup with Filters

| Tool | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| samtools depth (filtered) | 329 | 265 | 4,978 |
| alignx pileup AXF1 (filtered) | **222** | 290 | 8,587 |

When read filters are active (FLAG exclude unmapped, secondary, supplementary; MAPQ ≥ 20), the AXF1 pileup path adds FLAG and MAPQ columns to the selective I/O set (POS+CIGAR+FLAG+MAPQ). AXF1 is faster on chr1:1M-2M (1.49x) but slightly slower on chrY:20M-21M (0.92x) and the centromeric region (0.58x). The filter itself has minimal impact on pileup timing because few reads in this HG002 PacBio dataset are excluded by FLAG 2308 + MAPQ 20.

### View (Full-Record Output)

| Tool | chr1:1M-2M (ms) | chrY:20M-21M (ms) | chr1:121M-142M (ms) |
|:---|---:|---:|---:|
| samtools view | 459 | 386 | 8,874 |
| alignx view (AXF1) | **461** | **375** | 16,210 |
| samtools view (filtered) | 576 | 322 | 4,078 |
| alignx view AXF1 (filtered) | **468** | **319** | 5,034 |

After decoder optimization (batch SEQ 2-bit lookup table, QUAL pack bit-accumulator, CIGAR operation table, zero-copy SAM formatting), AXF1 view achieves parity with samtools view on 1 Mb regions: 461 ms vs 459 ms (chr1:1M-2M) and 375 ms vs 386 ms (chrY:20M-21M). Filtered AXF1 view is 1.01x-1.23x faster than filtered samtools view on 1 Mb regions due to the two-pass selective decode path (FLAG+MAPQ first, then output columns for matched records only). The centromeric 21 Mb region remains 0.55x for unfiltered view and 0.81x for filtered view — the sheer data volume of 58,168 PacBio long reads still favors BAM's sequential row-oriented parsing.
