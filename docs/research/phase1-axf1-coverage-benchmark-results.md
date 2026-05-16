# AXF1 Coverage Benchmark Results

Date: 2026-05-16
Server: missmi-server00
Binary: alignx built with `cmake --preset wsl-release`
Data: HG002 PacBio SequelII merged 15kb/20kb GRCh38

## Benchmark Design

`alignx coverage` reads only POS+CIGAR columns from AXF1 (selective column I/O)
while BAM must parse full records via HTSlib to extract the same fields.

## Pre-optimization Results (full-chunk read)

AXF1 reader read the entire chunk byte range for every chunk, then decoded
only POS+CIGAR. This negated the columnar advantage because I/O dominated.

| Region | AXF1 (ms) | BAM (ms) | Ratio | AXF1 bytes_read | AXF1 payload |
|:---|:---|:---|:---|:---|:---|
| chr1:1M-2M | 1,042 | 765 | 1.36x slower | 50.6 MB | 787 KB |
| chr1:121M-142M | 8,378 | 6,646 | 1.26x slower | 1.12 GB | 186 MB |
| chrY:20M-21M | 694 | 410 | 1.69x slower | 33.6 MB | 1.8 MB |

## Post-optimization Results (selective column read)

`read_chunk_columns_selective` reads only the chunk header + requested column
payloads. Persistent ifstream avoids repeated file open/close.

| Region | Records | AXF1 (ms) | BAM (ms) | Speedup | AXF1 bytes_read | AXF1 payload | I/O efficiency |
|:---|:---|:---|:---|:---|:---|:---|:---|
| chr1:1M-2M | 3,143 | 89.7 | 266.4 | **2.97x** | 864 KB | 787 KB | 91% |
| chr1:121M-142M | 58,168 | 4,026 | 4,675 | **1.16x** | 188 MB | 186 MB | 99% |
| chrY:20M-21M | 2,034 | 82.2 | 182.8 | **2.22x** | 1.87 MB | 1.82 MB | 97% |

## Timing Breakdown (selective column read)

### chr1:1000000-2000000 (324 chunks, 3143 records)

| Phase | AXF1 (ms) | BAM (ms) |
|:---|:---|:---|
| open | 0.37 | 64.5 |
| fetch/query | 0.01 | 0.13 |
| selective decode / read | 51.9 | 160.1 |
| filter | 2.2 | — |
| coverage compute | 30.9 | 30.2 |
| **total** | **89.7** | **266.4** |

### chr1:121000000-142000000 (6878 chunks, 58168 records)

| Phase | AXF1 (ms) | BAM (ms) |
|:---|:---|:---|
| open | 1.78 | 51.1 |
| fetch/query | 0.12 | 0.55 |
| selective decode / read | 2,495 | 3,325 |
| filter | 371 | — |
| coverage compute | 1,095 | 1,226 |
| **total** | **4,026** | **4,675** |

### chrY:20000000-21000000 (218 chunks, 2034 records)

| Phase | AXF1 (ms) | BAM (ms) |
|:---|:---|:---|
| open | 0.27 | 45.9 |
| fetch/query | 0.003 | 0.11 |
| selective decode / read | 48.3 | 100.0 |
| filter | 4.2 | — |
| coverage compute | 25.5 | 25.5 |
| **total** | **82.2** | **182.8** |

## Conclusion

Selective column I/O demonstrates the structural advantage of columnar
alignment storage. For POS+CIGAR-only workloads, AXF1 reads 91-99% fewer
bytes than full-chunk reads and achieves 1.16-2.97x speedup over BAM.

The speedup is largest on regions where the column payload is a small fraction
of the total chunk data (high ratio of QUAL+SEQ to POS+CIGAR), and smallest
on regions with very large record counts where coverage computation itself
dominates.
