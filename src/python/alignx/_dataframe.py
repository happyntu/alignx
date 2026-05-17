"""DataFrame and Series conversion utilities for alignx types."""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import pandas as pd

import _alignx


def _require_pandas():
    try:
        import pandas as pd
        return pd
    except ImportError:
        raise ImportError(
            "pandas is required for DataFrame operations; install with: pip install pandas"
        ) from None


def _require_numpy():
    try:
        import numpy as np
        return np
    except ImportError:
        raise ImportError(
            "numpy is required for array operations; install with: pip install numpy"
        ) from None


_NUMERIC_COLUMNS = {"pos", "flag", "mapq", "mate_pos", "template_length"}
_STRING_COLUMNS = {"qname", "cigar", "mate_reference", "sequence", "quality", "tags"}
_ALL_COLUMNS = [
    "qname", "flag", "pos", "mapq", "cigar", "mate_reference",
    "mate_pos", "template_length", "sequence", "quality", "tags",
]

_COLUMN_MAP = {
    "qname": _alignx.ColumnId.qname,
    "flag": _alignx.ColumnId.flag,
    "pos": _alignx.ColumnId.pos,
    "mapq": _alignx.ColumnId.mapq,
    "cigar": _alignx.ColumnId.cigar,
    "mate_reference": _alignx.ColumnId.mate_reference,
    "mate_pos": _alignx.ColumnId.mate_pos,
    "template_length": _alignx.ColumnId.template_length,
    "sequence": _alignx.ColumnId.sequence,
    "quality": _alignx.ColumnId.quality,
    "tags": _alignx.ColumnId.tags,
}


def chunk_to_dataframe(
    chunk: _alignx.Chunk,
    columns: list[str] | None = None,
) -> "pd.DataFrame":
    """Convert an Axf1Chunk to a pandas DataFrame.

    Parameters
    ----------
    chunk : Chunk
        A chunk read from an AXF1 file.
    columns : list[str], optional
        Column names to include. Default: all 11 SAM fields.
    """
    pd = _require_pandas()

    if columns is None:
        columns = list(_ALL_COLUMNS)

    records = chunk.records
    data: dict = {}

    for col in columns:
        if col in _NUMERIC_COLUMNS:
            data[col] = chunk.column_array(col)
        elif col in _STRING_COLUMNS:
            data[col] = [getattr(r, col) for r in records]
        else:
            raise ValueError(f"unknown column: {col!r}")

    return pd.DataFrame(data, columns=columns)


def coverage_to_series(cov: _alignx.CoverageResult) -> "pd.Series":
    """Convert CoverageResult to a pandas Series with genomic position index.

    Parameters
    ----------
    cov : CoverageResult
        Coverage result from alignx.coverage().

    Returns
    -------
    pd.Series
        Index is 0-based genomic positions, values are uint32 depths.
    """
    pd = _require_pandas()
    np = _require_numpy()

    index = pd.RangeIndex(start=cov.start, stop=cov.end, name="position")
    return pd.Series(cov.depth, index=index, name=cov.reference, dtype=np.uint32)


def query_region(
    path: str,
    region: str,
    columns: list[str] | None = None,
    flag_exclude: int = 0,
    min_mapq: int = 0,
) -> "pd.DataFrame":
    """Query a region from an AXF1 file and return records as a DataFrame.

    Parameters
    ----------
    path : str
        Path to the AXF1 file.
    region : str
        Region string, e.g. "chr1:1000000-2000000".
    columns : list[str], optional
        Record fields to include in the DataFrame.
    flag_exclude : int
        SAM FLAG bits to exclude (default: 0).
    min_mapq : int
        Minimum MAPQ to include (default: 0).
    """
    pd = _require_pandas()

    reader = _alignx.open(path)
    idx = reader.index

    ref_name, (start, end) = _parse_region(region)
    ref_id = _find_ref_id(idx.references, ref_name)

    chunk_entries = reader.query_chunks(ref_id=ref_id, start=start, end=end)

    column_ids = _map_columns_to_ids(columns) if columns else None

    frames: list = []
    for entry in chunk_entries:
        if column_ids is not None:
            chunk = reader.read_chunk_columns(entry, column_ids)
        else:
            chunk = reader.read_chunk(entry)

        df = chunk_to_dataframe(chunk, columns=columns)

        if flag_exclude and "flag" in df.columns:
            df = df[(df["flag"] & flag_exclude) == 0]
        if min_mapq and "mapq" in df.columns:
            df = df[df["mapq"] >= min_mapq]

        frames.append(df)

    if not frames:
        cols = columns or list(_ALL_COLUMNS)
        return pd.DataFrame(columns=cols)

    return pd.concat(frames, ignore_index=True)


def bam_to_dataframe(
    path: str,
    region: str,
    flag_exclude: int = 0,
    min_mapq: int = 0,
) -> "pd.DataFrame":
    """Read BAM records from a region into a DataFrame.

    Parameters
    ----------
    path : str
        Path to BAM file (must be indexed).
    region : str
        Region string.
    flag_exclude : int
        SAM FLAG bits to exclude.
    min_mapq : int
        Minimum MAPQ.
    """
    pd = _require_pandas()

    if not _alignx.has_htslib:
        raise RuntimeError("alignx built without htslib; BamReader unavailable")

    reader = _alignx.BamReader.open(path)
    reader.fetch(region)

    rows: list[dict] = []
    for rec in reader:
        if flag_exclude and (rec.flag & flag_exclude):
            continue
        if min_mapq and rec.mapq < min_mapq:
            continue
        rows.append({
            "qname": rec.qname,
            "reference_name": rec.reference_name,
            "position": rec.position,
            "end_position": rec.end_position,
            "template_length": rec.template_length,
            "flag": rec.flag,
            "mapq": rec.mapq,
        })

    return pd.DataFrame(rows)


def _parse_region(region: str) -> tuple[str, tuple[int, int]]:
    """Parse 'chr1:1000-2000' → ('chr1', (999, 2000)) (1-based inclusive → 0-based half-open)."""
    if ":" not in region:
        raise ValueError(f"region must contain ':' separator: {region!r}")
    ref, coords = region.rsplit(":", 1)
    if "-" not in coords:
        raise ValueError(f"region must contain start-end range: {region!r}")
    start_str, end_str = coords.split("-", 1)
    return ref, (int(start_str) - 1, int(end_str))


def _find_ref_id(references: list, ref_name: str) -> int:
    for i, ref in enumerate(references):
        if ref.name == ref_name:
            return i
    raise ValueError(f"reference '{ref_name}' not found in file index")


def _map_columns_to_ids(columns: list[str]) -> list:
    ids = []
    for col in columns:
        if col not in _COLUMN_MAP:
            raise ValueError(f"unknown column: {col!r}")
        ids.append(_COLUMN_MAP[col])
    return ids
