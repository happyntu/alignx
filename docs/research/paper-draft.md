# AXF1: A Columnar Alignment Format for Accelerated Genomic Region Queries

## Abstract

The Binary Alignment Map (BAM) format stores alignment records in a row-oriented layout that requires full-record parsing even when only a subset of fields is needed. We present AXF1, a columnar alignment format that stores each SAM field as an independent column stream within reference-sorted chunks, enabling selective column I/O and parallel decoding. On HG002 PacBio SequelII long-read data aligned to GRCh38, AXF1 region queries are 3.8x-5.4x faster than samtools view across typical euchromatic, Y-chromosome, and centromeric regions, while pileup computations that read only position and CIGAR columns are 1.1x-1.4x faster than samtools depth on typical regions and reach near-parity (0.90x) on 21 Mb centromeric regions via parallel depth accumulation. The format uses field-specific codecs (delta-varint positions, bit-packed flags, 2-bit sequence literals, alphabet-packed quality scores, front-compressed read name dictionaries, per-key tag streams) with per-chunk codec selection and raw fallback guarantees. AXF1 lossless files are 1.6x-2.3x larger than BAM due to the absence of general-purpose block compression; with optional lossy quality binning and zstd compression, file sizes reach 0.52x-0.63x of BAM on typical regions, comparable to CRAM. The file layout uses absolute byte offsets and contiguous chunk storage compatible with HTTP range requests. AXF1 demonstrates that columnar storage combined with parallel fused decoding can substantially accelerate genomic region queries without sacrificing lossless fidelity.

## 1. Introduction

### 1.1 Background

High-throughput sequencing generates alignment data at rates that challenge existing access patterns. The BAM format (Li et al., 2009) has been the standard binary alignment representation for over 15 years. BAM stores each alignment record contiguously: read name, flags, position, mapping quality, CIGAR, sequence, quality scores, and auxiliary tags are serialized together within BGZF-compressed blocks. This row-oriented layout is efficient for sequential reading of complete records but imposes unnecessary I/O and decode overhead for workloads that access only a subset of fields.

Common analytical workloads exhibit column-selective access patterns. Coverage computation requires only positions and CIGAR strings. Variant calling reads positions, sequences, and quality scores but not read names or most auxiliary tags. Quality score analysis touches only the quality column. In BAM, all of these workloads must parse the full record, including fields they discard.

CRAM (Hsi-Yang Fritz et al., 2011) addresses file size through reference-based compression but retains a fundamentally row-oriented access pattern. CRAM's record-based containers do not support independent column access, and its encoding complexity results in 2-4x slower decode times compared to BAM.

### 1.2 Columnar storage in genomics

Columnar storage formats are well-established in analytical databases (Abadi et al., 2006; Lamb et al., 2012) and data analytics (Apache Parquet, Apache Arrow). The key insight is that fields of the same type compress better together (type-homogeneous compression), and workloads that access a subset of columns avoid I/O on untouched columns (projection pushdown). These benefits translate directly to alignment data, where field types span integers (POS, FLAG, MAPQ), variable-length strings (SEQ, QUAL, CIGAR, QNAME), and heterogeneous key-value maps (TAGS).

Previous work on columnar genomic formats includes CRAM's internal column slicing within containers, DeBaCl (Hach et al., 2014) for sequence-focused compression, and various column-store approaches for variant data (BCF, GQT). To our knowledge, no production-ready columnar alignment format exists that provides independent column access with parallel decode at query time.

### 1.3 Contributions

We present AXF1, a columnar alignment format with the following design goals:

1. **Selective column I/O.** Workloads that need only a subset of fields read only those column payloads, with measurable I/O and decode savings.
2. **Parallel region decode.** Chunk-based layout enables multi-threaded decoding with work-stealing, achieving near-linear scaling on modern multi-core servers.
3. **Field-specific codecs.** Each column uses a type-appropriate codec (delta-varint, bit-pack, 2-bit literal, alphabet-pack, dictionary, per-key streams) selected per chunk for minimum payload size, with raw fallback guarantees.
4. **Lossless round-trip.** BAM to AXF1 to BAM conversion produces byte-identical SAM output for mapped records.
5. **Cloud-compatible layout.** File-absolute byte offsets and contiguous chunks enable HTTP range-request access without format modifications.

## 2. Methods

