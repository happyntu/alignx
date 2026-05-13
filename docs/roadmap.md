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
- [ ] Benchmark: `alignx view` vs `samtools view` on chr1:1M-2M

**Tech notes:**
- HTSlib via vcpkg. Windows: confirm `htslib::htslib` target resolves.
- No AXF format in this version; index only.

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
- [ ] `AxfFileWriter`: chunk header, column streams, chunk footer, file index
- [ ] `AxfFileReader`: chunk seek, per-column read
- [ ] Codec: POS varint delta, FLAG bit-pack, MAPQ byte/RLE, SEQ reference-delta 2-bit
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

- [ ] QUAL codec: Illumina 8-level binning + zstd
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
