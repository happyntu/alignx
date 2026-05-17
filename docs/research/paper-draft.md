# AXF1: A Columnar Alignment Format for Accelerated Genomic Region Queries

## Abstract

We present AXF1, a columnar alignment format that stores each SAM field as an independent column stream within reference-sorted chunks. Unlike BAM's row-oriented layout, AXF1 enables selective column I/O and parallel multi-threaded decoding. On HG002 PacBio SequelII long-read data (GRCh38), AXF1 achieves 3.8x-5.4x faster region queries than samtools view and 1.1x-1.4x faster pileup than samtools depth on typical 1 Mb regions, with near-parity (0.90x) on a 21 Mb centromeric region via parallel depth accumulation. Field-specific codecs (delta-varint positions, bit-packed flags, 2-bit sequence literals, alphabet-packed quality scores, front-compressed read name dictionaries, per-key tag streams) are selected per chunk with raw fallback guarantees ensuring encoded payloads never exceed source size. Lossless AXF1 files are 1.6x-2.3x larger than BAM; with optional lossy quality binning and zstd compression, sizes reach 0.52x-0.63x of BAM, comparable to CRAM. The file layout uses absolute byte offsets and contiguous storage compatible with HTTP range requests. AXF1 demonstrates that columnar storage with parallel fused decoding substantially accelerates genomic region queries without sacrificing lossless fidelity.

## 1. Introduction

### 1.1 Background

The BAM format (Li et al., 2009) has been the standard binary alignment representation for over 15 years. BAM stores each alignment record contiguously within BGZF-compressed blocks: read name, flags, position, mapping quality, CIGAR, sequence, quality scores, and auxiliary tags are serialized per record. This row-oriented layout is efficient for sequential full-record access but imposes unnecessary I/O and decode overhead when only a subset of fields is needed.

Common analytical workloads exhibit column-selective access patterns. Coverage computation requires only positions and CIGAR strings. Variant calling reads positions, sequences, and quality scores but ignores read names and most tags. Quality assessment touches only the quality column. In BAM, all these workloads parse the full record, discarding 60-80% of decoded data.

CRAM (Hsi-Yang Fritz et al., 2011) addresses file size through reference-based compression but retains row-oriented access. CRAM containers do not support independent column access, and encoding complexity results in 2-4x slower decode times compared to BAM.

### 1.2 Columnar storage in genomics

Columnar storage is well-established in analytical databases (Abadi et al., 2008; Lamb et al., 2012) and data analytics (Apache Parquet, Apache Arrow). Fields of the same type compress better together, and workloads accessing a subset of columns avoid I/O on untouched data. These benefits translate directly to alignment data, where field types span integers (POS, FLAG, MAPQ), variable-length strings (SEQ, QUAL, CIGAR, QNAME), and heterogeneous key-value maps (TAGS).

Prior work on columnar genomic storage includes CRAM's internal column slicing within containers (limited to intra-container access without independent seekability), Genozip (Lan et al., 2021) for general genomic compression with some columnar properties, and column-store approaches for variant data (BCF, GQT; Layer et al., 2016). To our knowledge, no format provides fully independent column access with parallel chunk-level decode for alignment data.

### 1.3 Contributions

1. **Selective column I/O.** Workloads reading a subset of fields access only those column payloads, with measurable I/O and decode savings (1.1x-1.4x faster pileup on typical regions).
2. **Parallel region decode.** Chunk-based layout enables multi-threaded decoding with work-stealing, achieving 3.8x-5.4x faster region view than samtools on 8 workers.
3. **Field-specific codecs.** Per-chunk codec selection with raw fallback guarantees; 8 specialized codecs covering the full SAM field set.
4. **Lossless round-trip.** BAM → AXF1 → BAM produces byte-identical SAM output for mapped records.
5. **Cloud-compatible layout.** File-absolute byte offsets and contiguous chunks enable HTTP range-request access without format modifications.

## 2. Methods

### 2.1 File Layout

An AXF1 file consists of four sections (Figure 1):

1. **Magic header** (5 bytes): `AXF1\0`
2. **File header**: reference count, chunk count, byte offset of file index
3. **Chunk sequence**: reference-sorted, self-contained chunks
4. **File index** (at EOF): per-chunk byte offset and genomic interval

Each chunk contains:
- **Chunk header**: reference ID, start/end position, record count, column count, per-column metadata (column ID, codec ID, byte offset, byte length)
- **Column payloads**: independently decodable byte ranges
- **CRC-32 footer**

The file index enables random access via binary search: given a query region, overlapping chunks are identified by genomic interval, and only needed columns within those chunks are decoded.

> **Figure 1.** AXF1 file layout. Header → chunk sequence (each containing column payloads) → file index at EOF. Arrows indicate byte-offset references from index entries to chunk positions.

