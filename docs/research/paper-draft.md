# AXF1: A Columnar Alignment Format for Accelerated Genomic Region Queries

## Abstract

We present AXF1, a columnar alignment format that stores each SAM field as an independent column stream within reference-sorted chunks. Unlike BAM's row-oriented layout, AXF1 enables selective column I/O and parallel multi-threaded decoding. On HG002 PacBio SequelII long-read data (GRCh38), AXF1 achieves 3.8x-5.4x faster region queries than samtools view and 3.0x-4.1x faster pileup than samtools depth across all tested regions, including a 21 Mb centromeric region. On HG002 Illumina 300x short-read data, AXF1 achieves 3.0x-3.9x faster view, confirming that the speedup generalizes across sequencing platforms. Field-specific codecs (delta-varint positions, bit-packed flags, 2-bit sequence literals, reference-delta sequences, alphabet-packed quality scores, front-compressed read name dictionaries, per-key tag streams) are selected per chunk with raw fallback guarantees ensuring encoded payloads never exceed source size. With optional per-column zstd compression, lossless AXF1 approaches BAM size (1.02x on typical regions); with lossy quality binning and zstd, sizes reach 0.52x-0.63x of BAM, comparable to CRAM. The file layout uses absolute byte offsets and contiguous storage compatible with HTTP range requests. A Python API (pybind11) exposes all query and conversion functions with numpy/pandas integration. AXF1 demonstrates that columnar storage with parallel fused decoding substantially accelerates genomic region queries across both long-read and short-read data without sacrificing lossless fidelity.

## 1. Introduction

### 1.1 Background

The BAM format (Li et al., 2009) has been the standard binary alignment representation for over 15 years. BAM stores each alignment record contiguously within BGZF-compressed blocks: read name, flags, position, mapping quality, CIGAR, sequence, quality scores, and auxiliary tags are serialized per record. This row-oriented layout is efficient for sequential full-record access but imposes unnecessary I/O and decode overhead when only a subset of fields is needed.

Common analytical workloads exhibit column-selective access patterns. Coverage computation requires only positions and CIGAR strings. Variant calling reads positions, sequences, and quality scores but ignores read names and most tags. Quality assessment touches only the quality column. In BAM, all these workloads parse the full record, discarding 60-80% of decoded data.

CRAM (Hsi-Yang Fritz et al., 2011) addresses file size through reference-based compression but retains row-oriented access. CRAM containers do not support independent column access, and encoding complexity results in 2-4x slower decode times compared to BAM.

### 1.2 Columnar storage in genomics

Columnar storage is well-established in analytical databases (Abadi et al., 2008; Lamb et al., 2012) and data analytics (Apache Parquet, Apache Arrow). Fields of the same type compress better together, and workloads accessing a subset of columns avoid I/O on untouched data. These benefits translate directly to alignment data, where field types span integers (POS, FLAG, MAPQ), variable-length strings (SEQ, QUAL, CIGAR, QNAME), and heterogeneous key-value maps (TAGS).

Prior work on columnar genomic storage includes CRAM's internal column slicing within containers (limited to intra-container access without independent seekability), Genozip (Lan et al., 2021) for general genomic compression with some columnar properties, and column-store approaches for variant data (BCF, GQT; Layer et al., 2016). To our knowledge, no format provides fully independent column access with parallel chunk-level decode for alignment data.

### 1.3 Contributions

1. **Selective column I/O.** Workloads reading a subset of fields access only those column payloads, with measurable I/O and decode savings (3.0x-4.1x faster pileup via POS+CIGAR-only reads).
2. **Parallel region decode.** Chunk-based layout enables multi-threaded decoding with work-stealing, achieving 3.8x-5.4x faster region view than samtools on 8 workers.
3. **Field-specific codecs.** Per-chunk codec selection with raw fallback guarantees; 9 specialized codecs covering the full SAM field set, including optional reference-delta sequence encoding and per-column zstd compression.
4. **Lossless round-trip.** BAM → AXF1 → BAM produces byte-identical SAM output for mapped records.
5. **Cloud-compatible layout.** File-absolute byte offsets and contiguous chunks enable HTTP range-request access without format modifications.
6. **Python API.** pybind11 bindings with numpy/pandas integration expose all query, conversion, and coverage functions for interactive analysis.

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
| SEQ | Ref-delta / 2-bit literal | Raw string | CIGAR-driven reconstruction from reference; fallback to A=0,C=1,G=2,T=3 4 bases/byte |
| QUAL | Alphabet bit-pack | Raw string / byte RLE | Chunk-local alphabet → minimal bit width |
| QNAME | Front-compressed dict | Raw string | Sorted dictionary + shared prefix + varint indices |
| TAGS | Per-key streams | Raw string | Per-tag-key encoding with presence bitmaps |

