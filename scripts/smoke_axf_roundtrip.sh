#!/usr/bin/env bash
set -euo pipefail

ALIGNX="${ALIGNX_BIN:-build/wsl-release/alignx}"
INPUT_BAM=""
REGION=""
WORK_DIR=""
FORMAT="AXF0"
AXF1_QUALITY_COMPRESSION="none"

usage() {
  cat <<'USAGE'
Usage: scripts/smoke_axf_roundtrip.sh --input <bam> --region <region> --work-dir <dir> [options]

Correctness-only smoke check:
  1. alignx view <bam> <region> > bam.sam
  2. alignx convert <bam> -o sample.axf --region <region>
  3. alignx view sample.axf <region> > axf.sam
  4. diff -u bam.sam axf.sam > diff.txt

Options:
  --alignx <path>    alignx executable (default: $ALIGNX_BIN or build/wsl-release/alignx)
  --input <path>     input BAM
  --region <region>  region query, for example chrToy:1-250
  --work-dir <dir>   output directory for bam.sam, sample.axf, axf.sam, diff.txt
  --format <AXF0|AXF1>
                     output AXF format for the convert step (default: AXF0)
  --axf1-quality-compression <none|zstd>
                     pass AXF1 quality compression to alignx convert (default: none)
  -h, --help         show this help

This script performs no timing, repeats, profiling, or benchmark reporting.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --alignx)
      ALIGNX="$2"
      shift 2
      ;;
    --input)
      INPUT_BAM="$2"
      shift 2
      ;;
    --region)
      REGION="$2"
      shift 2
      ;;
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --format)
      FORMAT="$2"
      shift 2
      ;;
    --axf1-quality-compression)
      AXF1_QUALITY_COMPRESSION="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

fail_usage() {
  echo "FAIL usage: $1" >&2
  usage >&2
  exit 2
}

require_executable() {
  local exe="$1"
  if [[ -x "$exe" ]]; then
    return 0
  fi
  if command -v "$exe" >/dev/null 2>&1; then
    return 0
  fi
  echo "FAIL executable: required executable not found: $exe" >&2
  exit 1
}

[[ -n "$INPUT_BAM" ]] || fail_usage "--input is required"
[[ -n "$REGION" ]] || fail_usage "--region is required"
[[ -n "$WORK_DIR" ]] || fail_usage "--work-dir is required"

require_executable "$ALIGNX"

FORMAT_UPPER="$(printf '%s' "$FORMAT" | tr '[:lower:]' '[:upper:]')"
if [[ "$FORMAT_UPPER" != "AXF0" && "$FORMAT_UPPER" != "AXF1" ]]; then
  echo "FAIL format: --format must be AXF0 or AXF1" >&2
  exit 2
fi
if [[ "$AXF1_QUALITY_COMPRESSION" != "none" && "$AXF1_QUALITY_COMPRESSION" != "zstd" ]]; then
  echo "FAIL axf1-quality-compression: must be none or zstd" >&2
  exit 2
fi
if [[ "$FORMAT_UPPER" != "AXF1" && "$AXF1_QUALITY_COMPRESSION" != "none" ]]; then
  echo "FAIL axf1-quality-compression: requires --format AXF1" >&2
  exit 2
fi

if [[ ! -f "$INPUT_BAM" ]]; then
  echo "FAIL input: BAM does not exist: $INPUT_BAM" >&2
  exit 1
fi

mkdir -p "$WORK_DIR"

BAM_SAM="$WORK_DIR/bam.sam"
AXF_FILE="$WORK_DIR/sample.axf"
AXF_SAM="$WORK_DIR/axf.sam"
DIFF_OUT="$WORK_DIR/diff.txt"

rm -f "$BAM_SAM" "$AXF_FILE" "$AXF_SAM" "$DIFF_OUT"

"$ALIGNX" view "$INPUT_BAM" "$REGION" >"$BAM_SAM"
convert_args=(
  convert
  "$INPUT_BAM"
  -o "$AXF_FILE"
  --region "$REGION"
  --format "$FORMAT_UPPER"
)
if [[ "$FORMAT_UPPER" == "AXF1" && "$AXF1_QUALITY_COMPRESSION" != "none" ]]; then
  convert_args+=(--axf1-quality-compression "$AXF1_QUALITY_COMPRESSION")
fi
"$ALIGNX" "${convert_args[@]}" >/dev/null
"$ALIGNX" view "$AXF_FILE" "$REGION" >"$AXF_SAM"

if ! diff -u "$BAM_SAM" "$AXF_SAM" >"$DIFF_OUT"; then
  echo "FAIL axf_roundtrip: BAM and AXF SAM outputs differ" >&2
  echo "  bam_sam: $BAM_SAM" >&2
  echo "  axf_sam: $AXF_SAM" >&2
  echo "  diff:    $DIFF_OUT" >&2
  exit 1
fi

BAM_BYTES=$(wc -c <"$BAM_SAM")
AXF_BYTES=$(wc -c <"$AXF_SAM")
RECORDS=$(grep -cve '^$' "$AXF_SAM" || true)

echo "OK axf_roundtrip"
echo "input	$INPUT_BAM"
echo "region	$REGION"
echo "format	$FORMAT_UPPER"
echo "axf1_quality_compression	$AXF1_QUALITY_COMPRESSION"
echo "work_dir	$WORK_DIR"
echo "bam_sam	$BAM_SAM"
echo "axf	$AXF_FILE"
echo "axf_sam	$AXF_SAM"
echo "diff	$DIFF_OUT"
echo "records	$RECORDS"
echo "bam_stdout_bytes	$BAM_BYTES"
echo "axf_stdout_bytes	$AXF_BYTES"
