# Developer Guide

## Prerequisites

| Tool | Version | Notes |
|---|---|---|
| CMake | 3.25+ | Required for `CMakePresets.json` support |
| vcpkg | latest | `VCPKG_ROOT` env var must be set |
| MSVC (Windows) | VS 2026 / v143 | C++23 support required |
| GCC (Linux) | 13+ | Or Clang 17+ |
| Ninja (Linux) | any | Preferred generator on Linux |

## First-time setup (Windows)

```powershell
# Ensure VCPKG_ROOT is set
$env:VCPKG_ROOT = "C:/vcpkg"

# Configure + build
cmake --preset windows-debug
cmake --build --preset windows-debug

# Run tests
ctest --preset windows-debug --output-on-failure
```

vcpkg installs dependencies from `vcpkg.json` automatically on first configure.
The Windows manifest intentionally does not install htslib when the default
`x64-windows` triplet reports it as unsupported; scaffold builds continue with
BAM/CRAM I/O disabled.

## First-time setup (Linux)

```bash
export VCPKG_ROOT="$HOME/vcpkg"

cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

## Scaffold-only mode

Phase 0 includes only a minimal `alignx_lib` scaffold source so GoogleTest can
create a smoke-test target. No functional `alignx` executable is created until
`src/main.cpp` and Phase 1 sources exist.

## Adding a new source module

1. Create `src/<module>/foo.cpp` (and optionally `src/<module>/foo.hpp`).
2. CMake picks it up automatically via `GLOB_RECURSE CONFIGURE_DEPENDS`.
3. Add unit tests under `tests/unit/test_foo.cpp`.
4. Run `cmake --build --preset windows-debug && ctest --preset windows-debug`.

## Code style

- C++23 standard. No `using namespace std;` in headers.
- Prefer `std::span`, `std::print`, structured bindings over C equivalents.
- Format with `clang-format` (config at repo root) before committing.
- Comments only when the WHY is non-obvious.

## htslib target verification

After first configure, check CMake output for:
```
htslib found via CMake config
```
or
```
htslib found via pkg-config
```

If neither appears, htslib is missing. Install via vcpkg:
```bash
vcpkg install htslib
```
or on Ubuntu:
```bash
apt install libhts-dev
```

On Windows, the default vcpkg `x64-windows` triplet may report htslib as
unsupported. In that case, use the scaffold build without htslib until a
supported Windows HTSlib strategy is selected.
