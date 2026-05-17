"""Tests for numpy/pandas integration in alignx Python bindings."""

from __future__ import annotations

from pathlib import Path

import pytest

import _alignx

np = pytest.importorskip("numpy")
pd = pytest.importorskip("pandas")


class TestCoverageNumpy:

    def test_depth_is_numpy_array(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        assert isinstance(cov.depth, np.ndarray)

    def test_depth_dtype(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        assert cov.depth.dtype == np.uint32

    def test_depth_shape(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        assert cov.depth.shape == (60,)

    def test_depth_values_nonzero(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        assert cov.depth.sum() > 0

    def test_depth_gc_safe(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        arr = cov.depth
        expected_sum = int(arr.sum())
        del cov
        assert int(arr.sum()) == expected_sum

    def test_buffer_protocol(self, toy_axf1: Path):
        cov = _alignx.coverage(str(toy_axf1), "chrToy:101-160")
        arr = np.asarray(cov)
        assert arr.dtype == np.uint32
        assert arr.shape == (60,)


class TestColumnArray:

    def test_pos_array(self, toy_axf1: Path):
        reader = _alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        arr = chunk.column_array("pos")
        assert isinstance(arr, np.ndarray)
        assert arr.dtype == np.int32
        assert len(arr) == len(chunk.records)

    def test_flag_array(self, toy_axf1: Path):
        reader = _alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        arr = chunk.column_array("flag")
        assert arr.dtype == np.uint16

    def test_mapq_array(self, toy_axf1: Path):
        reader = _alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        arr = chunk.column_array("mapq")
        assert arr.dtype == np.uint8

    def test_invalid_column(self, toy_axf1: Path):
        reader = _alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        with pytest.raises(ValueError):
            chunk.column_array("qname")


class TestChunkDataFrame:

    def test_to_dataframe(self, toy_axf1: Path):
        import alignx
        reader = alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        df = chunk.to_dataframe()
        assert isinstance(df, pd.DataFrame)
        assert len(df) == len(chunk.records)
        assert "qname" in df.columns
        assert "pos" in df.columns

    def test_dtypes(self, toy_axf1: Path):
        import alignx
        reader = alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        df = chunk.to_dataframe()
        assert df["flag"].dtype == np.uint16
        assert df["pos"].dtype == np.int32
        assert df["mapq"].dtype == np.uint8

    def test_column_selection(self, toy_axf1: Path):
        import alignx
        reader = alignx.open(str(toy_axf1))
        chunks = reader.query_chunks(ref_id=0, start=100, end=200)
        chunk = reader.read_chunk(chunks[0])
        df = chunk.to_dataframe(columns=["pos", "flag", "qname"])
        assert list(df.columns) == ["pos", "flag", "qname"]


class TestCoverageSeries:

    def test_to_series(self, toy_axf1: Path):
        import alignx
        cov = alignx.coverage(str(toy_axf1), "chrToy:101-160")
        s = cov.to_series()
        assert isinstance(s, pd.Series)
        assert len(s) == 60
        assert s.index[0] == 100
        assert s.index[-1] == 159
        assert s.name == "chrToy"

    def test_series_dtype(self, toy_axf1: Path):
        import alignx
        cov = alignx.coverage(str(toy_axf1), "chrToy:101-160")
        s = cov.to_series()
        assert s.dtype == np.uint32


class TestQueryRegion:

    def test_query_region(self, toy_axf1: Path):
        from alignx._dataframe import query_region
        df = query_region(str(toy_axf1), "chrToy:101-160")
        assert isinstance(df, pd.DataFrame)
        assert len(df) > 0
        assert "qname" in df.columns

    def test_with_columns(self, toy_axf1: Path):
        from alignx._dataframe import query_region
        df = query_region(str(toy_axf1), "chrToy:101-160", columns=["pos", "flag"])
        assert list(df.columns) == ["pos", "flag"]

    def test_empty_region(self, toy_axf1: Path):
        from alignx._dataframe import query_region
        df = query_region(str(toy_axf1), "chrToy:900000-900010")
        assert isinstance(df, pd.DataFrame)
        assert len(df) == 0


@pytest.mark.skipif(not _alignx.has_htslib, reason="htslib required")
class TestBamDataFrame:

    def test_bam_to_dataframe(self, toy_bam: Path):
        from alignx._dataframe import bam_to_dataframe
        df = bam_to_dataframe(str(toy_bam), "chrToy:101-160")
        assert isinstance(df, pd.DataFrame)
        assert len(df) > 0
        assert "qname" in df.columns
        assert "reference_name" in df.columns
