"""Test suite for alignx Python bindings (_alignx module)."""

from __future__ import annotations

import subprocess
from pathlib import Path

import pytest

import _alignx as alignx


# ── Module attributes ─────────────────────────────────────────────────────────

def test_version():
    assert alignx.__version__ == "0.1.0"


def test_has_htslib():
    assert isinstance(alignx.has_htslib, bool)


def test_has_zstd():
    assert isinstance(alignx.has_zstd, bool)


# ── Enums ─────────────────────────────────────────────────────────────────────

def test_column_id_values():
    assert alignx.ColumnId.qname.value == 1
    assert alignx.ColumnId.pos.value == 3
    assert alignx.ColumnId.tags.value == 11


def test_codec_id_values():
    assert alignx.CodecId.raw.value == 0
    assert alignx.CodecId.pos_delta_varint.value == 1
    assert alignx.CodecId.seq_ref_delta.value == 13


def test_compression_values():
    assert alignx.Compression.none is not None
    assert alignx.Compression.zstd is not None


def test_quality_lossy_values():
    assert alignx.QualityLossy.none is not None
    assert alignx.QualityLossy.illumina8 is not None


# ── WriteOptions / RecordFilter ───────────────────────────────────────────────

def test_write_options_defaults():
    opts = alignx.WriteOptions()
    assert opts.quality_compression == alignx.Compression.none
    assert opts.column_compression == alignx.Compression.none
    assert opts.quality_lossy == alignx.QualityLossy.none
    assert opts.reference_fasta is None


def test_write_options_mutate():
    opts = alignx.WriteOptions()
    opts.quality_compression = alignx.Compression.zstd
    opts.reference_fasta = "/tmp/ref.fa"
    assert opts.quality_compression == alignx.Compression.zstd
    assert opts.reference_fasta == "/tmp/ref.fa"
    opts.reference_fasta = None
    assert opts.reference_fasta is None


def test_record_filter_defaults():
    f = alignx.RecordFilter()
    assert f.flag_exclude == 0
    assert f.min_mapq == 0
    assert not f.is_active()


def test_record_filter_active():
    f = alignx.RecordFilter()
    f.min_mapq = 20
    assert f.is_active()


# ── Axf1FileReader — open + inspect metadata ─────────────────────────────────