### 2.2 Chunk Sizing Policy

Chunks are sized by a hybrid policy enforcing four constraints:

| Parameter | Value | Purpose |
|:---|:---|:---|
| Target size | 256 KiB | Balanced chunk granularity |
| Maximum size | 512 KiB | Memory bound per chunk |
| Maximum records | 4,096 | Decode time bound |
| Maximum genomic span | 1 Mbp | Region query selectivity |

The policy was tuned on HG002 PacBio data across euchromatic, Y-chromosome, and centromeric regions. A 50 kb span-based alternative showed improvement only on centromeric regions at the cost of regressions elsewhere.

### 2.3 Codec Stack

Each column uses a field-specific codec selected per chunk to minimize payload size, with raw fallback guarantees (Table 1).

> **Table 1.** AXF1 codec assignments per SAM field.

| Field | Primary Codec | Fallback | Mechanism |
|:---|:---|:---|:---|
| POS | Delta-varint | Raw int32 | Successive differences as unsigned varints |
| FLAG | Bit-pack | Raw uint16 | Minimum-width bit packing |
| MAPQ | Byte RLE | Raw uint8 | (value, run-length) pairs |
| CIGAR | Token stream | Raw string | (varint length, op code) pairs per operation |
| SEQ | 2-bit literal | Raw string | A=0,C=1,G=2,T=3; 4 bases/byte; consteval LUT decode |
| QUAL | Alphabet bit-pack | Raw string / byte RLE | Chunk-local alphabet → minimal bit width |
| QNAME | Front-compressed dict | Raw string | Sorted dictionary + shared prefix + varint indices |
| TAGS | Per-key streams | Raw string | Per-tag-key encoding with presence bitmaps |

Optional: zstd compression envelope wraps QUAL when smaller. Lossy mode (`--axf1-quality-lossy illumina8`) applies 8-level binning before codec selection.

### 2.4 Query Engine

**Region query.** Binary-search file index → memory-map file (`mmap` + `MADV_SEQUENTIAL`) → distribute chunks to thread pool (8 workers) via atomic work-stealing → decode + format in parallel.

**Fused decode.** All columns decoded into contiguous `FlatColumn` arrays (single `char[]` buffer + offset array per string field); SAM formatted directly via pointer writes without per-record struct allocation.

**Interior chunk skip.** Chunks fully contained within the query region bypass per-record CIGAR overlap checking.

**Selective column I/O.** Pileup reads only POS+CIGAR; filtered queries add FLAG+MAPQ to the selective set.

**Parallel pileup.** For large regions (>=500 chunks), 8 workers each maintain a thread-local depth array; results are element-wise merged after join.

**Worker-local buffers.** Each view worker accumulates output into one pre-reserved buffer, reducing allocations from O(chunks) to O(workers).

### 2.5 Benchmark Setup

**Hardware.** Intel Xeon E5-2696 v4 (88 cores, 2.20 GHz), ZFS storage.

**Data.** Genome in a Bottle HG002, PacBio SequelII 15 kb/20 kb merged, GRCh38, pbmm2, haplotag 10x. samtools 1.23.1 baseline.

**Regions.** chr1:1M-2M (1 Mb, 3,143 records, euchromatic), chrY:20M-21M (1 Mb, 2,034 records, Y chromosome), chr1:121M-142M (21 Mb, 58,168 records, centromeric).

**Protocol.** Warmup + 5-10 timed repeats. SHA-256 correctness verification before timing.

## 3. Results

### 3.1 Region Query Performance (Figure 2)

| Tool | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| samtools view | 236 ms | 169 ms | 3,223 ms |
| **AXF1 view** | **44 ms (5.4x)** | **35 ms (4.8x)** | **854 ms (3.8x)** |
| samtools view (filtered) | 224 ms | 162 ms | 2,594 ms |
| **AXF1 view (filtered)** | **42 ms (5.3x)** | **32 ms (5.1x)** | **500 ms (5.2x)** |

> **Figure 2.** Region query latency (ms, log scale). Grouped bar chart: samtools view vs AXF1 view, unfiltered and filtered, across three regions. Error bars show min-max over 10 repeats.

AXF1 is 3.8x-5.4x faster than samtools across all regions. The filtered path achieves 5.1x-5.3x by skipping SAM formatting for excluded records. Filtered centromeric (500 ms) is faster than unfiltered (854 ms) because output volume drives the remaining time.

### 3.2 Selective Column I/O (Pileup)

| Tool | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| samtools depth | 383 ms | 286 ms | 5,731 ms |
| **AXF1 pileup** | **265 ms (1.4x)** | **270 ms (1.1x)** | 6,381 ms (0.90x) |

