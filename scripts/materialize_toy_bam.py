#!/usr/bin/env python3
"""Materialize deterministic toy BAM/BAI fixtures without external tools."""

from __future__ import annotations

import argparse
import binascii
import pathlib
import struct
import zlib


BASE_TO_BAM = {
    "=": 0,
    "A": 1,
    "C": 2,
    "M": 3,
    "G": 4,
    "R": 5,
    "S": 6,
    "V": 7,
    "T": 8,
    "W": 9,
    "Y": 10,
    "H": 11,
    "K": 12,
    "D": 13,
    "B": 14,
    "N": 15,
}

CIGAR_OP = {
    "M": 0,
    "I": 1,
    "D": 2,
    "N": 3,
    "S": 4,
    "H": 5,
    "P": 6,
    "=": 7,
    "X": 8,
    "B": 9,
}

BGZF_EOF = bytes.fromhex(
    "1f8b08040000000000ff0600424302001b0003000000000000000000"
)


def bgzf_block(payload: bytes, level: int = 6) -> bytes:
    if len(payload) > 65536:
        raise ValueError("BGZF payload exceeds 64 KiB")

    compressor = zlib.compressobj(level, zlib.DEFLATED, -15)
    compressed = compressor.compress(payload) + compressor.flush()
    crc32 = binascii.crc32(payload) & 0xFFFFFFFF
    isize = len(payload) & 0xFFFFFFFF

    total_size = 18 + len(compressed) + 8
    if total_size > 65536:
        raise ValueError("BGZF block exceeds 64 KiB")

    header = (
        b"\x1f\x8b\x08\x04"
        + struct.pack("<LBB", 0, 0, 255)
        + struct.pack("<H", 6)
        + b"BC"
        + struct.pack("<HH", 2, total_size - 1)
    )
    trailer = struct.pack("<LL", crc32, isize)
    return header + compressed + trailer


def encode_seq(seq: str) -> bytes:
    out = bytearray()
    for index in range(0, len(seq), 2):
        first = BASE_TO_BAM[seq[index].upper()]
        second = BASE_TO_BAM[seq[index + 1].upper()] if index + 1 < len(seq) else 0
        out.append((first << 4) | second)
    return bytes(out)


def encode_qual(qual: str) -> bytes:
    return bytes(ord(ch) - 33 for ch in qual)


def encode_cigar(items: list[tuple[int, str]]) -> bytes:
    values = [(length << 4) | CIGAR_OP[op] for length, op in items]
    return b"".join(struct.pack("<I", value) for value in values)


def reg2bin(beg: int, end: int) -> int:
    end -= 1
    if beg >> 14 == end >> 14:
        return 4681 + (beg >> 14)
    if beg >> 17 == end >> 17:
        return 585 + (beg >> 17)
    if beg >> 20 == end >> 20:
        return 73 + (beg >> 20)
    if beg >> 23 == end >> 23:
        return 9 + (beg >> 23)
    if beg >> 26 == end >> 26:
        return 1 + (beg >> 26)
    return 0


def bam_record(
    *,
    qname: str,
    flag: int,
    ref_id: int,
    pos: int,
    mapq: int,
    cigar: list[tuple[int, str]],
    seq: str,
    qual: str,
    nm: int | None = None,
) -> bytes:
    read_name = qname.encode("ascii") + b"\0"
    l_seq = len(seq)
    cigar_bytes = encode_cigar(cigar)
    seq_bytes = encode_seq(seq)
    qual_bytes = encode_qual(qual)

    ref_consumed_ops = {"M", "D", "N", "=", "X"}
    ref_len = sum(length for length, op in cigar if op in ref_consumed_ops)
    if ref_id >= 0 and pos >= 0:
        bin_value = reg2bin(pos, pos + max(ref_len, 1))
    else:
        bin_value = 4680

    aux = b""
    if nm is not None:
        aux += b"NMC" + struct.pack("<B", nm)

    core = struct.pack(
        "<iiBBHHHiiii",
        ref_id,
        pos,
        len(read_name),
        mapq,
        bin_value,
        len(cigar),
        flag,
        l_seq,
        -1,
        -1,
        0,
    )
    body = core + read_name + cigar_bytes + seq_bytes + qual_bytes + aux
    return struct.pack("<i", len(body)) + body


