#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-debug/alignx"
SAMTOOLS="samtools"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
REGION="chrToy:1-250"
OUTPUT="benchmarks/results/phase1_view_chrtoy_samtools.tsv"
WARMUP=0
REPEATS=1
PREPARE_AXF_INDEX=1
AXF_INDEX_OUTPUT=""

usage() {
  cat <<'USAGE'
Usage: scripts/bench_region_query.sh [options]

Options:
  --alignx <path>    alignx executable (default: build/wsl-debug/alignx)
  --samtools <path>  samtools executable (default: samtools)
  --input <path>     input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --region <region>  region query (default: chrToy:1-250)
  --output <path>    output TSV (default: benchmarks/results/phase1_view_chrtoy_samtools.tsv)
  --warmup <n>       warmup iterations, not written to TSV (default: 0)
  --repeats <n>      measured iterations written to TSV (default: 1)
  --axf-index-output <path>
                     keep the generated AXF index at this path
  --skip-axf-index   skip alignx index preflight before timing
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
    --warmup)
      WARMUP="$2"
      shift 2
      ;;
    --repeats)
      REPEATS="$2"
      shift 2
      ;;
    --axf-index-output)
      AXF_INDEX_OUTPUT="$2"
      shift 2
      ;;
    --skip-axf-index)
      PREPARE_AXF_INDEX=0
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

require_nonnegative_integer() {
  local name="$1"
  local value="$2"
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    echo "$name must be a non-negative integer: $value" >&2
    exit 2
  fi
}

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

run_timed() {
  local run_id="$1"
  local tool="$2"
  local exe="$3"
  local stdout_file="$4"
  local stderr_file="$5"
  shift 5

  local start_ns end_ns exit_code stdout_bytes
  start_ns="$(date +%s%N)"
  set +e
  "$exe" "$@" >"$stdout_file" 2>"$stderr_file"
  exit_code=$?
  set -e
  end_ns="$(date +%s%N)"
  stdout_bytes="$(wc -c <"$stdout_file" | tr -d ' ')"

  awk -v tool="$tool" \
      -v run_id="$run_id" \
      -v start="$start_ns" \
      -v end="$end_ns" \
      -v exit_code="$exit_code" \
      -v stdout_bytes="$stdout_bytes" \
      'BEGIN { printf "%d\t%s\t%.3f\t%d\t%d\n", run_id, tool, (end - start) / 1000000.0, exit_code, stdout_bytes }'
}

require_nonnegative_integer "--warmup" "$WARMUP"
require_nonnegative_integer "--repeats" "$REPEATS"
if [[ "$REPEATS" -lt 1 ]]; then
  echo "--repeats must be at least 1" >&2
  exit 2
fi

require_executable "$ALIGNX"
require_executable "$SAMTOOLS"

if [[ ! -f "$INPUT_BAM" ]]; then
  echo "Input BAM not found: $INPUT_BAM" >&2
  exit 1
fi

if ! input_index="$(find_bam_index "$INPUT_BAM")"; then
  echo "Input BAM index not found: expected .bai or .csi for $INPUT_BAM" >&2
  exit 1
fi

mkdir -p "$(dirname "$OUTPUT")"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

if [[ "$PREPARE_AXF_INDEX" -eq 1 ]]; then
  if [[ -n "$AXF_INDEX_OUTPUT" ]]; then
    axf_index_output="$AXF_INDEX_OUTPUT"
    mkdir -p "$(dirname "$axf_index_output")"
  else
    axf_index_output="$tmp_dir/input.axf.idx"
  fi

  if ! "$ALIGNX" index "$INPUT_BAM" -o "$axf_index_output" >"$tmp_dir/alignx_index.tsv" 2>"$tmp_dir/alignx_index.err"; then
    sed 's/^/alignx index stderr: /' "$tmp_dir/alignx_index.err" >&2
    echo "alignx index preflight failed for $INPUT_BAM" >&2
    exit 1
  fi
  if [[ ! -s "$axf_index_output" ]]; then
    echo "alignx index preflight produced an empty AXF index: $axf_index_output" >&2
    exit 1
  fi
  sed 's/^/alignx index: /' "$tmp_dir/alignx_index.tsv" >&2
else
  echo "Skipping AXF index preflight; found input index: $input_index" >&2
fi

alignx_stdout="$tmp_dir/alignx.sam"
alignx_stderr="$tmp_dir/alignx.err"
samtools_stdout="$tmp_dir/samtools.sam"
samtools_stderr="$tmp_dir/samtools.err"

run_pair() {
  local run_id="$1"
  local emit="$2"

  : >"$alignx_stderr"
  : >"$samtools_stderr"

  local alignx_result samtools_result
  alignx_result="$(run_timed "$run_id" alignx "$ALIGNX" "$alignx_stdout" "$alignx_stderr" view "$INPUT_BAM" "$REGION")"
  samtools_result="$(run_timed "$run_id" samtools "$SAMTOOLS" "$samtools_stdout" "$samtools_stderr" view "$INPUT_BAM" "$REGION")"

  if [[ -s "$alignx_stderr" ]]; then
    sed 's/^/alignx stderr: /' "$alignx_stderr" >&2
  fi
  if [[ -s "$samtools_stderr" ]]; then
    sed 's/^/samtools stderr: /' "$samtools_stderr" >&2
  fi

  if ! diff -u "$samtools_stdout" "$alignx_stdout" >"$tmp_dir/stdout.diff"; then
    cat "$tmp_dir/stdout.diff" >&2
    echo "alignx output differs from samtools output on run $run_id" >&2
    exit 1
  fi

  if [[ "$emit" == "true" ]]; then
    printf "%s\n" "$alignx_result"
    printf "%s\n" "$samtools_result"
  fi
}

for ((run_id = -WARMUP; run_id < 0; ++run_id)); do
  run_pair "$run_id" false >/dev/null
done

{
  printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
  for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
    run_pair "$run_id" true
  done
} >"$OUTPUT"

echo "Wrote $OUTPUT"