def test_open_toy_axf1(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    idx = reader.index
    assert len(idx.references) > 0
    assert len(idx.chunks) > 0
    assert idx.references[0].name == "chrToy"


def test_open_nonexistent():
    with pytest.raises(RuntimeError):
        alignx.open("/nonexistent/file.axf1")


def test_file_index_repr(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    r = repr(reader.index)
    assert "FileIndex" in r


def test_reader_repr(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    r = repr(reader)
    assert "Axf1FileReader" in r


# ── Query and record access ──────────────────────────────────────────────────

def test_query_region(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    chunks = reader.query_chunks(ref_id=0, start=100, end=200)
    assert len(chunks) > 0
    for c in chunks:
        assert isinstance(c, alignx.ChunkIndex)
        assert c.overlaps(100, 200)


def test_read_chunk_records(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    chunks = reader.query_chunks(ref_id=0, start=100, end=200)
    chunk = reader.read_chunk(chunks[0])
    assert isinstance(chunk, alignx.Chunk)
    assert len(chunk.records) > 0
    rec = chunk.records[0]
    assert isinstance(rec, alignx.Record)
    assert isinstance(rec.qname, str)
    assert isinstance(rec.pos, int)
    assert isinstance(rec.flag, int)


def test_read_chunk_columns(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    chunks = reader.query_chunks(ref_id=0, start=100, end=200)
    chunk = reader.read_chunk_columns(
        chunks[0], [alignx.ColumnId.pos, alignx.ColumnId.cigar]
    )
    assert len(chunk.records) > 0
    for rec in chunk.records:
        assert rec.pos >= 0
        assert rec.cigar != ""


def test_record_repr(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    chunks = reader.query_chunks(ref_id=0, start=100, end=200)
    chunk = reader.read_chunk(chunks[0])
    r = repr(chunk.records[0])
    assert "Record" in r


def test_chunk_index_repr(toy_axf1: Path):
    reader = alignx.open(str(toy_axf1))
    chunks = reader.query_chunks(ref_id=0, start=100, end=200)
    r = repr(chunks[0])
    assert "ChunkIndex" in r


# ── High-level view ──────────────────────────────────────────────────────────

def test_view_region(toy_axf1: Path):
    sam = alignx.view(str(toy_axf1), "chrToy:101-160")
    assert isinstance(sam, str)
    lines = sam.strip().split("\n")
    assert len(lines) > 0
    for line in lines:
        fields = line.split("\t")
        assert len(fields) >= 11


def test_view_with_filter(toy_axf1: Path):
    all_sam = alignx.view(str(toy_axf1), "chrToy:101-160")
    filtered_sam = alignx.view(str(toy_axf1), "chrToy:101-160", flag_exclude=0x10)
    all_count = len(all_sam.strip().split("\n"))
    filtered_count = len(filtered_sam.strip().split("\n")) if filtered_sam.strip() else 0
    assert filtered_count <= all_count


# ── Coverage ─────────────────────────────────────────────────────────────────

def test_coverage(toy_axf1: Path):
    cov = alignx.coverage(str(toy_axf1), "chrToy:101-160")
    assert isinstance(cov, alignx.CoverageResult)
    assert cov.reference == "chrToy"
    assert cov.start == 100
    assert cov.end == 160
    assert len(cov.depth) == 60
    assert cov.records_counted > 0


def test_coverage_repr(toy_axf1: Path):
    cov = alignx.coverage(str(toy_axf1), "chrToy:101-160")
    r = repr(cov)
    assert "CoverageResult" in r


def test_coverage_len(toy_axf1: Path):
    cov = alignx.coverage(str(toy_axf1), "chrToy:101-160")
    assert len(cov) == 60


# ── Convert + export roundtrip ───────────────────────────────────────────────

def test_convert_roundtrip(toy_bam: Path, tmp_path: Path):
    axf1_path = tmp_path / "roundtrip.axf1"
    alignx.convert(str(toy_bam), str(axf1_path))
    assert axf1_path.exists()
    reader = alignx.open(str(axf1_path))
    assert len(reader.index.chunks) > 0


@pytest.mark.skipif(not alignx.has_htslib, reason="htslib required")
def test_export_bam(toy_axf1: Path, tmp_path: Path):
    bam_out = tmp_path / "exported.bam"
    alignx.export_bam(str(toy_axf1), str(bam_out))
    assert bam_out.exists()
    assert bam_out.stat().st_size > 0


# ── BamReader ────────────────────────────────────────────────────────────────

@pytest.mark.skipif(not alignx.has_htslib, reason="htslib required")
class TestBamReader:
    def test_open(self, toy_bam: Path):
        reader = alignx.BamReader.open(str(toy_bam))
        assert reader.has_index()
        refs = reader.references()
        assert len(refs) > 0
        assert refs[0].name == "chrToy"

    def test_fetch_and_iterate(self, toy_bam: Path):
        reader = alignx.BamReader.open(str(toy_bam))
        reader.fetch("chrToy:101-160")
        records = list(reader)
        assert len(records) > 0
        for rec in records:
            assert isinstance(rec, alignx.BamRecord)
            assert rec.reference_name == "chrToy"

    def test_context_manager(self, toy_bam: Path):
        with alignx.BamReader.open(str(toy_bam)) as reader:
            reader.fetch("chrToy:101-160")
            rec = reader.next_record()
            assert rec is not None

    def test_repr(self, toy_bam: Path):
        reader = alignx.BamReader.open(str(toy_bam))
        r = repr(reader)
        assert "BamReader" in r

    def test_bam_record_repr(self, toy_bam: Path):
        reader = alignx.BamReader.open(str(toy_bam))
        reader.fetch("chrToy:101-160")
        rec = reader.next_record()
        r = repr(rec)
        assert "BamRecord" in r


# ── FastaReader ──────────────────────────────────────────────────────────────

@pytest.mark.skipif(not alignx.has_htslib, reason="htslib required")
class TestFastaReader:
    def test_open(self, toy_ref: Path):
        reader = alignx.FastaReader.open(str(toy_ref))
        contigs = reader.contigs()
        assert len(contigs) > 0
        assert contigs[0].name == "chrToy"

    def test_fetch_sequence(self, toy_ref: Path):
        reader = alignx.FastaReader.open(str(toy_ref))
        seq = reader.fetch_sequence("chrToy", 0, 10)
        assert isinstance(seq, str)
        assert len(seq) == 10

    def test_fetch_contig(self, toy_ref: Path):
        reader = alignx.FastaReader.open(str(toy_ref))
        seq = reader.fetch_contig("chrToy")
        assert isinstance(seq, str)
        assert len(seq) == 1000

    def test_compute_sha256(self, toy_ref: Path):
        reader = alignx.FastaReader.open(str(toy_ref))
        sha = reader.compute_contig_sha256("chrToy")
        assert isinstance(sha, bytes)
        assert len(sha) == 32

    def test_context_manager(self, toy_ref: Path):
        with alignx.FastaReader.open(str(toy_ref)) as reader:
            contigs = reader.contigs()
            assert len(contigs) > 0

    def test_repr(self, toy_ref: Path):
        reader = alignx.FastaReader.open(str(toy_ref))
        r = repr(reader)
        assert "FastaReader" in r
