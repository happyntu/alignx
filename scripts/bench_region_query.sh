#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-release/alignx"
SAMTOOLS="samtools"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
REGION="chrToy:1-250"
OUTPUT="benchmarks/results/phase1_view_chrtoy_samtools.tsv"
SUMMARY_OUTPUT=""
WARMUP=0
REPEATS=1
PREPARE_AXF_INDEX=1
AXF_INDEX_OUTPUT=""
ALIGNX_HTS_THREADS=""

usage() {
  cat <<'USAGE'
Usage: scripts/bench_region_query.sh [options]

Options:
  --alignx <path>    alignx executable (default: build/wsl-release/alignx)
  --samtools <path>  samtools executable (default: samtools)
  --input <path>     input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --region <region>  region query (default: chrToy:1-250)
  --output <path>    output TSV (default: benchmarks/results/phase1_view_chrtoy_samtools.tsv)
  --summary-output <path>
                     summary TSV (default: <output without .tsv>.summary.tsv)
  --warmup <n>       warmup iterations, not written to TSV (default: 0)
  --repeats <n>      measured iterations written to TSV (default: 1)
  --axf-index-output <path>
                     keep the generated AXF index at this path
  --alignx-hts-threads <n>
                     pass --hts-threads <n> to alignx view only
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
    --summary-output)
      SUMMARY_OUTPUT="$2"
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
    --alignx-hts-threads)
      ALIGNX_HTS_THREADS="$2"
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

default_summary_output() {
  local output="$1"
  if [[ "$output" == *.tsv ]]; then
    printf "%s.summary.tsv\n" "${output%.tsv}"
  else
    printf "%s.summary.tsv\n" "$output"
  fi
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

write_summary() {
  local raw_output="$1"
  local summary_output="$2"

  mkdir -p "$(dirname "$summary_output")"
  awk -F '\t' '
    NR == 1 { next }
    {
      tool = $2
      time_ms = $3 + 0.0
      exit_code = $4 + 0
      stdout_bytes = $5 + 0

      if (!(tool in seen)) {
        seen[tool] = 1
        tool_order[++tool_count] = tool
        min_time[tool] = time_ms
        max_time[tool] = time_ms
        min_stdout[tool] = stdout_bytes
        max_stdout[tool] = stdout_bytes
      }

      count[tool] += 1
      sum_time[tool] += time_ms
      values[tool, count[tool]] = time_ms

      if (time_ms < min_time[tool]) {
        min_time[tool] = time_ms
      }
      if (time_ms > max_time[tool]) {
        max_time[tool] = time_ms
      }
      if (stdout_bytes < min_stdout[tool]) {
        min_stdout[tool] = stdout_bytes
      }
      if (stdout_bytes > max_stdout[tool]) {
        max_stdout[tool] = stdout_bytes
      }
      if (exit_code != 0) {
        nonzero_exit[tool] += 1
      }
    }
    END {
      print "tool\truns\tavg_ms\tmedian_ms\tp95_ms\tmin_ms\tmax_ms\tp95_over_median\tmax_over_median\toutlier_runs_2x_median\tnonzero_exit_count\tstdout_bytes_min\tstdout_bytes_max"

      for (order = 1; order <= tool_count; ++order) {
        tool = tool_order[order]
        n = count[tool]
        for (i = 1; i <= n; ++i) {
          sorted[i] = values[tool, i]
        }

        for (i = 2; i <= n; ++i) {
          key = sorted[i]
          j = i - 1
          while (j >= 1 && sorted[j] > key) {
            sorted[j + 1] = sorted[j]
            j -= 1
          }
          sorted[j + 1] = key
        }

        if (n % 2 == 1) {
          median = sorted[(n + 1) / 2]
        } else {
          median = (sorted[n / 2] + sorted[(n / 2) + 1]) / 2.0
        }

        p95_index = int(0.95 * n)
        if ((0.95 * n) > p95_index) {
          ++p95_index
        }
        if (p95_index < 1) {
          p95_index = 1
        }
        if (p95_index > n) {
          p95_index = n
        }

        p95 = sorted[p95_index]
        avg = sum_time[tool] / n
        p95_over_median = median > 0 ? p95 / median : 0
        max_over_median = median > 0 ? max_time[tool] / median : 0
        outliers = 0
        for (i = 1; i <= n; ++i) {
          if (median > 0 && values[tool, i] > (2.0 * median)) {
            ++outliers
          }
        }

        printf "%s\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%d\n",
          tool, n, avg, median, p95, min_time[tool], max_time[tool],
          p95_over_median, max_over_median, outliers, nonzero_exit[tool],
          min_stdout[tool], max_stdout[tool]
      }
    }
  ' "$raw_output" >"$summary_output"
}

require_nonnegative_integer "--warmup" "$WARMUP"
require_nonnegative_integer "--repeats" "$REPEATS"
if [[ -n "$ALIGNX_HTS_THREADS" ]]; then
  require_nonnegative_integer "--alignx-hts-threads" "$ALIGNX_HTS_THREADS"
fi
if [[ "$REPEATS" -lt 1 ]]; then
  echo "--repeats must be at least 1" >&2
  exit 2
fi

if [[ -z "$SUMMARY_OUTPUT" ]]; then
  SUMMARY_OUTPUT="$(default_summary_output "$OUTPUT")"
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
  local alignx_args=(view)
  if [[ -n "$ALIGNX_HTS_THREADS" ]]; then
    alignx_args+=(--hts-threads "$ALIGNX_HTS_THREADS")
  fi
  alignx_args+=("$INPUT_BAM" "$REGION")

  alignx_result="$(run_timed "$run_id" alignx "$ALIGNX" "$alignx_stdout" "$alignx_stderr" "${alignx_args[@]}")"
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
write_summary "$OUTPUT" "$SUMMARY_OUTPUT"
echo "Wrote $SUMMARY_OUTPUT"