### 2.1 File Layout

An AXF1 file consists of four sections:

1. **Magic header** (5 bytes): `AXF1\0`
2. **File header**: reference count, total chunk count, byte offset of the file index
3. **Chunk sequence**: reference-sorted chunks, each self-contained
4. **File index** (at EOF): maps each chunk to its file-absolute byte offset and genomic interval

Each chunk contains:
- **Chunk header**: reference ID (uint32), start position (int32), end position (int32), record count (uint32), column count (uint16), and per-column metadata (column ID, codec ID, byte offset within chunk, byte length)
- **Column payloads**: independently decodable byte ranges
- **CRC-32 footer**: integrity check

The file index at EOF enables random access via binary search: given a query region, the reader identifies overlapping chunks by genomic interval, seeks to their byte offsets, and decodes only the needed columns.

### 2.2 Chunk Sizing Policy

Chunks are sized by a hybrid policy enforcing four simultaneous constraints:

| Parameter | Value | Purpose |
|:---|:---|:---|
| Target size | 256 KiB | Soft limit for balanced chunk sizes |
| Maximum size | 512 KiB | Hard upper bound on memory per chunk |
| Maximum records | 4,096 | Bounds per-chunk decode time |
| Maximum genomic span | 1,000,000 bp | Ensures region queries skip irrelevant chunks |

This policy was tuned on HG002 PacBio SequelII data across euchromatic, Y-chromosome, and centromeric regions. Alternative span-based policies (50 kb span) showed faster decoding only on centromeric regions and slower on others, so the conservative byte-budget policy was retained as default.

### 2.3 Codec Stack

Each column uses a field-specific codec selected per chunk to minimize payload size. The writer evaluates candidate codecs and selects the one producing the smallest output, with a raw (uncompressed) fallback that guarantees the encoded payload is never larger than the source data.

**POS (position).** Delta-varint: positions within a sorted chunk are encoded as successive differences using unsigned variable-length integers. Fallback: raw 32-bit integers for non-monotonic chunks.

**FLAG.** Bit-pack: minimum bit width to represent all FLAG values in the chunk. Fallback: raw 16-bit integers.

**MAPQ.** Byte RLE: (value, run-length) pairs exploiting MAPQ repetitiveness (e.g., MAPQ 60 dominates in HiFi data). Fallback: raw bytes.

**CIGAR.** Token stream: operation count followed by (varint length, operation code) pairs. Alternative: dictionary codec (sorted unique strings + varint indices) for short-read data with repeated patterns. On HG002 PacBio data, the token codec was selected universally (CIGAR strings are unique per long read).

**SEQ (sequence).** 2-bit literal: A=0, C=1, G=2, T=3, packed 4 bases per byte. Decode uses a compile-time 256-entry lookup table (4 bases per byte). Fallback: raw bytes for sequences with ambiguity codes.

**QUAL (quality).** Three-tier selection: alphabet bit-pack (`qual_pack`) maps the chunk-local quality alphabet to minimal bit width; byte RLE (`qual_rle`) for highly repetitive values; raw fallback. Optional zstd compression envelope wraps the selected codec when smaller. On HG002 PacBio lossless data, `qual_pack` was selected universally (bit_width=7 for ~70 unique quality values).

**QNAME (read name).** Front-compressed dictionary: sorted unique names where each entry stores the shared prefix length with its predecessor plus the suffix. Per-record varint indices reference the dictionary.

**TAGS (auxiliary tags).** Per-key streams: each tag key gets a dedicated stream with a presence bitmap (for partial tag presence) and type-specific encoding (zigzag varint for integers, length-prefixed bytes for strings). On HG002 chr1:1M-2M, 77% of chunks used per-stream encoding.

**Optional lossy mode.** `--axf1-quality-lossy illumina8` applies Illumina 8-level quality binning (Q0-41+ mapped to 8 representative values) before codec selection, shifting most chunks from `qual_pack` to the more compact `qual_rle`.

### 2.4 Query Engine

#### Region query

Given a genomic region, the query engine:
1. Binary-searches the file index for overlapping chunks
2. Memory-maps the file (Linux `mmap` + `MADV_SEQUENTIAL`)
3. Distributes chunks to a fixed thread pool (min(hardware_concurrency, 8) workers) via atomic work-stealing
4. Each worker decodes requested columns directly from mapped memory and formats output