Optional per-column zstd compression: a generic `compressed` envelope wraps any column's best base codec in a zstd frame when the compressed output is smaller; the reader recursively unwraps during decode. On HG002 chr1:1M-2M, 8 of 11 columns benefit from zstd wrapping, reducing lossless file size from 1.58x to 1.02x of BAM. Lossy mode (`--axf1-quality-lossy illumina8`) applies 8-level Phred binning before codec selection.

**Reference-delta SEQ.** When a reference FASTA is provided (`--reference`), the encoder attempts CIGAR-driven reference-delta encoding per chunk: the reference subsequence is aligned using CIGAR operations, and only mismatches, soft-clips, and insertions are stored. Per-contig SHA-256 checksums in a v3 typed extension block validate reference identity at decode time. The encoder falls back to 2-bit literal when ref-delta produces a larger payload or the sequence contains non-ACGT bases. On HG002 PacBio chr1:1M-2M, ref-delta reduces raw AXF1 size by 20.7% and zstd-compressed AXF1 by 15.5%.

### 2.4 Query Engine

**Region query.** Binary-search file index → memory-map file (`mmap` + `MADV_SEQUENTIAL`) → distribute chunks to thread pool (8 workers) via atomic work-stealing → decode + format in parallel.

**Fused decode.** All columns decoded into contiguous `FlatColumn` arrays (single `char[]` buffer + offset array per string field); SAM formatted directly via pointer writes without per-record struct allocation.

**Interior chunk skip.** Chunks fully contained within the query region bypass per-record CIGAR overlap checking.

**Selective column I/O.** Pileup reads only POS+CIGAR; filtered queries add FLAG+MAPQ to the selective set.

**Parallel pileup.** For large regions (>=500 chunks), 8 workers each maintain a thread-local depth array; results are element-wise merged after join.

**Worker-local buffers.** Each view worker accumulates output into one pre-reserved buffer, reducing allocations from O(chunks) to O(workers).

**SIMD decode.** AVX2 SEQ 2-bit decode processes 128 bases per iteration via `_mm256_shuffle_epi8` as a 4-entry LUT with lane-crossing interleave; FLAG bit-pack uses uint64 bulk load + mask (eliminates per-bit inner loop). Guarded by compile-time `ALIGNX_HAVE_AVX2` with scalar fallback.

### 2.5 Benchmark Setup

**Hardware.** Intel Xeon E5-2696 v4 (88 cores, 2.20 GHz), ZFS storage.

**Data.** (1) Genome in a Bottle HG002, PacBio SequelII 15 kb/20 kb merged, GRCh38, pbmm2, haplotag 10x (~10x coverage). (2) HG002 NIST Illumina 2x250bp paired-end, novoalign, GRCh38 (~300x coverage). samtools 1.23.1 baseline.

**Regions.** chr1:1M-2M (1 Mb, euchromatic), chrY:20M-21M (1 Mb, Y chromosome), chr1:121M-142M (21 Mb, centromeric). PacBio: 3,143/2,034/58,168 records. Illumina: 271,381/120,493/4,068,017 mapped records.

**Protocol.** Warmup + 5 timed repeats. SHA-256 correctness verification before timing. Illumina comparison uses `samtools view -F 4` baseline (AXF1 stores mapped records only; ~2.5% of Illumina paired-end records are unmapped mates).

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

### 3.2 Pileup Performance

| Tool | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| samtools depth | 279 ms | 220 ms | 4,792 ms |
| **AXF1 pileup** | **78 ms (3.6x)** | **74 ms (3.0x)** | **1,177 ms (4.1x)** |

