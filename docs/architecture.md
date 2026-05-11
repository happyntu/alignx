# Architecture

## Module map

```text
alignx/
  src/
    io/          BAM/SAM/CRAM reader and writer (HTSlib wrapper)
                 mmap reader for .axf blocks
    index/       BAI/CSI reader; AXF multi-level index builder, writer, reader
    format/      .axf columnar block layout; chunk header/footer; file magic
    compress/    quality-score specialized codec
                 CIGAR delta encoding
                 read-name dictionary compression
                 position delta (varint) stream
    query/       region query engine (BAM + AXF)
                 read filter: FLAG / MAPQ / tag / pair-aware
    analysis/    coverage calculator
                 pileup engine
                 stats collector (insert size, flag distribution, MAPQ dist)
    convert/     BAM/CRAM → AXF pipeline
                 AXF → BAM/CRAM pipeline
    cli/         subcommand dispatch: convert / view / export / pileup / stats / index
    bench/       wall-clock timer, RSS measurement, throughput metrics
```

## Data flow

### BAM region query (v0.1)
```
alignx view sample.bam chr1:1M-2M
  → cli: parse region string → GenomicInterval
  → io:  BamReader::open(path)
  → index: AXFIndex::query(interval) OR BaiReader::query(interval)
  → io:  BamReader::fetch(virtual_offset_range)
  → query: RecordFilter::apply(FLAG, MAPQ, ...)
  → cli: RecordFormatter::print(stdout, SAM/AXF)
```

### BAM → AXF conversion (v0.3)
```
alignx convert sample.bam -o sample.axf
  → io:  BamReader::stream_all()
  → convert: AxfEncoder::begin_chunk(reference_id, start)
  → compress: QualCodec::encode(qual), CigarDelta::encode(cigar)
              PosStream::encode(pos), ReadNameDict::encode(qname)
  → format: AxfChunkWriter::flush(block)
  → index:  AXFIndex::add_chunk(chunk_meta)
  → format: AxfFileWriter::finalize(index)
```

### AXF region query (v0.3+)
```
alignx view sample.axf chr1:1M-2M
  → index: AXFIndex::query(interval) → [chunk_offset, ...]
  → io:    AxfBlockReader::read(chunk_offset) → ColumnarChunk
  → query: columnar filter: read only POS/FLAG/MAPQ columns
  → cli:   RecordFormatter::print(stdout)
```

## AXF format sketch

```
File magic: "AXF1\0" (5 bytes)
File header: version, ref_count, chunk_count, index_offset
Chunks (sorted by ref_id, pos):
  ChunkHeader: ref_id, start_pos, end_pos, record_count, column_offsets[]
  Columns (independent, seekable):
    POS    (varint delta stream)
    FLAG   (bit-packed)
    MAPQ   (byte array or RLE)
    CIGAR  (delta + op dictionary)
    SEQ    (reference-delta encoded, 2-bit)
    QUAL   (specialized codec: Illumina 8-level / raw / zstd)
    QNAME  (dictionary encoded)
    TAGS   (per-tag streams, optional)
  ChunkFooter: crc32
Index:
  BinIndex: ref_id → [(start, end, chunk_file_offset)]
  ColumnIndex: per-column offset within each chunk
```

## Source of truth

1. `docs/adr/`
2. `docs/architecture.md` (this file)
3. `docs/roadmap.md`
4. Implementation in `src/` and `tests/`
5. Exploratory notes in `docs/research/`
