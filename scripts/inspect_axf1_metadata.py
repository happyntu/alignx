#!/usr/bin/env python3
"""Inspect AXF1 header and chunk index metadata without decoding chunk payloads."""

from __future__ import annotations

import argparse
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


MAGIC = b"AXF1"
HEADER = struct.Struct("<4sIIQQ")
REFERENCE_PREFIX = struct.Struct("<H")
REFERENCE_LENGTH = struct.Struct("<I")
INDEX_ENTRY = struct.Struct("<IiiIQQ")
CHUNK_HEADER = struct.Struct("<IiiIH")
COLUMN_ENTRY = struct.Struct("<HHQQ")

COLUMN_NAMES = {
    1: "qname",
    2: "flag",
    3: "pos",
    4: "mapq",
    5: "cigar",
    6: "mate_reference",
    7: "mate_pos",
    8: "template_length",
    9: "sequence",
    10: "quality",
    11: "tags",
}

CODEC_NAMES = {
    0: "raw",
    1: "pos_delta_varint",
    2: "flag_bitpack",
    3: "mapq_rle",
    4: "seq_2bit_literal",
    5: "cigar_token",
}


@dataclass(frozen=True)
class Reference:
    name: str
    length: int


@dataclass(frozen=True)
class Chunk:
    ref_id: int
    start_pos: int
    end_pos: int
    record_count: int
    chunk_offset: int
    chunk_length: int

    @property
    def span(self) -> int:
        return self.end_pos - self.start_pos


@dataclass(frozen=True)
class Axf1Metadata:
    path: Path
    file_size: int
    version: int
    index_offset: int
    source_path: str
    conversion_region: str
    is_subset: bool
    references: list[Reference]
    chunks: list[Chunk]


@dataclass(frozen=True)
class ColumnEntry:
    chunk_index: int
    column_id: int
    codec_id: int
    payload_offset: int
    payload_length: int


def column_name(column_id: int) -> str:
    return COLUMN_NAMES.get(column_id, f"unknown_{column_id}")


def codec_name(codec_id: int) -> str:
    return CODEC_NAMES.get(codec_id, f"unknown_{codec_id}")


def require_range(data: bytes, offset: int, size: int, label: str) -> None:
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError(f"truncated AXF1 file while reading {label}")


def read_u8(data: bytes, offset: int, label: str) -> tuple[int, int]:
    require_range(data, offset, 1, label)
    return data[offset], offset + 1


def read_string(data: bytes, offset: int, label: str) -> tuple[str, int]:
    require_range(data, offset, 4, f"{label} length")
    (size,) = struct.unpack_from("<I", data, offset)
    offset += 4
    require_range(data, offset, size, label)
    value = data[offset : offset + size].decode("utf-8")
    return value, offset + size