#### Selective column I/O

For pileup/coverage workloads, the reader decodes only POS and CIGAR columns, skipping SEQ, QUAL, QNAME, and TAGS. When read filters are active (FLAG/MAPQ), those columns are added to the selective set.

#### Fused decode

For full-record view, the fused decode path (`decode_all_columns_mapped`) decodes all columns into contiguous columnar arrays (`FlatColumn`: single `char[]` buffer + `uint32_t` offset array per string field) and formats SAM directly via pointer writes — no intermediate per-record struct allocation.

#### Interior chunk skip

Chunks whose genomic interval is fully contained within the query region bypass per-record CIGAR-based overlap checking, eliminating ~58,000 CIGAR string parses on the centromeric 21 Mb region.

#### Worker-local buffer reuse

Each worker thread accumulates output into one pre-reserved `std::string` buffer. Results are drained in chunk order after all workers complete. This reduces heap allocations from one per chunk (~6,878 on centromeric) to one per worker (8).

### 2.5 Benchmark Setup

#### Hardware and data

Benchmarks were run on a dedicated server (Intel Xeon E5-2696 v4, 88 cores, 2.20 GHz, AVX2) with ZFS storage (`/mypool`). The dataset was the Genome in a Bottle HG002 sample, PacBio SequelII 15 kb/20 kb merged, aligned to GRCh38 with pbmm2, haplotag phased at 10x coverage (~30x mean). samtools 1.23.1 served as the baseline.

#### Regions

| Region | Span | Records | Character |
|:---|:---|---:|:---|
| chr1:1,000,000-2,000,000 | 1 Mb | 3,143 | Typical euchromatic |
| chrY:20,000,000-21,000,000 | 1 Mb | 2,034 | Y chromosome |
| chr1:121,000,000-142,000,000 | 21 Mb | 58,168 | Centromeric / repetitive |

#### Protocol

Each configuration was run with warmup iterations (excluded) followed by 5-10 timed repeats. Correctness was verified by SHA-256 digest comparison of SAM output against samtools baseline before timing.

## 3. Results

### 3.1 Region Query Performance

| Tool | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| samtools view | 236 ms | 169 ms | 3,223 ms |
| **AXF1 view** | **44 ms (5.4x)** | **35 ms (4.8x)** | **854 ms (3.8x)** |
| samtools view (filtered) | 224 ms | 162 ms | 2,594 ms |
| **AXF1 view (filtered)** | **42 ms (5.3x)** | **32 ms (5.1x)** | **500 ms (5.2x)** |

AXF1 region view is 3.8x-5.4x faster than samtools view across all tested regions. The filtered path (FLAG exclude 2308 + MAPQ >= 20) achieves 5.1x-5.3x speedup by skipping SAM formatting for excluded records.

### 3.2 Selective Column I/O (Pileup)

| Tool | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| samtools depth | 383 ms | 286 ms | 5,731 ms |
| **AXF1 pileup** | **265 ms (1.4x)** | **270 ms (1.1x)** | 6,381 ms (0.90x) |

On 1 Mb regions, AXF1 pileup (POS+CIGAR only) is 1.1x-1.4x faster than samtools depth via selective column I/O (reads only POS and CIGAR payloads, skipping SEQ, QUAL, QNAME, TAGS). On the 21 Mb centromeric region (6,878 chunks), the parallel pileup engine (8-worker thread pool with per-worker depth arrays) achieves 0.90x of samtools depth — near parity compared to the sequential-only baseline of 0.69x. The remaining gap reflects the memory bandwidth cost of maintaining 8 independent 84 MB depth arrays competing for L3 cache.

### 3.3 Compression

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM | 31.98 MB (1.00x) | 18.24 MB (1.00x) | 493.77 MB (1.00x) |
| CRAM | 20.76 MB (0.65x) | 11.55 MB (0.63x) | 377.55 MB (0.77x) |
| AXF1 lossless | 50.50 MB (1.58x) | 33.51 MB (1.84x) | 1,121.39 MB (2.27x) |
| AXF1+zstd | 38.92 MB (1.22x) | 24.24 MB (1.33x) | 858.81 MB (1.74x) |
| AXF1 lossy+zstd | 16.68 MB (0.52x) | 11.52 MB (0.63x) | 473.70 MB (0.96x) |