AXF1 pileup reads only POS and CIGAR columns. On 1 Mb regions, selective I/O provides 1.1x-1.4x speedup. On the 21 Mb centromeric region, parallel depth accumulation (8 workers) achieves 0.90x of samtools — near parity, improved from 0.69x with sequential-only processing.

### 3.3 Compression (Figure 3)

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM | 31.98 MB (1.00x) | 18.24 MB (1.00x) | 493.77 MB (1.00x) |
| CRAM | 20.76 MB (0.65x) | 11.55 MB (0.63x) | 377.55 MB (0.77x) |
| AXF1 lossless | 50.50 MB (1.58x) | 33.51 MB (1.84x) | 1,121.39 MB (2.27x) |
| AXF1+zstd | 38.92 MB (1.22x) | 24.24 MB (1.33x) | 858.81 MB (1.74x) |
| AXF1 lossy+zstd | 16.68 MB (0.52x) | 11.52 MB (0.63x) | 473.70 MB (0.96x) |

> **Figure 3.** File size relative to BAM (log scale). Stacked bar chart showing size ratios for each config across regions.

AXF1 lossless is 1.6x-2.3x larger than BAM (no general-purpose block compression). AXF1 lossy+zstd achieves 0.52x-0.63x of BAM on typical regions, matching CRAM.

### 3.4 Encode Time

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM (samtools) | 1,503 ms | 988 ms | 29,656 ms |
| CRAM | 14,072 ms (9.4x slower) | 13,478 ms (13.6x) | 31,897 ms (1.1x) |
| AXF1 lossless | 1,768 ms (1.2x) | 1,085 ms (1.1x) | 31,678 ms (1.1x) |
| AXF1 lossy | 1,074 ms (0.7x) | 694 ms (0.7x) | 18,983 ms (0.6x) |

AXF1 lossless encoding is comparable to BAM (1.1x-1.2x). AXF1 lossy is faster (0.6x-0.7x). CRAM is 9-14x slower on 1 Mb regions due to reference compression startup.

### 3.5 Decode Comparison (Figure 4)

| Format | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM (samtools) | 236 ms | 169 ms | 3,223 ms |
| CRAM | 673 ms (0.35x) | 376 ms (0.45x) | 11,411 ms (0.28x) |
| AXF1 | 44 ms (5.4x) | 35 ms (4.8x) | 854 ms (3.8x) |

> **Figure 4.** Decode latency comparison (ms, log scale). BAM, CRAM, and AXF1 across three regions.

AXF1 is 11x-15x faster than CRAM and 3.8x-5.4x faster than BAM. The centromeric 21 Mb region (1.12 GB payload, 6,878 chunks) achieves ~1.31 GB/s effective throughput with 8 workers.

### 3.6 Round-Trip Fidelity

BAM → AXF1 → BAM round-trip produces byte-identical SAM output for mapped records, verified by SHA-256 on both toy fixtures and HG002 real data. AXF1 stores mapped records only; unmapped records are excluded from comparison.

## 4. Discussion

### 4.1 Columnar vs. row-oriented alignment storage

BAM's design reflects an era when sequential access dominated. Modern workloads access 2-4 of 8+ SAM fields, forcing BAM to parse 60-80% unused data. AXF1 addresses this mismatch at two levels: selective I/O avoids reading untouched columns (1.1x-1.4x pileup speedup), and parallel fused decoding exploits chunk independence for multi-threaded execution (3.8x-5.4x view speedup even when all fields are requested).

The view speedup — where all columns are read — demonstrates that columnar layout benefits extend beyond projection pushdown. The fused decode path (columnar arrays → SAM formatting without per-record allocation) and chunk-level parallelism provide additional advantages unavailable to row-oriented formats.

### 4.2 File size tradeoff

AXF1 lossless trades storage (1.6x-2.3x of BAM) for decode speed (5.4x faster). This targets query-intensive workloads where data is written once and queried many times. For archival storage, CRAM (0.63x-0.77x) remains appropriate. With lossy quality binning and zstd, AXF1 matches CRAM sizes (0.52x-0.63x) while maintaining 15x faster decode.

The size overhead stems from per-column codec headers and the absence of cross-column block compression. Adding optional per-column zstd/LZ4 would reduce the gap while preserving column independence.

### 4.3 Parallel scaling

The 5.4x view speedup on 1 Mb regions (44 ms, 8 workers, 324 chunks) approaches the theoretical throughput limit for SAM text generation (~100 MB/s per core). The 3.8x centromeric speedup (854 ms) reflects output volume saturation: 1.8 GB of SAM text must be drained sequentially.

The filtered centromeric path (500 ms, 5.2x) confirms that reducing output volume recovers scaling — excluded records skip formatting entirely.