def read_metadata(path: Path) -> Axf1Metadata:
    data = path.read_bytes()
    require_range(data, 0, HEADER.size, "header")
    magic, version, ref_count, chunk_count, index_offset = HEADER.unpack_from(data, 0)
    if magic != MAGIC:
        raise ValueError("invalid AXF1 magic")
    if index_offset > len(data):
        raise ValueError("AXF1 index offset points outside file")

    offset = HEADER.size
    references: list[Reference] = []
    for ref_index in range(ref_count):
        require_range(data, offset, REFERENCE_PREFIX.size, f"reference {ref_index} name length")
        (name_size,) = REFERENCE_PREFIX.unpack_from(data, offset)
        offset += REFERENCE_PREFIX.size
        require_range(data, offset, name_size, f"reference {ref_index} name")
        name = data[offset : offset + name_size].decode("utf-8")
        offset += name_size
        require_range(data, offset, REFERENCE_LENGTH.size, f"reference {ref_index} length")
        (length,) = REFERENCE_LENGTH.unpack_from(data, offset)
        offset += REFERENCE_LENGTH.size
        references.append(Reference(name=name, length=length))

    if offset > index_offset:
        raise ValueError("AXF1 references overlap index")

    source_path = ""
    conversion_region = ""
    is_subset = False
    if version == 2:
        subset_flag, offset = read_u8(data, offset, "subset flag")
        if subset_flag > 1:
            raise ValueError("invalid AXF1 subset metadata flag")
        is_subset = subset_flag == 1
        source_path, offset = read_string(data, offset, "source path")
        conversion_region, offset = read_string(data, offset, "conversion region")
    elif version != 1:
        raise ValueError("unsupported AXF1 version")
    if offset > index_offset:
        raise ValueError("AXF1 metadata overlaps index")

    chunks: list[Chunk] = []
    offset = index_offset
    for chunk_index in range(chunk_count):
        require_range(data, offset, INDEX_ENTRY.size, f"chunk index entry {chunk_index}")
        ref_id, start_pos, end_pos, record_count, chunk_offset, chunk_length = INDEX_ENTRY.unpack_from(
            data, offset
        )
        offset += INDEX_ENTRY.size
        if ref_id >= len(references):
            raise ValueError(f"AXF1 chunk {chunk_index} reference id out of range")
        if start_pos >= end_pos:
            raise ValueError(f"AXF1 chunk {chunk_index} requires start_pos < end_pos")
        if chunk_offset + chunk_length > index_offset:
            raise ValueError(f"AXF1 chunk {chunk_index} payload overlaps index or file end")
        chunks.append(
            Chunk(
                ref_id=ref_id,
                start_pos=start_pos,
                end_pos=end_pos,
                record_count=record_count,
                chunk_offset=chunk_offset,
                chunk_length=chunk_length,
            )
        )

    if offset != len(data):
        raise ValueError("unexpected trailing bytes in AXF1 file")

    return Axf1Metadata(
        path=path,
        file_size=len(data),
        version=version,
        index_offset=index_offset,
        source_path=source_path,
        conversion_region=conversion_region,
        is_subset=is_subset,
        references=references,
        chunks=chunks,
    )


def format_summary(metadata: Axf1Metadata) -> list[tuple[str, str]]:
    chunks = metadata.chunks
    total_records = sum(chunk.record_count for chunk in chunks)
    spans = [chunk.span for chunk in chunks]
    lengths = [chunk.chunk_length for chunk in chunks]
    records = [chunk.record_count for chunk in chunks]

    rows = [
        ("path", str(metadata.path)),
        ("file_size", str(metadata.file_size)),
        ("version", str(metadata.version)),
        ("is_subset", "true" if metadata.is_subset else "false"),
        ("source_path", metadata.source_path),
        ("conversion_region", metadata.conversion_region),
        ("reference_count", str(len(metadata.references))),
        ("chunk_count", str(len(chunks))),
        ("total_records", str(total_records)),
        ("index_offset", str(metadata.index_offset)),
    ]
    if chunks:
        rows.extend(
            [
                ("min_records_per_chunk", str(min(records))),
                ("max_records_per_chunk", str(max(records))),
                ("avg_records_per_chunk", f"{total_records / len(chunks):.3f}"),
                ("min_span", str(min(spans))),
                ("max_span", str(max(spans))),
                ("avg_span", f"{sum(spans) / len(spans):.3f}"),
                ("min_chunk_length", str(min(lengths))),
                ("max_chunk_length", str(max(lengths))),
                ("avg_chunk_length", f"{sum(lengths) / len(lengths):.3f}"),
            ]
        )
    return rows


def emit_summary(metadata: Axf1Metadata) -> None:
    for key, value in format_summary(metadata):
        print(f"{key}\t{value}")


def emit_chunks(metadata: Axf1Metadata) -> None:
    print("chunk_index\tref_id\treference\tstart_pos\tend_pos\tspan\trecord_count\tchunk_offset\tchunk_length")
    for index, chunk in enumerate(metadata.chunks):
        reference = metadata.references[chunk.ref_id].name
        print(
            f"{index}\t{chunk.ref_id}\t{reference}\t{chunk.start_pos}\t{chunk.end_pos}\t"
            f"{chunk.span}\t{chunk.record_count}\t{chunk.chunk_offset}\t{chunk.chunk_length}"
        )


