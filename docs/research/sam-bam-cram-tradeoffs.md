# SAM / BAM / BAI / CRAM 技術分析與 alignx 定位

## 格式資訊理論評估

```text
SAM  = 文字格式，可讀性高，空間極度浪費（無壓縮）
BAM  = SAM 的二進位 BGZF 壓縮版，row-oriented，通用成熟
BAI  = BAM 的 binning index，座標上限 ~512 Mbp（2^29-1）
CSI  = BAI 的後繼者，座標無限制，但仍為 binning scheme
CRAM = Reference-based row-oriented 格式，比 BAM 更省空間
```

BAI 的 512 Mbp 限制見 samtools 官方文件：
> "Regions must be specified as a single interval less than 2^29 bases"
> — https://www.htslib.org/doc/samtools-index.html

## 各格式可優化空間

| 優化方向 | BAM | CRAM | alignx 有機會 |
|---|---|---|---|
| 壓縮率（lossless） | 基準 | 比 BAM 好 30-50% | 中等（難贏 CRAM 很多）|
| 欄位選擇性讀取 | ✗ | ✗ | ✅（columnar）|
| Region query 速度 | BAI 16kbp 粒度 | 需 CRAI | ✅（細粒度 index）|
| SIMD 並行解壓 | 不針對設計 | 不針對設計 | ✅（設計目標）|
| Cloud HTTP range | 不友善 | 不友善 | ✅（設計目標）|
| GPU 解壓 | ✗ | ✗ | Phase 3+（設計目標）|

## 競爭格式分析

| 格式 | 定位 | alignx 相對 |
|---|---|---|
| BAM | 通用二進位標準 | alignx 相容讀取，提供加速層 |
| CRAM | 高壓縮替代 BAM | alignx lossless 壓縮率可能輸，但 query 速度目標更快 |
| htslib BGZF | 底層壓縮塊 | alignx 使用 BGZF 讀現有 BAM，自身格式不用 BGZF |
| Zarr / HDF5 | 通用 columnar | 不針對 alignment 語意；alignx 有 CIGAR/POS/SEQ 特化 codec |
| Arrow/Parquet | 通用欄位格式 | 無 genomic region index；alignx 有專屬索引 |
| pbbam / SMRT | PacBio 專屬 | 不同應用場景 |

## 生態障礙（務實評估）

alignx 不是第一個嘗試「取代 BAM」的專案：
- CRAM 花了約 10 年才被普遍接受（2013 spec → 2020+ 廣泛部署）
- 每個新格式都需要 samtools / GATK / IGV / DeepVariant 支援

**務實策略**：
1. 提供 BAM/CRAM 相容讀取層（HTSlib）
2. 新格式定位為「加速 cache」，不要求使用者放棄 BAM
3. 等 benchmark 數據充足後，再推動生態整合

## 最有機會的 benchmark 目標

不要比「誰的檔案更小」，而是比：

| Benchmark | alignx 目標 |
|---|---|
| `chr1:1M-2M` region query | 比 samtools view 快 2x+ |
| coverage 計算（POS-only 路徑）| 比 samtools depth 快（無需讀 SEQ/QUAL）|
| FLAG/MAPQ filter query | 比 samtools view -F 快（index 層剪枝）|
| S3/GCS range request | 比 BAI 方案更少 round-trip |

## 參考連結

- SAM/BAM/BAI spec: https://samtools.github.io/hts-specs/
- CRAM workflow: https://www.htslib.org/workflow/cram.html
- samtools index docs: https://www.htslib.org/doc/samtools-index.html
- samtools overview: https://www.htslib.org/doc/samtools.html