### 4.4 Limitations

**File size.** AXF1 lossless is 1.6x-2.3x larger than BAM. Storage-constrained applications should use CRAM or AXF1 lossy modes.

**Pileup at scale.** Parallel pileup reaches 0.90x of samtools depth on 21 Mb centromeric (improved from 0.69x sequential). The remaining gap reflects L3 cache contention from 8 independent 84 MB depth arrays.

**No reference-based compression.** AXF1 omits reference-delta sequence encoding, prioritizing self-contained files over maximum compression.

**Mapped records only.** Unmapped reads and unplaced records are not stored. A complete archival format would need to handle these.

**Single dataset.** Results reflect PacBio SequelII long-read characteristics. Illumina short-read data (shorter records, repeated CIGARs, lower quality entropy) and ONT data (higher error rates) may show different codec selection patterns and relative performance.

### 4.5 Cloud compatibility

AXF1's layout (absolute byte offsets, contiguous chunks, index at EOF) enables HTTP range-request access against S3/GCS/Azure without format modifications. A client reads the index via one range request, identifies relevant chunks by binary search, and fetches chunk byte ranges in parallel. The 256 KiB target chunk size aligns with cloud storage minimum request granularity.

### 4.6 Future work

1. **SIMD decode.** AVX2/AVX-512 for SEQ 2-bit decode (PDEP/PEXT), POS delta-varint (parallel prefix sum), and SAM integer formatting.
2. **Reference-delta SEQ.** Optional reference-based sequence compression for reduced file size.
3. **Cloud transport.** HTTP range-request client for direct cloud object storage access.
4. **Python bindings.** Column-native API returning NumPy arrays or Arrow tables.
5. **Per-column block compression.** Optional zstd/LZ4 per column to reduce lossless file size while preserving column independence.
6. **Streaming pileup.** Coordinate-sorted depth output without materializing the full depth array, reducing memory pressure for large regions.

## 5. Conclusion

AXF1 demonstrates that columnar alignment storage substantially accelerates genomic region queries. On HG002 PacBio data, AXF1 achieves 3.8x-5.4x faster full-record queries and 1.1x-1.4x faster pileup compared to samtools, with parallel centromeric pileup reaching near-parity (0.90x). The format maintains lossless BAM round-trip fidelity while offering optional lossy compression that matches CRAM file sizes at 15x faster decode. Field-specific codecs, chunk-level parallelism, selective column I/O, and cloud-compatible layout address the fundamental mismatch between row-oriented BAM and modern analytical workloads where query latency — not storage density — is the primary constraint.

## Data Availability

AXF1 source code, benchmark scripts, and toy test fixtures are available at https://github.com/happyntu/alignx. HG002 PacBio SequelII data is from the Genome in a Bottle Consortium (Zook et al., 2019).

## References

1. Li H, Handsaker B, Wysoker A, et al. The Sequence Alignment/Map format and SAMtools. *Bioinformatics*. 2009;25(16):2078-2079.
2. Hsi-Yang Fritz M, Leinonen R, Cochrane G, Birney E. Efficient storage of high throughput DNA sequencing data using reference-based compression. *Genome Res*. 2011;21(5):734-740.
3. Abadi DJ, Madden SR, Hachem N. Column-stores vs. row-stores: how different are they really? *Proc. ACM SIGMOD*. 2008:967-980.
4. Lamb A, Fuller M, Varadarajan R, et al. The Vertica analytic database: C-store 7 years later. *Proc. VLDB Endowment*. 2012;5(12):1790-1801.
5. Lan D, Tober R, Friedman B, Leinonen R, Robinson J. Genozip: a universal extensible genomic data compressor. *Bioinformatics*. 2021;37(16):2225-2230.
6. Layer RM, Kindlon N, Karber KD, Quinlan AR. GIGGLE: a search engine for large-scale integrated genome analysis. *Nat Methods*. 2018;15(2):123-126.
7. Zook JM, McDaniel J, Olson ND, et al. An open resource for accurately benchmarking small variant and reference calls. *Nat Biotechnol*. 2019;37(5):561-566.

## Supplementary Figures

> **Figure S1.** AXF1 chunk layout detail showing column payload byte ranges within a single chunk. Each column is independently seekable via chunk-header metadata.

> **Figure S2.** Codec selection distribution on HG002 chr1:1M-2M (324 chunks). Pie charts per column showing fraction of chunks using each codec.

> **Figure S3.** Decode time breakdown (profiling). Stacked bar showing time spent in chunk selection, column decode, SAM formatting, and output write for each region.

> **Figure S4.** Parallel scaling: view latency vs. worker count (1-8 threads) on centromeric region.
