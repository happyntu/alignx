# ADR-003: Index 設計 — 多層次 AXF Index vs BAI/CSI

| 欄位 | 內容 |
|---|---|
| 狀態 | **已決議 (Accepted)** |
| 日期 | 2026-05-11 |
| 決策者 | cklin |
| 影響範圍 | `src/index/` |

---

## 背景

BAI（BAM index）使用 binning scheme（16-level bin hierarchy），每個 bin 儲存對應
virtual file offset list。設計來自 Tabix，有以下已知限制：

- 單條 reference 座標上限約 `2^29 - 1`（約 512 Mbp），超出需改用 CSI。
- Linear index 解析度為 16 kbp，粒度固定。
- 不支援非位置查詢（QNAME、FLAG、tag）。
- 每次 random seek 都需要 BGZF block decompression，不適合欄位選擇性讀取。

---

## 決議

**alignx 使用兩層 index：**

1. **BaiReader / CsiReader**（v0.1）：讀取現有 BAI/CSI，讓 alignx 在不轉換格式的情況下直接查詢 BAM。
2. **AXFIndex**（v0.1 起逐步建立）：alignx 自身的多層次索引，儲存於 `.axf.idx`。

---

## AXFIndex 設計

```
AXFIndex (v1, 對應 v0.1 BAM-only 場景):
  magic:        "AXI1\0"
  ref_count:    u32
  Per reference:
    interval_tree OR sorted_list of:
      { start: u32, end: u32, chunk_offset: u64, column_index_offset: u64 }
  Footer:
    crc32 of entire index
```

**v0.1 實作**：每條 reference 維護 sorted interval list，查詢用 binary search 找 overlap。

**v0.3+**：改為 interval tree 或 succinct range tree，支援：
- 位置 region query（主要）
- MAPQ ≥ threshold filter（在 index 層剪枝）
- FLAG 位元 filter（在 index 層剪枝，若 chunk summary bit 支援）

**v1.0+（cloud-friendly）**：
- chunk offset 必須連續，允許單次 HTTP Range Request 取回整個 chunk
- 不依賴 BGZF virtual offset，直接使用 file-absolute byte offset

---

## BAI vs CSI vs AXFIndex 比較

| 面向 | BAI | CSI | AXFIndex |
|---|---|---|---|
| 座標上限 | ~512 Mbp | 無限制 | 無限制 |
| 查詢粒度 | 16 kbp linear | 可調 | chunk 大小（可調） |
| 非位置查詢 | 不支援 | 不支援 | v1.0+ 部分支援 |
| 欄位選擇性 | 不支援 | 不支援 | ✅（column_index_offset） |
| cloud HTTP range | 不友善 | 不友善 | ✅（設計目標） |
| 現有工具相容 | ✅ samtools 原生 | ✅ samtools 原生 | 需 alignx |

---

## 實作順序

| 版本 | Index 工作 |
|---|---|
| v0.1 | BaiReader（讀 .bai）+ AXFIndex v1（sorted list，BAM-only）|
| v0.2 | CsiReader（讀 .csi）|
| v0.3 | AXFIndex v2（與 .axf chunk 整合，column_index_offset）|
| v1.0 | AXFIndex v3（interval tree + cloud-friendly offset）|

---

## 後果

### 正面
- v0.1 即可讀現有 BAI，不需要使用者先轉換格式。
- AXFIndex 從 v0.1 就開始積累，到 v0.3 無縫切換為 AXF 整合版本。
- cloud-friendly offset 設計讓 v1.0 直接支援 S3 / GCS object storage。

### 負面（需主動管理）
- AXFIndex 需維護獨立的 `.axf.idx` 檔案，converter 必須同步產生。
- Interval tree 實作複雜度高於 BAI binning；v0.1 先用 sorted list 降低風險。
