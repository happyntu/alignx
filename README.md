# alignx

A high-performance, C++23-based alignment storage and query engine that remains compatible
with SAM/BAM/CRAM, while introducing a GPU/SIMD-friendly columnar format (`.axf`) and a
next-generation index for faster genomic region queries and downstream analysis.

## Positioning

alignx is **not** a full replacement for SAM/BAM on day one. It is:

1. **A faster query and filter layer** on top of existing BAM/CRAM files.
2. **A new accelerated cache format** (`.axf`) for pipelines that repeatedly query the same regions.
3. **A converter** between BAM/CRAM and `.axf`, and back.

```
Input:  SAM / BAM / CRAM
Output: BAM / CRAM / .axf
Core:   faster region query, lower I/O, GPU/SIMD-ready, cloud object storage friendly
```

## CLI overview

```bash
alignx convert  sample.bam  -o sample.axf        # BAM → AXF
alignx view     sample.axf  chr1:100000-200000    # region query
alignx export   sample.axf  -o sample.bam         # AXF → BAM
alignx pileup   sample.axf  chr7:55019017-55211628
alignx stats    sample.axf
alignx index    sample.axf                        # build .axf.idx
```

## Roadmap

| Version | Focus |
|---|---|
| v0.1 | BAM reader (HTSlib) + faster region query + new index |
| v0.2 | coverage / view / filter acceleration |
| v0.3 | columnar `.axf` cache format |
| v0.4 | CRAM/BAM converter, round-trip fidelity tests |
| v1.0 | full new format + benchmark paper |

## Build

**Windows (MSVC + vcpkg):**
```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
```

**Linux (GCC 13+ / Clang 17+):**
```bash
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release --output-on-failure
```

> `VCPKG_ROOT` must be set in your environment for vcpkg manifest mode.

## Dependencies

| Library | Role |
|---|---|
| HTSlib | BAM / SAM / CRAM / BGZF I/O |
| zlib | BGZF block decompression |
| CLI11 | Subcommand / flag parsing |
| GoogleTest | Unit and integration tests |

## Docs

- `docs/architecture.md` — module map and data flow
- `docs/roadmap.md` — phase breakdown and milestones
- `docs/adr/` — accepted architectural decisions
- `docs/research/` — competitive analysis and algorithm notes
