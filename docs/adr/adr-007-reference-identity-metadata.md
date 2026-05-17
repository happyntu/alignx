# ADR-007: Reference Identity Metadata

**Status:** Accepted  
**Date:** 2026-05-17

## Context

AXF1 v2 metadata stores `source_path`, `conversion_region`, and `is_subset`.
These are audit hints only — they do not identify the reference genome used
during alignment.

Phase 2 Item 6 (reference-delta SEQ codec) requires the decoder to know exactly
which reference FASTA was used at encode time so it can reconstruct original
sequences. Without stable reference identity, the decoder cannot verify that the
provided FASTA matches what the encoder used, risking silent data corruption.

Current `Axf1FileMetadata`:

```cpp
struct Axf1FileMetadata {
    std::string source_path;
    std::string conversion_region;
    bool is_subset = false;
};
```

This struct has no typed extensibility mechanism. Adding fields requires a binary
format change.

## Decision

**Introduce AXF1 v3 file metadata with a typed key/value extension block after
the existing v2 fields. Reference identity is stored as optional extension
entries. The reference-delta SEQ codec is gated behind `--reference <fasta>` at
encode time.**

## Design

### File Version Strategy

Bump the file version from 2 to 3. The v3 writer appends a typed extension block
after the v2 metadata payload. V3 readers can open v2 files (no extensions). V2
readers reject v3 files with "unsupported AXF1 version" — this is acceptable
because v3 introduces a new decode dependency (external reference) that v2
readers cannot satisfy.

```
kVersionV1 = 1   // legacy, no metadata
kVersionV2 = 2   // source_path, conversion_region, is_subset
kVersionV3 = 3   // v2 fields + typed extension block
```

### Typed Extension Block Format

After the v2 metadata payload:

```
ExtensionBlock:
  entry_count    u32
  entries        Entry[entry_count]

Entry:
  key_id         u16       // well-known key enum
  flags          u8        // bit 0: required (reader MUST understand to decode)
  value_length   u32       // byte length of value payload
  value          byte[value_length]
```

**Key IDs** (initial set):

| ID | Name | Value format | Required | Purpose |
|---:|:-----|:-------------|:---------|:--------|
| 1 | `ref_assembly_name` | UTF-8 string | no | e.g., "GRCh38" |
| 2 | `ref_contig_table` | binary: `count u32`, per contig: `name_len u16 + name + length u32` | no | reference dictionary for validation |
| 3 | `ref_contig_sha256` | binary: `contig_index u32 + sha256 [32 bytes]` repeated | **yes** if ref-delta codec used | per-contig sequence checksums |
| 4 | `ref_fasta_uri` | UTF-8 string | no | path/URI hint for locating FASTA |
| 5 | `bam_header_sha256` | 32 bytes | no | SHA-256 of original BAM/CRAM `@HD`+`@SQ` header text |
| 6 | `encode_reference_path` | UTF-8 string | no | audit: FASTA path used at encode time |

**Reader rules:**

- Read all entries. For entries with `flags & 0x01` (required): if the key ID is
  not recognized, reject the file with a clear error message.
- For entries with `flags & 0x01 == 0` (optional): skip unrecognized key IDs
  silently.
- This allows future extensions without version bumps for optional metadata.

### Reference Identity Validation at Decode Time

When a chunk uses the reference-delta SEQ codec:

1. Reader loads the extension block and finds `ref_contig_sha256` (required).
2. Reader opens the user-provided FASTA (via `--reference <fasta>` CLI flag or
   environment variable `ALIGNX_REFERENCE`).
3. Before decoding the first ref-delta chunk for a contig, reader computes
   SHA-256 of that contig's sequence from the FASTA and compares against the
   stored checksum.
4. On mismatch: hard error — "reference contig chr1 SHA-256 mismatch: expected
   abc…, got def…; provide the correct reference FASTA".
5. On missing FASTA: hard error — "file requires reference FASTA for decode;
   use --reference <path>".

Checksum computation is per-contig, cached after first validation. Cost: one
sequential FASTA read per contig on first access.

### Encode-Time Behavior

When `--reference <fasta>` is provided at encode time:

1. Writer loads FASTA index (`.fai`), validates contig names match BAM header.
2. Writer computes per-contig SHA-256 for all contigs referenced by records in
   the input.
3. Writer stores `ref_assembly_name` (from `@SQ AS:` tag if available),
   `ref_contig_table`, `ref_contig_sha256`, and `encode_reference_path` in the
   extension block.
4. Writer is now permitted to use the reference-delta SEQ codec for chunks
   where it provides size benefit over `seq_2bit_literal`.
