#!/usr/bin/env bash
set -euo pipefail

SAMTOOLS="samtools"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
REGION="chrToy:1-250"
REQUIRE_RECORDS=1

usage() {
  cat <<'USAGE'
Usage: scripts/check_benchmark_input.sh [options]

Options:
  --samtools <path>        samtools executable (default: samtools)
  --input <path>           input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --region <region>        region query (default: chrToy:1-250)
  --allow-empty-region     allow the region to return zero records
  -h, --help               show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --samtools)
      SAMTOOLS="$2"
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
    --allow-empty-region)
      REQUIRE_RECORDS=0
      shift
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

check_file() {
  local label="$1"
  local path="$2"
  if [[ ! -f "$path" ]]; then
    echo "FAIL $label: missing file: $path" >&2
    exit 1
  fi
  echo "OK $label: $path"
}

require_executable "$SAMTOOLS"
check_file "bam" "$INPUT_BAM"

index_path=""
if [[ -f "${INPUT_BAM}.bai" ]]; then
  index_path="${INPUT_BAM}.bai"
elif [[ -f "${INPUT_BAM}.csi" ]]; then
  index_path="${INPUT_BAM}.csi"
elif [[ "$INPUT_BAM" == *.bam && -f "${INPUT_BAM%.bam}.bai" ]]; then
  index_path="${INPUT_BAM%.bam}.bai"
elif [[ "$INPUT_BAM" == *.bam && -f "${INPUT_BAM%.bam}.csi" ]]; then
  index_path="${INPUT_BAM%.bam}.csi"
else
  echo "FAIL index: missing .bai or .csi for $INPUT_BAM" >&2
  exit 1
fi
echo "OK index: $index_path"

echo "OK samtools: $("$SAMTOOLS" --version | head -n 1)"

if "$SAMTOOLS" quickcheck "$INPUT_BAM"; then
  echo "OK quickcheck"
else
  echo "FAIL quickcheck: samtools quickcheck failed for $INPUT_BAM" >&2
  exit 1
fi

if "$SAMTOOLS" view -H "$INPUT_BAM" >/dev/null; then
  echo "OK header"
else
  echo "FAIL header: samtools view -H failed for $INPUT_BAM" >&2
  exit 1
fi

record_count="$("$SAMTOOLS" view -c "$INPUT_BAM" "$REGION")"
echo "OK region_count: $REGION -> $record_count"

if [[ "$REQUIRE_RECORDS" -eq 1 && "$record_count" -eq 0 ]]; then
  echo "FAIL region_count: region returned zero records: $REGION" >&2
  exit 1
fi

if "$SAMTOOLS" view "$INPUT_BAM" "$REGION" >/dev/null; then
  echo "OK region_view"
else
  echo "FAIL region_view: samtools view failed for $REGION" >&2
  exit 1
fi

echo "OK benchmark_input"
