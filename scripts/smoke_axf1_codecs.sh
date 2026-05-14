#!/usr/bin/env bash
set -euo pipefail

ALIGNX="${ALIGNX_BIN:-build/wsl-release/alignx}"
SAMTOOLS="${SAMTOOLS_BIN:-samtools}"
INSPECTOR="${AXF1_INSPECTOR:-scripts/inspect_axf1_metadata.py}"
INPUT_BAM=""
REGION=""
WORK_DIR=""

usage() {
  cat <<'USAGE'
Usage: scripts/smoke_axf1_codecs.sh --input <bam> --region <region> --work-dir <dir> [options]

Correctness-only AXF1 codec smoke check:
  1. alignx convert <bam> -o sample.axf1 --format AXF1 --region <region>
  2. alignx view sample.axf1 <region> > axf1.sam
  3. alignx view <bam> <region> > bam.sam
  4. samtools view <bam> <region> > samtools.sam
  5. verify all three SAM outputs have the same SHA256
  6. inspect AXF1 column codec distribution without decoding payloads

Options:
  --alignx <path>     alignx executable (default: $ALIGNX_BIN or build/wsl-release/alignx)
  --samtools <path>   samtools executable (default: $SAMTOOLS_BIN or samtools)
  --inspector <path>  AXF1 metadata inspector (default: $AXF1_INSPECTOR or scripts/inspect_axf1_metadata.py)
  --input <path>      input BAM
  --region <region>   region query, for example chrToy:1-250
  --work-dir <dir>    output directory for AXF1, SAM outputs, SHA256, and codec TSVs
  -h, --help          show this help

This script performs no timing, repeats, profiling, or benchmark reporting.
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --alignx)
      ALIGNX="$2"
      shift 2
      ;;
    --samtools)
      SAMTOOLS="$2"
      shift 2
      ;;
    --inspector)
      INSPECTOR="$2"
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
require_executable "$SAMTOOLS"
require_executable "$INSPECTOR"

if [[ ! -f "$INPUT_BAM" ]]; then
  echo "FAIL input: BAM does not exist: $INPUT_BAM" >&2
  exit 1
fi

mkdir -p "$WORK_DIR"

AXF1_FILE="$WORK_DIR/sample.axf1"
AXF1_SAM="$WORK_DIR/axf1.sam"
BAM_SAM="$WORK_DIR/bam.sam"
SAMTOOLS_SAM="$WORK_DIR/samtools.sam"
SHA256_OUT="$WORK_DIR/stdout.sha256"
LINES_OUT="$WORK_DIR/stdout.lines"
CODECS_OUT="$WORK_DIR/column_codecs.tsv"
COLUMNS_OUT="$WORK_DIR/columns.tsv"
SUMMARY_OUT="$WORK_DIR/summary.tsv"

rm -f \
  "$AXF1_FILE" \
  "$AXF1_SAM" \
  "$BAM_SAM" \
  "$SAMTOOLS_SAM" \
  "$SHA256_OUT" \
  "$LINES_OUT" \
  "$CODECS_OUT" \
  "$COLUMNS_OUT" \
  "$SUMMARY_OUT"

"$ALIGNX" convert "$INPUT_BAM" -o "$AXF1_FILE" --format AXF1 --region "$REGION" >/dev/null
"$ALIGNX" view "$AXF1_FILE" "$REGION" >"$AXF1_SAM"
"$ALIGNX" view "$INPUT_BAM" "$REGION" >"$BAM_SAM"
"$SAMTOOLS" view "$INPUT_BAM" "$REGION" >"$SAMTOOLS_SAM"

sha256sum "$AXF1_SAM" "$BAM_SAM" "$SAMTOOLS_SAM" >"$SHA256_OUT"
wc -l "$AXF1_SAM" "$BAM_SAM" "$SAMTOOLS_SAM" >"$LINES_OUT"

unique_hashes="$(awk '{print $1}' "$SHA256_OUT" | sort -u | wc -l)"
if [[ "$unique_hashes" -ne 1 ]]; then
  echo "FAIL axf1_codecs: AXF1, BAM, and samtools SAM outputs differ" >&2
  echo "  sha256: $SHA256_OUT" >&2
  exit 1
fi

"$INSPECTOR" "$AXF1_FILE" --column-codecs >"$CODECS_OUT"
"$INSPECTOR" "$AXF1_FILE" --columns >"$COLUMNS_OUT"

records="$(grep -cve '^$' "$AXF1_SAM" || true)"
stdout_sha256="$(awk 'NR == 1 {print $1}' "$SHA256_OUT")"

{
  echo -e "key\tvalue"
  echo -e "input\t$INPUT_BAM"
  echo -e "region\t$REGION"
  echo -e "work_dir\t$WORK_DIR"
  echo -e "axf1\t$AXF1_FILE"
  echo -e "axf1_sam\t$AXF1_SAM"
  echo -e "bam_sam\t$BAM_SAM"
  echo -e "samtools_sam\t$SAMTOOLS_SAM"
  echo -e "stdout_sha256\t$stdout_sha256"
  echo -e "records\t$records"
  echo -e "column_codecs\t$CODECS_OUT"
  echo -e "columns\t$COLUMNS_OUT"
} >"$SUMMARY_OUT"

echo "OK axf1_codecs"
cat "$SUMMARY_OUT"
