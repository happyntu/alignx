# ADR-001: 核心語言與 Build System 選型 — C++23 + CMake + vcpkg

| 欄位 | 內容 |
|---|---|
| 狀態 | **已決議 (Accepted)** |
| 日期 | 2026-05-11 |
| 決策者 | cklin |
| 影響範圍 | 所有 `src/` 模組、build system、未來 SIMD/CUDA 整合 |

---

## 背景

alignx 的核心任務包含：

- HTSlib BAM/CRAM/SAM 讀寫（C library 整合）
- 高效能 region query（pointer-heavy index traversal）
- BGZF block 解壓（zlib）
- 未來 AVX2/AVX-512 SIMD 路徑（Phase 2+）
- 未來 CUDA GPU 解壓（Phase 3+）
- Windows（MSVC）與 Linux（GCC/Clang）雙平台

---

## 決議

**C++23 + CMake 3.25+ + vcpkg manifest mode。**

---

## 比較矩陣

| 面向 | C++23 | Rust | alignx 判斷 |
|---|---|---|---|
| HTSlib C 整合 | ✅ 直接 `#include <htslib/...>` | ⚠️ 需 bindgen / unsafe FFI | C++23 |
| BGZF / zlib | ✅ 原生 C API | ⚠️ FFI 層 | C++23 |
| SIMD intrinsics | ✅ `<immintrin.h>` 直接 | ⚠️ `std::arch` 可用但繁瑣 | C++23 |
| CUDA 整合 | ✅ nvcc 原生 C++ host | ⚠️ cxx / unsafe FFI | C++23 |
| Memory safety | ⚠️ 靠 ASan + discipline | ✅ compile-time 保證 | Rust 較優 |
| 開發速度 (MVP) | ✅ 現有 HTSlib 生態直接用 | ⚠️ binding 建立成本 | C++23 |

---

## Build system 理由

| 選項 | 排除原因 / 採用原因 |
|---|---|
| **CMake 3.25+** | ✅ CMakePresets.json 跨平台、vcpkg 整合成熟、CUDA enable_language 支援 |
| Meson | ⚠️ vcpkg 整合不成熟、Windows MSVC 支援較弱 |
| Bazel | ⚠️ 對 C/HTSlib 外部依賴整合複雜、學習成本高 |

## 依賴管理

**vcpkg manifest mode**（`vcpkg.json` 在 repo root）。

優點：
- 依賴版本鎖定在 repo 內
- `cmake --preset` 自動觸發 vcpkg install
- Windows MSVC 與 Linux GCC 均支援

---

## 後果

### 正面
- HTSlib C API 無 FFI 邊界，零整合摩擦。
- SIMD / CUDA 整合路徑最短。
- 與 samtools/HTSlib/minimap2 生態技術棧一致。

### 負面（需主動管理）
- **Memory safety**：Debug build 啟用 AddressSanitizer。
- **Windows htslib**：vcpkg htslib port 在 Windows 的 CMake target 需在首次 configure 後確認（見 `docs/dev/README.md`）。
- **風格一致性**：`.clang-format` 強制執行。
