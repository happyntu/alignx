# ADR-004: 壓縮與硬體加速策略 — CPU-first、GPU optional、欄位專用無損壓縮

| 欄位 | 內容 |
|---|---|
| 狀態 | **已決議 (Accepted)** |
| 日期 | 2026-05-11 |
| 決策者 | cklin |
| 影響範圍 | `src/compress/`, `src/format/`, `src/query/`, `src/convert/` |

---

## 背景

alignx 的目標是提升 alignment region query、coverage、stats、filter 等工作負載的 I/O 與解碼效率。
這些工作負載常只需要部分欄位，例如 POS、CIGAR、MAPQ、FLAG，而不需要完整解碼 SEQ、QUAL、TAGS。

因此，AXF 的格式價值不應建立在單一 compression codec 或特定硬體上，而應建立在：

- columnar layout
- chunk-level random access
- selective column I/O
- 欄位專用 transform
- 可選的 SIMD/GPU 加速

---

## 決議

**alignx 採用 CPU-first 設計；GPU 是可選加速路徑，不是格式或功能依賴。**

所有 AXF 檔案必須可在一般 CPU 上完整讀寫與查詢。GPU/CUDA 不得成為格式正確性、解碼能力、
或使用者工作流程的必要條件。

執行路徑分層如下：

| 層級 | 定位 | 要求 |
|---|---|---|
| Scalar CPU | correctness baseline | 必須完整支援 |
| SIMD CPU | hot-path acceleration | AVX2/AVX-512 等可選 |
| GPU | batch acceleration | experimental，可缺席 |

GPU 只適合大量 chunk 的批次工作，例如 coverage、pileup、批次轉檔或大規模 column decode。
小區間 random query 通常受 I/O、seek、chunk 解壓與 kernel dispatch overhead 限制，不應依賴 GPU。

---

## 無損壓縮原則

AXF 的無損壓縮策略先降低 entropy，再進行 entropy coding。

優先順序：

1. **欄位分離**：POS、FLAG、MAPQ、CIGAR、SEQ、QUAL、QNAME、TAGS 各自成 stream。
2. **可逆 transform**：delta、frame-of-reference、bit-pack、RLE、dictionary、tokenization、reference-delta。
3. **欄位專用模型**：利用 reference、position、read group、tag type、pair 狀態等 context。
4. **entropy coding**：rANS/tANS/FSE、Huffman、arithmetic coding，依 speed/ratio profile 選擇。
5. **chunk 獨立性**：每個 chunk 必須可獨立解碼，保留 random access 和 cloud range query 能力。

AXF 不以 lossless size 贏過 CRAM 為主要目標。CRAM 3.1 是壓縮率與生態相容性的主要比較基準。
AXF 的主要目標是對 region query、coverage、stats、filter 等工作負載少讀、少解碼、少搬資料。

---

## 欄位策略

| 欄位 | 無損 transform | 建議 entropy/fallback |
|---|---|---|
| POS | sorted delta、varint、frame-of-reference bit-pack | raw/bit-pack；SIMD delta decode |
| FLAG | bit-plane、bit-pack、common pattern dictionary | RLE 或 small alphabet coding |
| MAPQ | byte stream、small alphabet pack、RLE | rANS/zstd fallback |
| CIGAR | op stream + length stream、common CIGAR dictionary、length delta | rANS/zstd fallback |
| SEQ | 先採 self-contained 2-bit literal；reference-delta 待 reference identity metadata 與 CIGAR/strand 語意設計後再導入 | rANS/zstd fallback |
| QUAL | lossless raw quality stream；context model by cycle/read group | FQZComp-like model、rANS/arithmetic、zstd fallback |
| QNAME | tokenizer：prefix、instrument/run/lane/tile/x/y、pair suffix、numeric delta | dictionary + integer streams |
| TAGS | per-tag stream；numeric delta/varint；string dictionary/tokenizer | zstd fallback for unknown tags |

Quality score binning 只有在輸入資料本來已經 binned，或使用者明確選擇 lossy profile 時才允許。
預設 AXF 必須是 lossless round-trip。

---

## Codec Profiles

AXF 應支援 profile，而不是單一固定 codec。

| Profile | 目標 | 策略 |
|---|---|---|
| `fast` | query latency / cache format | bit-pack、varint、RLE、LZ4 或低階 zstd |
| `normal` | speed/size balance | 欄位 transform + zstd/rANS |
| `small` | 較高壓縮率 | 更強 context model、rANS/arithmetic、較高 zstd level |
| `archive` | 長期保存 | 可用高 CPU 成本 codec；仍需保持 chunk random access |

v0.3 先實作簡單、可驗證、可 benchmark 的 codec：

- POS varint delta
- FLAG bit-pack
- MAPQ raw byte/RLE
- SEQ 2-bit literal with raw fallback

v1.0 再加入 QUAL、CIGAR、QNAME、TAGS 的完整專用 codec。

---

## 與既有格式的關係

| 格式 | 角色 |
|---|---|
| BAM/BGZF | 相容性 baseline；BGZF 提供 blocked gzip random access，但 row-oriented |
| CRAM 3.1 | 壓縮率與專用 codec baseline；支援 column-like data series 與 data-type-specific codecs |
| AXF | selective column I/O、cloud-friendly chunk offset、SIMD/GPU-friendly decode path |

AXF 必須 benchmark：

- size ratio
- encode time
- decode time
- region query latency
- coverage/pileup throughput
- stats/filter workload latency

比較對象至少包含：

- BAM/BGZF
- CRAM 3.1 normal
- CRAM 3.1 small/archive
- AXF fast/normal/small

---

## 後果

### 正面

- 沒有 GPU 的使用者仍可完整使用 alignx。
- 格式長期穩定，不被 CUDA、driver、vendor 或特定硬體綁死。
- SIMD/GPU 可在不破壞格式的情況下逐步加入。
- 欄位專用 transform 更符合 alignment data 的資訊結構。
- benchmark 可清楚區分「壓縮率」與「查詢效率」兩個目標。

### 負面

- 需要維護多個 codec profile 和 fallback path。
- lossless QUAL 壓縮困難，AXF size 不一定能贏 CRAM。
- GPU path 需要額外資料搬移與 batch scheduling 設計，短 query 不一定受益。
- 欄位專用 codec 會增加 converter 和 round-trip 測試負擔。

---

## References

- HTS specifications: https://samtools.github.io/hts-specs/
- CRAM 3.1 paper: https://doi.org/10.1093/bioinformatics/btac010
- HTSlib CRAM benchmarks: https://www.htslib.org/benchmarks/CRAM.html
- Zstandard format, RFC 8878: https://www.rfc-editor.org/rfc/rfc8878.html
- LZ4 documentation: https://lz4.org/