def bam_payload() -> tuple[bytes, int, int, int]:
    header_text = (
        "@HD\tVN:1.6\tSO:coordinate\n"
        "@SQ\tSN:chrToy\tLN:1000\n"
    ).encode("ascii")

    payload = bytearray()
    payload += b"BAM\1"
    payload += struct.pack("<i", len(header_text))
    payload += header_text
    payload += struct.pack("<i", 1)
    payload += struct.pack("<i", len(b"chrToy\0"))
    payload += b"chrToy\0"
    payload += struct.pack("<i", 1000)

    first_mapped_offset = len(payload)
    payload += bam_record(
        qname="read001",
        flag=0,
        ref_id=0,
        pos=100,
        mapq=60,
        cigar=[(10, "M")],
        seq="ACGTACGTAA",
        qual="FFFFFFFFFF",
        nm=0,
    )
    payload += bam_record(
        qname="read002",
        flag=16,
        ref_id=0,
        pos=150,
        mapq=50,
        cigar=[(5, "M"), (1, "I"), (4, "M")],
        seq="TTTTACGGGA",
        qual="FFFFFFFFFF",
        nm=1,
    )
    after_last_mapped_offset = len(payload)
    payload += bam_record(
        qname="read003",
        flag=4,
        ref_id=-1,
        pos=-1,
        mapq=0,
        cigar=[],
        seq="GATTACA",
        qual="FFFFFFF",
    )

    return bytes(payload), first_mapped_offset, after_last_mapped_offset, 1


def bai_payload(first_mapped_offset: int, after_last_mapped_offset: int, no_coord: int) -> bytes:
    bin_value = reg2bin(100, 110)

    payload = bytearray()
    payload += b"BAI\1"
    payload += struct.pack("<i", 1)

    payload += struct.pack("<i", 1)
    payload += struct.pack("<I", bin_value)
    payload += struct.pack("<i", 1)
    payload += struct.pack("<QQ", first_mapped_offset, after_last_mapped_offset)

    payload += struct.pack("<i", 1)
    payload += struct.pack("<Q", first_mapped_offset)

    payload += struct.pack("<Q", no_coord)
    return bytes(payload)


def csi_payload(first_mapped_offset: int, after_last_mapped_offset: int, no_coord: int) -> bytes:
    min_shift = 14
    depth = 5
    bin_value = reg2bin(100, 110)

    payload = bytearray()
    payload += b"CSI\1"
    payload += struct.pack("<i", min_shift)
    payload += struct.pack("<i", depth)
    payload += struct.pack("<i", 0)  # l_aux
    payload += struct.pack("<i", 1)  # n_ref

    payload += struct.pack("<i", 1)  # n_bin
    payload += struct.pack("<I", bin_value)
    payload += struct.pack("<Q", first_mapped_offset)  # loffset
    payload += struct.pack("<i", 1)  # n_chunk
    payload += struct.pack("<QQ", first_mapped_offset, after_last_mapped_offset)

    payload += struct.pack("<Q", no_coord)
    return bytes(payload)


def materialize(output_dir: pathlib.Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    payload, first_mapped_offset, after_last_mapped_offset, no_coord = bam_payload()
    bam_path = output_dir / "toy_alignment.sorted.bam"
    bai_path = output_dir / "toy_alignment.sorted.bam.bai"
    csi_path = output_dir / "toy_alignment.sorted.bam.csi"

    bam_path.write_bytes(bgzf_block(payload) + BGZF_EOF)
    bai_path.write_bytes(bai_payload(first_mapped_offset, after_last_mapped_offset, no_coord))
    csi_path.write_bytes(
        bgzf_block(csi_payload(first_mapped_offset, after_last_mapped_offset, no_coord)) + BGZF_EOF
    )

    print(f"Wrote {bam_path}")
    print(f"Wrote {bai_path}")
    print(f"Wrote {csi_path}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-dir",
        default="tests/toy_data",
        type=pathlib.Path,
        help="Directory for generated toy BAM and BAI fixtures.",
    )
    args = parser.parse_args()
    materialize(args.output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
