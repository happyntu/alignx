# Roadmap

## Version strategy

alignx follows an incremental release strategy. Each version must be benchmarkable
and produce correct output before the next version starts.

---

## v0.1 — BAM reader + faster region query

**Goal:** Prove that a new index can accelerate BAM region queries vs. BAI.

**Deliverables:**

- [x] HTSlib wrapper: `BamReader` with `open`, `fetch(region)`, `next_record`
- [x] BAI/CSI index reader
- [x] `AXFIndex` v1: chunk-level bin index stored as separate `.axf.idx` file
- [x] `alignx view <bam> <region>` — outputs SAM to stdout
- [x] `alignx stats <bam>` — flag/MAPQ/insert size distribution
- [x] `alignx index <bam>` — writes projected `.axf.idx` from `.bai` / `.csi`
- [x] Unit tests: BamReader round-trip on toy BAM
- [x] Engineering benchmark: `alignx view` vs `samtools view` on HG002 chr1:1M-2M

**Tech notes:**
- HTSlib via vcpkg. Windows: confirm `htslib::htslib` target resolves.
- HG002 chr1 benchmark results are recorded in
  `docs/research/phase1-bam-view-findings.md`; they are not paper-grade claims.

---

## v0.2 — Coverage and filter acceleration

**Goal:** Faster coverage and per-read filter than `samtools depth` / `samtools view -F`.

**Deliverables:**

- [ ] `alignx pileup <bam> <region>` — per-base coverage
- [ ] Read filter: `--flag-exclude`, `--min-mapq`, `--tag`
- [ ] Benchmark: vs `samtools depth`, `mosdepth` on chr1
- [ ] Integration test: coverage array matches samtools depth output exactly

---

## v0.3 — AXF columnar cache format

**Goal:** Write and read the `.axf` columnar format. Prove selective column I/O is faster.

**MVP staging:** ADR-005 defines a correctness-first AXF MVP before the full
columnar codec stack. The first closed loop may store row-preserving SAM-line
payloads in indexed AXF blocks to validate conversion, indexing, CLI routing,
and region-query correctness.

**Deliverables:**

- [x] AXF MVP file header, reference metadata, block payloads, and block index
- [x] `alignx convert <bam> -o <axf>` MVP
- [x] `alignx view <axf> <region>` MVP with toy stdout parity
- [x] AXF1 raw-column toy correctness path with magic-based view routing
- [x] AXF1 hybrid chunk sizing correctness smoke on HG002 chr1 small region
- [x] AXF1 region-converted subset boundary smoke on HG002 chr1 small region
- [x] AXF1 v2 file metadata records source path and subset conversion region
- [x] AXF1 v2 metadata smoke on HG002 chr1 small region
- [x] AXF1 v2 metadata corruption tests for full-file and metadata-only readers
- [x] AXF1 source identity design note
- [x] AXF1 metadata extensibility design note
- [x] AXF1 POS delta-varint codec with raw fallback for non-monotonic chunks
- [x] AXF1 POS delta-varint smoke on HG002 chr1 small region
- [x] AXF1 FLAG bit-pack codec with raw fallback
- [x] AXF1 FLAG bit-pack smoke on HG002 chr1 small region
- [x] AXF1 MAPQ RLE codec with raw fallback
- [x] AXF1 MAPQ RLE smoke on HG002 chr1 small region
- [x] AXF1 SEQ codec design note
- [x] AXF1 SEQ 2-bit literal codec with raw fallback
- [x] AXF1 SEQ 2-bit literal toy smoke
- [x] AXF1 SEQ 2-bit literal smoke on HG002 chr1 small region
- [x] AXF1 CIGAR codec design note
- [x] AXF1 CIGAR token stream codec with raw fallback
- [x] AXF1 CIGAR token stream toy smoke
- [x] AXF1 CIGAR token stream smoke on HG002 chr1 small region
- [x] AXF1 QUAL codec design note
- [x] AXF1 QUAL byte RLE codec with raw fallback
- [x] AXF1 QUAL byte RLE toy smoke
- [x] AXF1 QUAL byte RLE smoke on HG002 chr1 small region
- [x] AXF1 next QUAL codec design note
- [x] AXF1 QUAL alphabet bit-pack codec with raw fallback
- [x] AXF1 QUAL alphabet bit-pack toy smoke
- [x] AXF1 QUAL alphabet bit-pack smoke on HG002 chr1 small region
- [x] AXF1 compressed payload wrapper design note
- [x] AXF1 `qual_pack_compressed` stored-envelope reader path with raw/base writer fallback
- [x] AXF1 zstd payload wrapper feature-gate design
- [x] AXF1 zstd feature-gate skeleton with disabled-build rejection path
- [x] AXF1 zstd compressed payload envelope reader
- [x] Explicit AXF1 zstd quality payload writer path and CLI flag
- [x] AXF1 zstd quality writer smoke on HG002 chr1 small region
- [x] AXF1 zstd quality writer size policy with raw/base fallback
- [x] AXF1 zstd quality writer size-policy smoke on HG002 chr1 small region
- [x] Reusable AXF1 codec smoke script supports zstd quality compression option
- [x] Scripted AXF1 zstd quality writer smoke on HG002 chr1 small region
- [ ] Optional LZ4 compressed payload writer path and size policy
- [ ] `AxfFileWriter`: chunk header, column streams, chunk footer, file index
- [ ] `AxfFileReader`: chunk seek, per-column read
- [ ] Codec design: SEQ reference-delta with reference identity metadata
- [x] Production AXF1 chunk sizing policy design: byte budget, genomic span, record count hybrid
- [x] Implement production AXF1 hybrid chunk sizing in converter
- [ ] Tune AXF1 chunk sizing thresholds on HG002-style data
- [ ] Round-trip fidelity: BAM → AXF → BAM → diff
- [ ] Benchmark: AXF coverage (POS only) vs BAM full-record parse on chr1

**SIMD option (off by default):**
- AVX2 delta decode of POS stream

---

## v0.4 — CRAM support + cloud-friendly index

**Goal:** Read CRAM; index supports HTTP range requests for object storage.

**Deliverables:**

- [ ] CRAM reader via HTSlib
- [ ] `alignx export <axf> -o <bam|cram>`
- [ ] AXF index: HTTP-range-friendly chunk map (contiguous offsets, no random seeks)
- [ ] Integration test: CRAM → AXF → BAM fidelity

---

## v1.0 — Full format + benchmark paper

**Goal:** Production-ready AXF with QUAL and CIGAR codecs. Benchmarkable against CRAM.

**Deliverables:**

- [ ] QUAL codec: stronger lossless model or compressed wrapper; lossy binning only under explicit lossy profile
- [ ] CIGAR delta + op dictionary encoding
- [ ] QNAME dictionary encoding
- [ ] TAG per-stream encoding
- [ ] `alignx index` — rebuild `.axf.idx` from existing `.axf`
- [ ] Compression benchmark: AXF vs BAM vs CRAM (ratio, encode time, decode time)
- [ ] Query benchmark: AXF vs BAM on region / coverage / pileup / filter workloads
- [ ] Draft methods section for benchmark paper

**Phase 2+ (post v1.0):**
- SIMD AVX2/AVX-512 decompression paths
- GPU CUDA decompression (experimental)
- Cloud object storage range-query client
- Python bindings (pybind11)
