#!/usr/bin/env bash
set -euo pipefail

SAMTOOLS="samtools"
ALIGNX="build/wsl-debug/alignx"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
REGION="chrToy:1-250"
REQUIRE_RECORDS=1
CHECK_AXF_INDEX=1
AXF_INDEX_OUTPUT=""

usage() {
  cat <<'USAGE'
Usage: scripts/check_benchmark_input.sh [options]

Options:
  --samtools <path>        samtools executable (default: samtools)
  --alignx <path>          alignx executable (default: build/wsl-debug/alignx)
  --input <path>           input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --region <region>        region query (default: chrToy:1-250)
  --axf-index-output <path>
                           keep the generated AXF index at this path
  --skip-axf-index         skip alignx index preflight
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
    --axf-index-output)
      AXF_INDEX_OUTPUT="$2"
      shift 2
      ;;
    --skip-axf-index)
      CHECK_AXF_INDEX=0
      shift
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

find_bam_index() {
  local input="$1"
  local candidate
  for candidate in "${input}.csi" "${input}.bai"; do
    if [[ -f "$candidate" ]]; then
      printf "%s\n" "$candidate"
      return 0
    fi
  done

  if [[ "$input" == *.bam ]]; then
    for candidate in "${input%.bam}.csi" "${input%.bam}.bai"; do
      if [[ -f "$candidate" ]]; then
        printf "%s\n" "$candidate"
        return 0
      fi
    done
  fi
  return 1
}

cleanup_dirs=()
cleanup() {
  for dir in "${cleanup_dirs[@]}"; do
    rm -rf "$dir"
  done
}
trap cleanup EXIT

require_executable "$SAMTOOLS"
if [[ "$CHECK_AXF_INDEX" -eq 1 ]]; then
  require_executable "$ALIGNX"
fi
check_file "bam" "$INPUT_BAM"

if ! index_path="$(find_bam_index "$INPUT_BAM")"; then
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

if [[ "$CHECK_AXF_INDEX" -eq 1 ]]; then
  log_dir="$(mktemp -d)"
  cleanup_dirs+=("$log_dir")

  if [[ -n "$AXF_INDEX_OUTPUT" ]]; then
    axf_index_output="$AXF_INDEX_OUTPUT"
    mkdir -p "$(dirname "$axf_index_output")"
  else
    axf_index_output="$log_dir/input.axf.idx"
  fi

  axf_index_stdout="$log_dir/alignx_index.out"
  axf_index_stderr="$log_dir/alignx_index.err"
  if "$ALIGNX" index "$INPUT_BAM" -o "$axf_index_output" >"$axf_index_stdout" 2>"$axf_index_stderr"; then
    if [[ ! -s "$axf_index_output" ]]; then
      echo "FAIL alignx_index: empty AXF index output: $axf_index_output" >&2
      exit 1
    fi
    echo "OK alignx_index: $axf_index_output"
    sed 's/^/OK alignx_index_info: /' "$axf_index_stdout"
  else
    sed 's/^/alignx index stderr: /' "$axf_index_stderr" >&2
    echo "FAIL alignx_index: alignx index failed for $INPUT_BAM" >&2
    exit 1
  fi
fi

echo "OK benchmark_input"