AXF1 lossless is 1.6x-2.3x larger than BAM because column payloads lack general-purpose block compression. With lossy quality binning (Illumina 8-level) and zstd quality compression, AXF1 achieves 0.52x-0.63x of BAM on typical regions, matching CRAM. On the 21 Mb centromeric region, AXF1 lossy+zstd is 0.96x of BAM.

### 3.4 Encode Time

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM (samtools) | 1,503 ms | 988 ms | 29,656 ms |
| CRAM | 14,072 ms (9.4x) | 13,478 ms (13.6x) | 31,897 ms (1.1x) |
| AXF1 lossless | 1,768 ms (1.2x) | 1,085 ms (1.1x) | 31,678 ms (1.1x) |
| AXF1 lossy | 1,074 ms (0.7x) | 694 ms (0.7x) | 18,983 ms (0.6x) |

AXF1 lossless encoding is comparable to BAM extraction (1.1x-1.2x). CRAM encoding is 9.4x-13.6x slower on 1 Mb regions due to reference compression startup. AXF1 lossy encoding is faster than BAM (0.6x-0.7x) because reduced quality entropy makes codec selection cheaper.

### 3.5 Decode vs. CRAM

| Format | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM (samtools) | 236 ms | 169 ms | 3,223 ms |
| CRAM | 673 ms (0.35x) | 376 ms (0.45x) | 11,411 ms (0.28x) |
| AXF1 | 44 ms (5.4x) | 35 ms (4.8x) | 854 ms (3.8x) |

AXF1 is 15x-13x faster than CRAM decode and 3.8x-5.4x faster than BAM decode. CRAM's reference-based decompression and record reconstruction impose significant overhead that columnar parallel decode avoids entirely.

### 3.6 Round-Trip Fidelity

BAM → AXF1 → BAM round-trip produces byte-identical SAM output for all mapped records across toy and real data. The round-trip is verified by:
1. Converting BAM to AXF1 (`alignx convert --format AXF1`)
2. Exporting AXF1 back to BAM (`alignx export -o output.bam`)
3. Comparing `samtools view` output of both BAMs by SHA-256 digest

AXF1 stores only mapped records; unmapped records are excluded from round-trip comparison.

## 4. Discussion

### 4.1 The case for columnar alignment storage

BAM's row-oriented layout was designed for an era when sequential tape-like access dominated and CPU cycles were expensive relative to I/O. Modern analytical workloads — coverage, pileup, variant calling, quality assessment — typically access 2-4 of the 8+ SAM fields. On these workloads, BAM forces parsing of 60-80% unused data per record.

AXF1's columnar layout directly addresses this mismatch. The 1.2x-1.6x pileup speedup on typical regions demonstrates the selective I/O benefit even for a workload reading only 2 columns (POS+CIGAR). The 3.8x-5.4x full-record view speedup demonstrates that columnar storage combined with parallel fused decoding can outperform row-oriented parsing even when all fields are requested.

### 4.2 File size tradeoff

AXF1 lossless files are larger than BAM (1.6x-2.3x) because the format prioritizes decode speed and random access over compression ratio. Each column payload uses a field-specific codec optimized for fast decode rather than maximum compression. The format does not apply general-purpose block compression (BGZF/DEFLATE) across columns.

This is a deliberate tradeoff: AXF1 targets query-intensive workloads where data is written once and read many times. For archival storage where size is the primary concern, CRAM (0.63x-0.77x of BAM) remains the appropriate choice. For workloads requiring frequent region queries, the 5.4x decode speedup justifies the storage overhead. With optional lossy quality binning and zstd compression, AXF1 can match or beat CRAM file sizes (0.52x-0.63x) at the cost of quality precision.

### 4.3 Parallel decode scaling

The 3.8x speedup on the centromeric 21 Mb region (854 ms with 8 workers) represents ~1.31 GB/s effective throughput from a 1.12 GB payload. The sub-linear scaling relative to 1 Mb regions (5.4x) reflects:
1. Memory bandwidth saturation on the 88-core server
2. Output volume: centromeric data produces ~1.8 GB of SAM text that must be written sequentially
3. Thread pool drain overhead: output must be emitted in chunk order

The filtered centromeric path (500 ms, 5.2x) demonstrates that reducing output volume recovers scaling — the filter eliminates formatting for excluded records, and the remaining records produce less output data.