5. Without `--reference`: writer uses only self-contained codecs. No reference
   identity metadata is written. Output remains AXF1 v3 (with an empty or
   minimal extension block) or v2 if no extensions are needed.

### CLI Interface

```bash
# Encode with reference (enables ref-delta SEQ)
alignx convert input.bam -o output.axf1 --format AXF1 --reference ref.fa

# Decode (required if file was encoded with ref-delta)
alignx view output.axf1 chr1:1000000-2000000 --reference ref.fa

# Decode without reference (works if file uses only self-contained codecs)
alignx view output.axf1 chr1:1000000-2000000
```

Environment variable `ALIGNX_REFERENCE` provides a default when `--reference` is
not specified. CLI flag takes precedence.

### Fallback: No Stored Literal Backup

When a chunk uses reference-delta SEQ, the original literal SEQ is **not**
stored alongside. Rationale:

- Storing both defeats the purpose of reference-delta compression.
- The reference FASTA is a stable artifact (checksummed). If the user has the
  file, decode works. If not, encode without `--reference` for portable output.
- This matches CRAM's model: CRAM requires the reference for decode.

The encoder's decision tree per chunk:

```
if --reference provided:
    if ref-delta payload < seq_2bit_literal payload:
        use ref-delta codec (requires FASTA at decode)
    else:
        use seq_2bit_literal (self-contained)
else:
    use seq_2bit_literal or raw (self-contained)
```

Files that use ref-delta on any chunk will have `ref_contig_sha256` marked as
required. Files that use only self-contained codecs will not set any required
extension entries.

### Impact on Existing Code

| Component | Change |
|:----------|:-------|
| `Axf1FileMetadata` struct | Add `std::vector<MetadataEntry> extensions` field |
| `encode_file_metadata()` | Serialize extension block after v2 fields when version ≥ 3 |
| `read_file_metadata()` | Parse extension block when version = 3; skip for version ≤ 2 |
| File header version constant | Add `kVersionV3 = 3` |
| Writer version selection | Emit v3 when any extension entries are present; v2 otherwise |
| `Axf1WriteOptions` | Add `std::filesystem::path reference_fasta` |
| CLI `convert` subcommand | Add `--reference` flag |
| CLI `view`/`pileup`/`coverage` | Add `--reference` flag for decode |

### MetadataEntry Struct

```cpp
struct MetadataEntry {
    std::uint16_t key_id;
    std::uint8_t flags;  // bit 0: required
    std::vector<unsigned char> value;
};

struct Axf1FileMetadata {
    std::string source_path;
    std::string conversion_region;
    bool is_subset = false;
    std::vector<MetadataEntry> extensions;  // v3+
};
```

## Alternatives Considered

### A. Extend v2 without version bump

Append optional bytes after existing v2 metadata. Rejected: v2 readers would
silently ignore reference identity, then attempt to decode ref-delta codec
payloads they cannot handle. A version bump gives a clean rejection boundary.

### B. Store literal SEQ as fallback alongside ref-delta

Always store both ref-delta and literal in the same column, so decode works
without a reference. Rejected: doubles SEQ storage for the common case where the
reference is available. Users who need portable files should encode without
`--reference`.

### C. Use file-level hash of entire FASTA

Single SHA-256 of the complete FASTA file. Rejected: different FASTA files can
have the same sequences in different order with different line wrapping, producing
different file hashes. Per-contig sequence hashing is the correct granularity.

### D. Defer to v4 and keep v3 for simpler extensions only

Separate reference identity from the extension mechanism. Rejected: the typed
extension block is the simplest mechanism that also solves reference identity in
one version bump.

## Consequences

- V3 files with ref-delta chunks cannot be decoded by v2 readers or without the
  matching reference FASTA. This is an intentional trade-off for compression.
- V3 files without ref-delta (all self-contained codecs) can still be decoded
  without a reference; the extension block contains only optional audit metadata.
- Future extensions (e.g., alignment tool version, sample ID, flow cell metadata)
  can be added as new key IDs without another version bump, as long as they are
  optional.
- The FASTA reader component and CIGAR-driven reconstruction logic must be
  implemented before the ref-delta SEQ codec; this ADR covers only the metadata
  contract.

## Implementation Order

1. `MetadataEntry` struct + extension block serialization/deserialization
2. `kVersionV3` constant + reader version dispatch
3. `--reference` CLI flag (no-op initially; validates FASTA exists)
4. Per-contig SHA-256 computation and `ref_contig_sha256` writer
5. FASTA reader component (`src/io/fasta_reader.hpp`)
6. Reference validation at decode time (checksum comparison)
7. Reference-delta SEQ codec (separate design doc)
