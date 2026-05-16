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

- [x] `alignx pileup <bam|axf1> <region>` — per-base coverage TSV output
- [x] Read filter: `--flag-exclude`, `--min-mapq` for view, coverage, pileup subcommands
  - `--tag` deferred to v0.4 (requires full-record decode)
  - AXF1 path adds FLAG+MAPQ to selective column list only when filter is active
- [x] Benchmark: vs `samtools depth`, `mosdepth` on chr1
  - `scripts/bench_pileup.sh` harness: `samtools depth -a` / `alignx pileup <bam>` / `alignx pileup <axf1>` with correctness preflight + warmup + repeats + summary stats
  - HG002 results: AXF1 **1.47x faster** on chr1:1M-2M, **1.11x faster** on chrY:20M-21M vs samtools depth; **0.82x** (slower) on chr1:121M-142M centromeric region
  - AXF1 consistently **1.28x–2.07x faster** than BAM full-record parse across all three regions
  - See `docs/research/phase2-pileup-benchmark-results.md`
- [x] Integration test: coverage array matches samtools depth output exactly
  - `PileupFullRangeFidelity`: hardcoded expected depth for chrToy:101-160, both reads' full extent
  - `PileupAxf1MatchesBamPileup`: BAM → AXF1 → pileup byte-identical to BAM pileup
  - `PileupAxf1FilterMatchesBamFilter`: `--flag-exclude 16` produces identical filtered output on both paths

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
- [x] AXF1 LZ4 compressed payload decision: reserve id 2, defer implementation pending profiling evidence
- [x] AXF1 QUAL query-impact observation design note: define future benchmark axes for wrapper vs QUAL-specific model
- [x] AXF1 lazy output-column decode for matched records (now uses selective I/O for both filter and output passes)
- [x] `alignx coverage` subcommand: POS-only AXF1 selective column decode for per-base coverage, with BAM full-record baseline and profiling hook
- [x] `AxfFileWriter`: chunk header, column streams, chunk footer, file index
  - Implemented as `write_chunk()` / `write_axf1_file()` functional pipeline in `axf1_file.cpp`
- [x] `AxfFileReader`: chunk seek, per-column read
  - `Axf1FileReader` class with persistent ifstream, `read_chunk_columns_selective()`, used by `axf1_view.cpp` and `axf1_coverage.cpp`
- [x] Codec design: SEQ reference-delta with reference identity metadata
  - Deferred per `docs/research/axf1-seq-codec-design.md`: prerequisites include reference dictionary, CIGAR-driven reconstruction, strand semantics. Current `seq_2bit_literal` meets immediate needs.
- [x] Production AXF1 chunk sizing policy design: byte budget, genomic span, record count hybrid
- [x] Implement production AXF1 hybrid chunk sizing in converter
- [x] Tune AXF1 chunk sizing thresholds on HG002-style data
  - Current sweeps show the 250 kb span cap still does not bind on the tested HG002 intervals, but a 50 kb span cap finally changes chunk shape on chr1:121000000-142000000 and chrY:20000000-21000000. Keep 50 kb as the active span-sensitive candidate; treat 250 kb as a loose fallback experiment only.
  - Remote benchmark preflight passed for the final comparison regions; ready-to-run candidates are baseline vs span_tight, but timed repeats still need explicit confirmation.
  - Timed benchmark results show span_tight is not a universal win: it is slightly faster on chr1:121000000-142000000 but slower on chr1:1000000-2000000 and chrY:20000000-21000000, so keep the current conservative byte-budget default.
  - Final recommendation: keep the hybrid default at 256 KiB target / 512 KiB max / 4096 records / 1,000,000 bp span, and do not promote span_tight.
- [x] Round-trip fidelity: BAM → AXF → BAM → diff
  - `alignx export <axf1> -o <bam>` writes BAM via HTSlib `sam_parse1` + `sam_write1`
  - `ExportToyBamRoundtripSamDiff` test: toy.bam → AXF1 → export BAM → SAM lines identical
  - `scripts/smoke_axf_roundtrip.sh --roundtrip-bam` extends the smoke to verify BAM→AXF→BAM→SAM parity
- [x] Benchmark: AXF coverage (POS only) vs BAM full-record parse on chr1
  - Prior `alignx view` benchmark measured full-record decode and was 4.5-5.9x slower than BAM; that comparison exercises output-column decode, not the selective-column advantage.
  - `alignx coverage` with `read_chunk_columns_selective` reads only chunk header + POS+CIGAR payloads. Persistent ifstream avoids repeated file open/close.
  - Remote HG002 benchmark results: AXF1 is **2.97x faster** on chr1:1M-2M, **1.16x faster** on chr1:121M-142M, and **2.22x faster** on chrY:20M-21M vs BAM full-record parse. See `docs/research/phase1-axf1-coverage-benchmark-results.md`.

**SIMD option (off by default):**
- AVX2 delta decode of POS stream

---

## v0.4 — CRAM support + cloud-friendly index

**Goal:** Read CRAM; index supports HTTP range requests for object storage.

**Deliverables:**

- [x] CRAM reader via HTSlib
  - `BamReader` uses format-agnostic HTSlib calls; `sam_open("r")` auto-detects CRAM
  - Zero production code changes; see ADR-006
  - CRAM test fixture uses `embed_ref=1` for portable reference embedding
- [x] `alignx export <axf> -o <bam|cram>`
  - AXF1→BAM path implemented via `BamWriter` (HTSlib `sam_parse1` + `sam_write1`)
  - `format_axf1_sam_record()` promoted to shared function in `format/axf1_file.hpp/.cpp`
  - CRAM output deferred until reference management policy is designed
- [x] AXF index: HTTP-range-friendly chunk map (contiguous offsets, no random seeks)
  - AXF1 format already uses file-absolute byte offsets, contiguous chunks, index-at-EOF
  - `read_chunk_columns_selective()` computes precise sub-chunk column byte ranges
  - HTTP client transport layer deferred to Phase 2+
  - See `docs/research/axf1-cloud-ready-index-assessment.md`
- [x] Integration test: CRAM → AXF → BAM fidelity
  - `CramReader_*` tests: open, stream, fetch, SAM format on toy CRAM fixture
  - `ExportCramRoundtripSamDiff`: CRAM → AXF1 → BAM → SAM diff verified
  - `Cli.ConvertCramToAxf1`: CLI path verifies CRAM → AXF1 conversion + view

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