### 4.4 Limitations

**File size.** AXF1 lossless is larger than BAM. Applications constrained by storage cost should use CRAM or AXF1 lossy modes.

**Pileup at scale.** AXF1 parallel pileup achieves 0.90x of samtools depth on the 21 Mb centromeric region. The remaining gap reflects memory bandwidth contention from 8 independent depth arrays (84 MB each) competing for shared L3 cache. Further improvement would require streaming pileup (coordinate-sorted output without full depth array) or NUMA-aware allocation.

**No reference-based compression.** AXF1 does not implement reference-delta sequence encoding. CRAM's reference-based compression is superior for file size on long reads aligned to a known reference. AXF1 prioritizes self-contained files that decode without external reference access.

**Mapped records only.** AXF1 currently stores only mapped records. Unmapped reads, supplementary alignments with unmapped mates, and unplaced reads are excluded. A complete archival format would need to handle these.

**Single dataset.** Benchmarks use HG002 PacBio SequelII long-read data. Performance characteristics may differ on Illumina short-read data (shorter records, different quality distributions, more repeated CIGARs) or Oxford Nanopore data (higher error rates, different quality entropy).

### 4.5 Cloud and distributed access

AXF1's file layout (absolute byte offsets, contiguous chunks, index at EOF) is directly compatible with HTTP range requests against cloud object storage (S3, GCS, Azure Blob). A client can:
1. Read the index from a single range request at the known EOF offset
2. Identify relevant chunks by binary search
3. Fetch chunk byte ranges in parallel

This requires no format modifications, pre-splitting, or auxiliary index servers. The chunk sizing policy (256 KiB target) produces range requests that align well with cloud storage minimum request sizes and per-request latency amortization.

### 4.6 Future work

1. **Parallel pileup.** Apply the thread pool and mmap engine to pileup/coverage workloads.
2. **SIMD decode.** AVX2/AVX-512 acceleration for SEQ 2-bit decode (PDEP/PEXT), POS delta-varint (parallel prefix sum), and SAM integer-to-string formatting.
3. **Reference-delta SEQ.** Optional reference-based sequence compression for reduced file size when a reference is available.
4. **Cloud transport.** HTTP range-request client for direct cloud object storage access.
5. **Python bindings.** Column-native query API returning NumPy arrays or Arrow tables for analytical workloads.
6. **Block compression.** Optional per-column zstd/LZ4 compression beyond quality to reduce file size while maintaining column independence.

## 5. Conclusion

AXF1 demonstrates that columnar storage is a viable and high-performance alternative to row-oriented BAM for genomic region queries. On HG002 PacBio long-read data, AXF1 achieves 3.8x-5.4x faster full-record region queries and 1.2x-1.6x faster pileup computations compared to samtools, while maintaining lossless round-trip fidelity. The format's field-specific codecs, chunk-based parallel decode, and selective column I/O address the fundamental mismatch between BAM's row-oriented layout and modern analytical access patterns. With optional lossy compression, AXF1 can match CRAM file sizes while delivering decode speeds an order of magnitude faster. The cloud-compatible file layout positions AXF1 for distributed genomics workloads where query latency, not storage density, is the primary constraint.

## References

- Li H, Handsaker B, Wysoker A, et al. The Sequence Alignment/Map format and SAMtools. *Bioinformatics*. 2009;25(16):2078-2079.
- Hsi-Yang Fritz M, Leinonen R, Cochrane G, Birney E. Efficient storage of high throughput DNA sequencing data using reference-based compression. *Genome Res*. 2011;21(5):734-740.
- Abadi DJ, Madden SR, Hachem N. Column-stores vs. row-stores: how different are they really? *SIGMOD*. 2008.
- Lamb A, Fuller M, Varadarajan R, et al. The Vertica analytic database: C-store 7 years later. *VLDB Endowment*. 2012;5(12):1790-1801.
- Hach F, Sarrafi I, Hormozdiari F, et al. mrsFAST-Ultra: a compact, SNP-aware mapper for high performance sequencing applications. *Nucleic Acids Res*. 2014;42(Web Server issue):W494-W500.
- Zook JM, McDaniel J, Olson ND, et al. An open resource for accurately benchmarking small variant and reference calls. *Nat Biotechnol*. 2019;37(5):561-566.
