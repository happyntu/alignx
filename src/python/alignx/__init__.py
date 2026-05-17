"""alignx — columnar alignment format for selective-column I/O."""

from __future__ import annotations

from _alignx import *  # noqa: F401,F403
from _alignx import __version__, has_htslib, has_zstd, has_avx2  # noqa: F401

from alignx._dataframe import (  # noqa: F401
    bam_to_dataframe,
    chunk_to_dataframe,
    coverage_to_series,
    query_region,
)

import _alignx

_alignx.Chunk.to_dataframe = lambda self, **kw: chunk_to_dataframe(self, **kw)
_alignx.CoverageResult.to_series = lambda self: coverage_to_series(self)

__all__ = [
    "__version__",
    "has_htslib",
    "has_zstd",
    "has_avx2",
    "ColumnId",
    "CodecId",
    "Compression",
    "QualityLossy",
    "Record",
    "Reference",
    "Chunk",
    "ChunkIndex",
    "FileMetadata",
    "FileIndex",
    "ChunkReadProfile",
    "CoverageResult",
    "WriteOptions",
    "RecordFilter",
    "Axf1FileReader",
    "open",
    "view",
    "coverage",
    "convert",
    "export_bam",
    "bam_to_dataframe",
    "chunk_to_dataframe",
    "coverage_to_series",
    "query_region",
]