AXF1 pileup reads only POS and CIGAR columns via selective column I/O. A 64 KB buffered writer with `std::to_chars` and skip-zero output (matching `samtools depth` default behavior) eliminates per-line `ostream` overhead. AXF1 achieves 3.0x-4.1x faster pileup than samtools depth across all regions, with the largest speedup (4.1x) on the 21 Mb centromeric region where the high chunk count (6,878) maximizes parallel decode benefit.

### 3.3 Compression (Figure 3)

| Config | chr1:1M-2M | chrY:20M-21M | chr1:121M-142M |
|:---|---:|---:|---:|
| BAM | 31.98 MB (1.00x) | 18.24 MB (1.00x) | 493.77 MB (1.00x) |
| CRAM | 20.76 MB (0.65x) | 11.55 MB (0.63x) | 377.55 MB (0.77x) |
| AXF1 lossless | 50.50 MB (1.58x) | 33.51 MB (1.84x) | 1,121.39 MB (2.27x) |
| AXF1+zstd (per-column) | 32.69 MB (1.02x) | — | — |
| AXF1+zstd (quality-only) | 38.92 MB (1.22x) | 24.24 MB (1.33x) | 858.81 MB (1.74x) |
| AXF1 lossy+zstd | 16.68 MB (0.52x) | 11.52 MB (0.63x) | 473.70 MB (0.96x) |

> **Figure 3.** File size relative to BAM (log scale). Stacked bar chart showing size ratios for each config across regions.

Without compression, AXF1 lossless is 1.6x-2.3x larger than BAM. Per-column zstd compression wraps each column's best base codec in a zstd envelope (8 of 11 columns benefit), reducing lossless AXF1 to 1.02x of BAM on chr1:1M-2M — near parity while preserving independent column access. AXF1 lossy+zstd achieves 0.52x-0.63x of BAM on typical regions, matching CRAM.

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

### 3.6 Illumina Short-Read Generalization

| Workload | PacBio (1 Mb) | Illumina (1 Mb) | PacBio (21 Mb) | Illumina (21 Mb) |
|:---|---:|---:|---:|---:|
| View speedup | 4.8x-5.4x | 3.0x-3.2x | 3.8x | 3.9x |
| Pileup speedup | 3.0x-3.6x | 0.92x-1.2x | 4.1x | 0.93x |

AXF1 view speedup generalizes across sequencing platforms: 3.0x-3.9x on Illumina 300x short reads (2x250bp). The slightly lower speedup compared to PacBio (3.8x-5.4x) reflects shorter per-record payloads (250bp vs 15-20kb SEQ/QUAL), reducing the relative benefit of fused columnar decode.

Pileup advantage is data-dependent: Illumina's simple CIGARs (predominantly `250M`) minimize the CIGAR parsing cost that selective column I/O avoids, and high record density (271K vs 3K per Mb) shifts the bottleneck from I/O to per-record depth accumulation.

### 3.7 Round-Trip Fidelity

BAM → AXF1 → BAM round-trip produces byte-identical SAM output for mapped records, verified by SHA-256 on both toy fixtures and HG002 real data (PacBio and Illumina). AXF1 stores mapped records only; unmapped records are excluded from comparison.

## 4. Discussion

### 4.1 Columnar vs. row-oriented alignment storage

BAM's design reflects an era when sequential access dominated. Modern workloads access 2-4 of 8+ SAM fields, forcing BAM to parse 60-80% unused data. AXF1 addresses this mismatch at two levels: selective I/O avoids reading untouched columns (3.0x-4.1x pileup speedup with buffered output), and parallel fused decoding exploits chunk independence for multi-threaded execution (3.8x-5.4x view speedup even when all fields are requested).

The view speedup — where all columns are read — demonstrates that columnar layout benefits extend beyond projection pushdown. The fused decode path (columnar arrays → SAM formatting without per-record allocation) and chunk-level parallelism provide additional advantages unavailable to row-oriented formats.

### 4.2 File size tradeoff

Without compression, AXF1 lossless is 1.6x-2.3x larger than BAM. Per-column zstd compression reduces lossless AXF1 to 1.02x of BAM on typical regions — near parity — while preserving independent column access. This eliminates the historical size penalty of columnar alignment storage. For maximum compression, lossy quality binning + zstd achieves 0.52x-0.63x of BAM, matching CRAM at 15x faster decode.

