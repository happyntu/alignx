# ADR-002: AXF 格式設計 — 欄位分離的 Columnar Alignment Format

| 欄位 | 內容 |
|---|---|
| 狀態 | **已決議 (Accepted)** |
| 日期 | 2026-05-11 |
| 決策者 | cklin |
| 影響範圍 | `src/format/`, `src/compress/`, `src/convert/` |

---

## 背景

BAM 是 row-oriented 格式：每筆 read 的所有欄位（POS、FLAG、MAPQ、CIGAR、SEQ、QUAL、TAGS）
連續儲存在同一個 record 中。但大多數分析只需要部分欄位：

| 工作負載 | 實際需要的欄位 |
|---|---|
| coverage 計算 | POS、CIGAR、MAPQ |
| variant 前處理 | SEQ、QUAL、CIGAR |
| QC / flag 統計 | FLAG、MAPQ |
| duplicate marking | QNAME、FLAG、POS、TLEN |
| tag 篩選 | 特定 optional tags |

Row-oriented layout 強迫每次都讀取完整 record，即使只需要 2–3 個欄位。

---

## 決議

**AXF 採用 columnar block layout，每個 chunk 內各欄位獨立儲存，可選擇性讀取。**

---

## 格式規格（v0.3 target）

```
File magic:  "AXF1\0"  (5 bytes)
File header: [version u8][ref_count u32][chunk_count u64][index_offset u64]

Per chunk (sorted by ref_id, then start_pos):
  ChunkHeader:
    ref_id       u32
    start_pos    u32
    end_pos      u32
    record_count u32
    col_offsets  [N_COLS × u64]  (relative to chunk start)
  Columns (independently seekable):
    POS   — varint delta stream (sorted, always increasing within chunk)
    FLAG  — bit-packed u16 array
    MAPQ  — byte array or 2-bit if range ≤ 4 values
    CIGAR — op dictionary (u8) + length delta varint stream
    SEQ   — reference-delta 2-bit encoding (diff from reference)
    QUAL  — Illumina 8-level binning → 3-bit pack, or raw, or zstd
    QNAME — dictionary encoded: id→string table + u32 id stream
    TAGS  — per-tag streams (optional, tag-key indexed)
  ChunkFooter:
    crc32  u32

File index (at index_offset):
  BinIndex:    ref_id → [(start, end, chunk_file_offset)]
  ColumnIndex: chunk_id → col_offsets (mirrors ChunkHeader, for fast index scan)
```

---

## 壓縮策略

| 欄位 | Phase 0 (scaffold) | Phase 1 (v0.3) | Phase 2 (v1.0) |
|---|---|---|---|
| POS | — | varint delta | AVX2 delta decode |
| FLAG | — | bit-pack u16 | — |
| MAPQ | — | raw byte | RLE if repeated |
| CIGAR | — | op dict + delta | — |
| SEQ | — | reference delta 2-bit | SIMD decode |
| QUAL | — | Illumina 8-level → 3-bit | zstd, or lossless raw |
| QNAME | — | dict encode | — |
| TAGS | — | per-tag zstd | — |

---

## 為何不直接用 CRAM

CRAM 是 reference-based row-oriented 格式，壓縮率優秀，但：
- 仍為 record-by-record 解碼，不能 column-selective I/O
- 不是為 SIMD 並行解壓設計
- cloud object storage random access 需依賴 CRAM index，chunk 對齊非 HTTP-range 友善

AXF 的目標不是比 CRAM 更小（lossless 時難贏），而是：

> **查詢特定欄位時，讀取的位元組數更少、解壓更快、更適合 SIMD/GPU。**

---

## 後果

### 正面
- Coverage / QC 工作負載只讀 POS + CIGAR + MAPQ，跳過 SEQ / QUAL / TAGS。
- 每個 column 可獨立 seek，適合 HTTP Range Request（雲端 object storage）。
- Columnar layout 對 SIMD 批量解壓天然友善。

### 負面（需主動管理）
- 轉換成本：BAM → AXF 需要一次全部掃描（v0.3 引入）。
- 格式相容性：目前不被 samtools / IGV 原生支援；需 converter。
- QUAL lossless 儲存時 AXF 可能大於 CRAM；需 benchmark 驗證。
