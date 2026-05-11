#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-debug/alignx"
SAMTOOLS="samtools"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
REGION="chrToy:1-250"
OUTPUT="benchmarks/results/phase1_view_chrtoy_samtools.tsv"

usage() {
  cat <<'USAGE'
Usage: scripts/bench_region_query.sh [options]

Options:
  --alignx <path>    alignx executable (default: build/wsl-debug/alignx)
  --samtools <path>  samtools executable (default: samtools)
  --input <path>     input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --region <region>  region query (default: chrToy:1-250)
  --output <path>    output TSV (default: benchmarks/results/phase1_view_chrtoy_samtools.tsv)
  -h, --help         show this help
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
    --input)
      INPUT_BAM="$2"
      shift 2
      ;;
    --region)
      REGION="$2"
      shift 2
      ;;
    --output)
      OUTPUT="$2"
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

require_executable() {
  local exe="$1"
  if [[ -x "$exe" ]]; then
    return 0
  fi
  if command -v "$exe" >/dev/null 2>&1; then
    return 0
  fi
  echo "Required executable not found: $exe" >&2
  exit 1
}

run_timed() {
  local tool="$1"
  local exe="$2"
  local stdout_file="$3"
  local stderr_file="$4"
  shift 4

  local start_ns end_ns exit_code stdout_bytes
  start_ns="$(date +%s%N)"
  set +e
  "$exe" "$@" >"$stdout_file" 2>"$stderr_file"
  exit_code=$?
  set -e
  end_ns="$(date +%s%N)"
  stdout_bytes="$(wc -c <"$stdout_file" | tr -d ' ')"

  awk -v tool="$tool" \
      -v start="$start_ns" \
      -v end="$end_ns" \
      -v exit_code="$exit_code" \
      -v stdout_bytes="$stdout_bytes" \
      'BEGIN { printf "%s\t%.3f\t%d\t%d\n", tool, (end - start) / 1000000.0, exit_code, stdout_bytes }'
}

require_executable "$ALIGNX"
require_executable "$SAMTOOLS"

if [[ ! -f "$INPUT_BAM" ]]; then
  echo "Input BAM not found: $INPUT_BAM" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

alignx_stdout="$tmp_dir/alignx.sam"
alignx_stderr="$tmp_dir/alignx.err"
samtools_stdout="$tmp_dir/samtools.sam"
samtools_stderr="$tmp_dir/samtools.err"

alignx_result="$(run_timed alignx "$ALIGNX" "$alignx_stdout" "$alignx_stderr" view "$INPUT_BAM" "$REGION")"
samtools_result="$(run_timed samtools "$SAMTOOLS" "$samtools_stdout" "$samtools_stderr" view "$INPUT_BAM" "$REGION")"

if [[ -s "$alignx_stderr" ]]; then
  sed 's/^/alignx stderr: /' "$alignx_stderr" >&2
fi
if [[ -s "$samtools_stderr" ]]; then
  sed 's/^/samtools stderr: /' "$samtools_stderr" >&2
fi

if ! diff -u "$samtools_stdout" "$alignx_stdout" >"$tmp_dir/stdout.diff"; then
  cat "$tmp_dir/stdout.diff" >&2
  echo "alignx output differs from samtools output" >&2
  exit 1
fi

{
  printf "tool\twall_time_ms\texit_code\tstdout_bytes\n"
  printf "%s\n" "$alignx_result"
  printf "%s\n" "$samtools_result"
} >"$OUTPUT"

echo "Wrote $OUTPUT"