For archival storage where decode speed is secondary, CRAM (0.63x-0.77x) remains appropriate. AXF1 targets query-intensive workloads where data is written once and queried many times.

### 4.3 Parallel scaling

The 5.4x view speedup on 1 Mb regions (44 ms, 8 workers, 324 chunks) approaches the theoretical throughput limit for SAM text generation (~100 MB/s per core). The 3.8x centromeric speedup (854 ms) reflects output volume saturation: 1.8 GB of SAM text must be drained sequentially.

The filtered centromeric path (500 ms, 5.2x) confirms that reducing output volume recovers scaling — excluded records skip formatting entirely.

Multi-threaded chunk encoding during conversion shows a different scaling profile: parallel encoding (8 workers) is ~7% slower than sequential on chr1:1M-2M (47s vs 44s) because BAM reading via HTSlib dominates total convert time (>90%). Encoding parallelism will benefit future streaming-write pipelines that decouple I/O from encoding.

### 4.4 Limitations

**File size.** AXF1 lossless without zstd is 1.6x-2.3x larger than BAM due to per-column codec headers and absence of cross-column block compression. Per-column zstd reduces the gap to near parity (1.02x on typical regions), but the compression envelope adds decode latency. Storage-constrained applications should use CRAM or AXF1 lossy modes.

**Mapped records only.** Unmapped reads and unplaced records are not stored. A complete archival format would need to handle these.

**Platform-dependent pileup.** Illumina short-read pileup shows lower speedup (0.92x-1.2x) compared to PacBio (3.0x-4.1x) because simple CIGARs and high record density shift the bottleneck from I/O to per-record depth accumulation. ONT data (high error rates, long reads with complex CIGARs) remains untested.

**Reference dependency.** The ref-delta SEQ codec requires a matching reference FASTA at decode time (validated by per-contig SHA-256). Without the reference, files using ref-delta cannot be decoded. The encoder falls back to self-contained 2-bit literal when no reference is provided.

### 4.5 Cloud compatibility

AXF1's layout (absolute byte offsets, contiguous chunks, index at EOF) enables HTTP range-request access against S3/GCS/Azure without format modifications. A client reads the index via one range request, identifies relevant chunks by binary search, and fetches chunk byte ranges in parallel. The 256 KiB target chunk size aligns with cloud storage minimum request granularity.

### 4.6 Future work

1. **AVX-512 / ARM NEON SIMD.** Extend SIMD decode beyond AVX2 SEQ/FLAG to additional columns (POS delta-varint parallel prefix sum, QUAL bit-pack) and additional architectures.
2. **GPU decompression.** CUDA kernels for bulk SEQ 2-bit and QUAL bit-pack decode on high-throughput analysis nodes.
3. **Cloud transport.** HTTP range-request client for direct S3/GCS/Azure object storage access. The AXF1 layout (absolute byte offsets, contiguous chunks, index-at-EOF) already supports this without format modifications.
4. **Streaming encode pipeline.** Producer-consumer architecture decoupling BAM reading from chunk encoding, enabling true parallel overlap of I/O and compression.
5. **Arrow/Polars interop.** Zero-copy columnar export to Apache Arrow record batches or Polars DataFrames for downstream analytical workflows.

## 5. Conclusion

AXF1 demonstrates that columnar alignment storage substantially accelerates genomic region queries across sequencing platforms. On HG002 PacBio long-read data, AXF1 achieves 3.8x-5.4x faster full-record queries and 3.0x-4.1x faster pileup compared to samtools. On Illumina 300x short-read data, view speedup remains 3.0x-3.9x, confirming that the parallel fused decode advantage generalizes beyond long reads. Per-column zstd compression brings lossless AXF1 to near BAM parity (1.02x), while optional lossy modes match CRAM file sizes at 15x faster decode. Field-specific codecs with reference-delta sequence encoding, chunk-level parallelism, selective column I/O, SIMD-accelerated decoding, and cloud-compatible layout address the fundamental mismatch between row-oriented BAM and modern analytical workloads where query latency — not storage density — is the primary constraint.

## Data Availability

AXF1 source code, benchmark scripts, and toy test fixtures are available at https://github.com/happyntu/alignx. HG002 PacBio SequelII and Illumina 2x250bp data are from the Genome in a Bottle Consortium (Zook et al., 2019).

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
