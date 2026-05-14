#!/usr/bin/env python3
"""Summarize AXF1 column payload sizes without decoding payloads."""

from __future__ import annotations

import argparse
import sys
from dataclasses import dataclass, field
from pathlib import Path

from inspect_axf1_metadata import (
    Axf1Metadata,
    codec_name,
    column_name,
    read_column_entries,
    read_metadata,
)


@dataclass
class ColumnSummary:
    column_id: int
    total_payload_bytes: int = 0
    chunk_count: int = 0
    codecs: dict[int, int] = field(default_factory=dict)


def summarize_columns(metadata: Axf1Metadata) -> list[ColumnSummary]:
    summaries: dict[int, ColumnSummary] = {}
    for entry in read_column_entries(metadata):
        summary = summaries.setdefault(entry.column_id, ColumnSummary(column_id=entry.column_id))
        summary.total_payload_bytes += entry.payload_length
        summary.chunk_count += 1
        summary.codecs[entry.codec_id] = summary.codecs.get(entry.codec_id, 0) + 1
    return sorted(summaries.values(), key=lambda item: item.column_id)


def format_codec_distribution(codecs: dict[int, int]) -> str:
    return ",".join(
        f"{codec_id}:{codec_name(codec_id)}:{count}" for codec_id, count in sorted(codecs.items())
    )


def emit_summary(metadata: Axf1Metadata) -> None:
    summaries = summarize_columns(metadata)
    total_payload_bytes = sum(summary.total_payload_bytes for summary in summaries)
    print(
        "column_id\tcolumn_name\ttotal_payload_bytes\tchunk_count\t"
        "codec_distribution\tavg_payload_bytes_per_chunk\tpercent_payload"
    )
    for summary in summaries:
        average = (
            summary.total_payload_bytes / summary.chunk_count if summary.chunk_count > 0 else 0.0
        )
        percent = (
            summary.total_payload_bytes * 100.0 / total_payload_bytes
            if total_payload_bytes > 0
            else 0.0
        )
        print(
            f"{summary.column_id}\t{column_name(summary.column_id)}\t"
            f"{summary.total_payload_bytes}\t{summary.chunk_count}\t"
            f"{format_codec_distribution(summary.codecs)}\t{average:.3f}\t{percent:.3f}"
        )


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Summarize AXF1 column payload sizes without decoding payloads."
    )
    parser.add_argument("path", type=Path, help="AXF1 file to summarize")
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        metadata = read_metadata(args.path)
        emit_summary(metadata)
    except OSError as error:
        print(f"failed to read AXF1 file: {error}", file=sys.stderr)
        return 1
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