def read_column_entries(metadata: Axf1Metadata) -> list[ColumnEntry]:
    data = metadata.path.read_bytes()
    columns: list[ColumnEntry] = []
    for chunk_index, chunk in enumerate(metadata.chunks):
        require_range(data, chunk.chunk_offset, CHUNK_HEADER.size, f"chunk {chunk_index} header")
        ref_id, start_pos, end_pos, record_count, column_count = CHUNK_HEADER.unpack_from(
            data, chunk.chunk_offset
        )
        if (
            ref_id != chunk.ref_id
            or start_pos != chunk.start_pos
            or end_pos != chunk.end_pos
            or record_count != chunk.record_count
        ):
            raise ValueError(f"AXF1 chunk {chunk_index} metadata does not match index")

        entries_offset = chunk.chunk_offset + CHUNK_HEADER.size
        payload_base = entries_offset + column_count * COLUMN_ENTRY.size
        chunk_end = chunk.chunk_offset + chunk.chunk_length
        if payload_base > chunk_end:
            raise ValueError(f"AXF1 chunk {chunk_index} column entries exceed chunk length")

        for column_index in range(column_count):
            entry_offset = entries_offset + column_index * COLUMN_ENTRY.size
            require_range(
                data,
                entry_offset,
                COLUMN_ENTRY.size,
                f"chunk {chunk_index} column entry {column_index}",
            )
            column_id, codec_id, payload_offset, payload_length = COLUMN_ENTRY.unpack_from(
                data, entry_offset
            )
            payload_file_offset = payload_base + payload_offset
            if payload_file_offset > chunk_end or payload_length > chunk_end - payload_file_offset:
                raise ValueError(
                    f"AXF1 chunk {chunk_index} column {column_index} payload exceeds chunk length"
                )
            columns.append(
                ColumnEntry(
                    chunk_index=chunk_index,
                    column_id=column_id,
                    codec_id=codec_id,
                    payload_offset=payload_offset,
                    payload_length=payload_length,
                )
            )
    return columns


def emit_columns(metadata: Axf1Metadata) -> None:
    print(
        "chunk_index\tcolumn_id\tcolumn_name\tcodec_id\tcodec_name\t"
        "payload_offset\tpayload_length"
    )
    for entry in read_column_entries(metadata):
        print(
            f"{entry.chunk_index}\t{entry.column_id}\t{column_name(entry.column_id)}\t"
            f"{entry.codec_id}\t{codec_name(entry.codec_id)}\t"
            f"{entry.payload_offset}\t{entry.payload_length}"
        )


def emit_column_codecs(metadata: Axf1Metadata) -> None:
    counts: dict[tuple[int, int], int] = {}
    for entry in read_column_entries(metadata):
        key = (entry.column_id, entry.codec_id)
        counts[key] = counts.get(key, 0) + 1

    print("column_id\tcolumn_name\tcodec_id\tcodec_name\tchunk_count")
    for (column_id, codec_id), count in sorted(counts.items()):
        print(
            f"{column_id}\t{column_name(column_id)}\t{codec_id}\t"
            f"{codec_name(codec_id)}\t{count}"
        )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Inspect AXF1 header and chunk index metadata without decoding chunk payloads."
    )
    parser.add_argument("path", type=Path, help="AXF1 file to inspect")
    parser.add_argument("--chunks", action="store_true", help="print one TSV row per chunk")
    parser.add_argument("--columns", action="store_true", help="print one TSV row per chunk column")
    parser.add_argument(
        "--column-codecs",
        action="store_true",
        help="print per-column codec distribution across chunks",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        metadata = read_metadata(args.path)
    except OSError as error:
        print(f"failed to read AXF1 file: {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1

    if args.chunks:
        emit_chunks(metadata)
    elif args.columns:
        emit_columns(metadata)
    elif args.column_codecs:
        emit_column_codecs(metadata)
    else:
        emit_summary(metadata)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
