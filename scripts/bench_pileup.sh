#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-release/alignx"
SAMTOOLS="samtools"
INPUT_BAM=""
INPUT_AXF1=""
REGION=""
OUTPUT="benchmarks/results/phase2_pileup.tsv"
SUMMARY_OUTPUT=""
WARMUP=0
REPEATS=1
ALIGNX_HTS_THREADS=""
SKIP_CORRECTNESS=0

usage() {
  cat <<'USAGE'
Usage: scripts/bench_pileup.sh [options]

Benchmark alignx pileup (BAM + AXF1) vs samtools depth.

Options:
  --alignx <path>      alignx executable (default: build/wsl-release/alignx)
  --samtools <path>     samtools executable (default: samtools)
  --input-bam <path>   input BAM file (required)
  --input-axf1 <path>  input AXF1 file (optional; skips AXF1 tool if absent)
  --region <region>     region query (required)
  --output <path>       output TSV (default: benchmarks/results/phase2_pileup.tsv)
  --summary-output <path>
                        summary TSV (default: <output without .tsv>.summary.tsv)
  --warmup <n>          warmup iterations, not written to TSV (default: 0)
  --repeats <n>         measured iterations written to TSV (default: 1)
  --alignx-hts-threads <n>
                        pass --hts-threads <n> to alignx pileup (BAM path only)
  --skip-correctness    skip stdout parity check vs samtools depth
  -h, --help            show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --alignx)         ALIGNX="$2";             shift 2 ;;
    --samtools)       SAMTOOLS="$2";            shift 2 ;;
    --input-bam)      INPUT_BAM="$2";           shift 2 ;;
    --input-axf1)     INPUT_AXF1="$2";          shift 2 ;;
    --region)         REGION="$2";              shift 2 ;;
    --output)         OUTPUT="$2";              shift 2 ;;
    --summary-output) SUMMARY_OUTPUT="$2";      shift 2 ;;
    --warmup)         WARMUP="$2";              shift 2 ;;
    --repeats)        REPEATS="$2";             shift 2 ;;
    --alignx-hts-threads) ALIGNX_HTS_THREADS="$2"; shift 2 ;;
    --skip-correctness)   SKIP_CORRECTNESS=1;   shift ;;
    -h|--help)        usage; exit 0 ;;
    *)                echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

require_nonnegative_integer() {
  local name="$1" value="$2"
  if [[ ! "$value" =~ ^[0-9]+$ ]]; then
    echo "$name must be a non-negative integer: $value" >&2
    exit 2
  fi
}

require_executable() {
  local exe="$1"
  if [[ -x "$exe" ]] || command -v "$exe" >/dev/null 2>&1; then
    return 0
  fi
  echo "Required executable not found: $exe" >&2
  exit 1
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
  local run_id="$1" tool="$2" exe="$3" stdout_file="$4" stderr_file="$5"
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
  local raw_output="$1" summary_output="$2"
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

      if (time_ms < min_time[tool]) min_time[tool] = time_ms
      if (time_ms > max_time[tool]) max_time[tool] = time_ms
      if (stdout_bytes < min_stdout[tool]) min_stdout[tool] = stdout_bytes
      if (stdout_bytes > max_stdout[tool]) max_stdout[tool] = stdout_bytes
      if (exit_code != 0) nonzero_exit[tool] += 1
    }
    END {
      print "tool\truns\tavg_ms\tmedian_ms\tp95_ms\tmin_ms\tmax_ms\tp95_over_median\tmax_over_median\toutlier_runs_2x_median\tnonzero_exit_count\tstdout_bytes_min\tstdout_bytes_max"

      for (order = 1; order <= tool_count; ++order) {
        tool = tool_order[order]
        n = count[tool]
        for (i = 1; i <= n; ++i) sorted[i] = values[tool, i]

        for (i = 2; i <= n; ++i) {
          key = sorted[i]
          j = i - 1
          while (j >= 1 && sorted[j] > key) { sorted[j + 1] = sorted[j]; j -= 1 }
          sorted[j + 1] = key
        }

        if (n % 2 == 1) median = sorted[(n + 1) / 2]
        else median = (sorted[n / 2] + sorted[(n / 2) + 1]) / 2.0

        p95_index = int(0.95 * n)
        if ((0.95 * n) > p95_index) ++p95_index
        if (p95_index < 1) p95_index = 1
        if (p95_index > n) p95_index = n

        p95 = sorted[p95_index]
        avg = sum_time[tool] / n
        p95_over_median = median > 0 ? p95 / median : 0
        max_over_median = median > 0 ? max_time[tool] / median : 0
        outliers = 0
        for (i = 1; i <= n; ++i) {
          if (median > 0 && values[tool, i] > (2.0 * median)) ++outliers
        }

        printf "%s\t%d\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%d\n",
          tool, n, avg, median, p95, min_time[tool], max_time[tool],
          p95_over_median, max_over_median, outliers, nonzero_exit[tool],
          min_stdout[tool], max_stdout[tool]
      }
    }
  ' "$raw_output" >"$summary_output"
}

# --- Validation ---

if [[ -z "$INPUT_BAM" ]]; then
  echo "--input-bam is required" >&2
  exit 2
fi
if [[ -z "$REGION" ]]; then
  echo "--region is required" >&2
  exit 2
fi

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
if [[ -n "$INPUT_AXF1" ]] && [[ ! -f "$INPUT_AXF1" ]]; then
  echo "Input AXF1 not found: $INPUT_AXF1" >&2
  exit 1
fi

HAS_AXF1=0
if [[ -n "$INPUT_AXF1" ]]; then
  HAS_AXF1=1
fi

mkdir -p "$(dirname "$OUTPUT")"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

samtools_stdout="$tmp_dir/samtools.tsv"
samtools_stderr="$tmp_dir/samtools.err"
alignx_bam_stdout="$tmp_dir/alignx_bam.tsv"
alignx_bam_stderr="$tmp_dir/alignx_bam.err"
alignx_axf1_stdout="$tmp_dir/alignx_axf1.tsv"
alignx_axf1_stderr="$tmp_dir/alignx_axf1.err"

# --- Correctness preflight ---

if [[ "$SKIP_CORRECTNESS" -eq 0 ]]; then
  echo "Correctness preflight: samtools depth vs alignx pileup..." >&2

  "$SAMTOOLS" depth -a -r "$REGION" "$INPUT_BAM" >"$samtools_stdout" 2>"$samtools_stderr"
  if [[ -s "$samtools_stderr" ]]; then
    sed 's/^/samtools depth stderr: /' "$samtools_stderr" >&2
  fi

  alignx_bam_args=(pileup)
  if [[ -n "$ALIGNX_HTS_THREADS" ]]; then
    alignx_bam_args+=(--hts-threads "$ALIGNX_HTS_THREADS")
  fi
  alignx_bam_args+=("$INPUT_BAM" "$REGION")
  "$ALIGNX" "${alignx_bam_args[@]}" >"$alignx_bam_stdout" 2>"$alignx_bam_stderr"
  if [[ -s "$alignx_bam_stderr" ]]; then
    sed 's/^/alignx pileup (BAM) stderr: /' "$alignx_bam_stderr" >&2
  fi

  samtools_sha="$(sha256sum <"$samtools_stdout" | awk '{print $1}')"
  alignx_bam_sha="$(sha256sum <"$alignx_bam_stdout" | awk '{print $1}')"
  if [[ "$samtools_sha" != "$alignx_bam_sha" ]]; then
    echo "CORRECTNESS FAILURE: alignx pileup (BAM) output differs from samtools depth" >&2
    diff -u "$samtools_stdout" "$alignx_bam_stdout" | head -40 >&2
    exit 1
  fi
  echo "  alignx pileup (BAM) matches samtools depth  SHA=$samtools_sha" >&2

  if [[ "$HAS_AXF1" -eq 1 ]]; then
    "$ALIGNX" pileup "$INPUT_AXF1" "$REGION" >"$alignx_axf1_stdout" 2>"$alignx_axf1_stderr"
    if [[ -s "$alignx_axf1_stderr" ]]; then
      sed 's/^/alignx pileup (AXF1) stderr: /' "$alignx_axf1_stderr" >&2
    fi

    alignx_axf1_sha="$(sha256sum <"$alignx_axf1_stdout" | awk '{print $1}')"
    if [[ "$samtools_sha" != "$alignx_axf1_sha" ]]; then
      echo "CORRECTNESS FAILURE: alignx pileup (AXF1) output differs from samtools depth" >&2
      diff -u "$samtools_stdout" "$alignx_axf1_stdout" | head -40 >&2
      exit 1
    fi
    echo "  alignx pileup (AXF1) matches samtools depth  SHA=$samtools_sha" >&2
  fi

  echo "Correctness preflight passed." >&2
fi

# --- Benchmark ---

build_alignx_bam_args() {
  local args=(pileup)
  if [[ -n "$ALIGNX_HTS_THREADS" ]]; then
    args+=(--hts-threads "$ALIGNX_HTS_THREADS")
  fi
  args+=("$INPUT_BAM" "$REGION")
  printf '%s\n' "${args[@]}"
}

run_triple() {
  local run_id="$1" emit="$2"

  : >"$samtools_stderr"
  : >"$alignx_bam_stderr"

  local samtools_result alignx_bam_result alignx_axf1_result

  samtools_result="$(run_timed "$run_id" samtools_depth "$SAMTOOLS" "$samtools_stdout" "$samtools_stderr" depth -a -r "$REGION" "$INPUT_BAM")"

  local alignx_bam_args=(pileup)
  if [[ -n "$ALIGNX_HTS_THREADS" ]]; then
    alignx_bam_args+=(--hts-threads "$ALIGNX_HTS_THREADS")
  fi
  alignx_bam_args+=("$INPUT_BAM" "$REGION")
  alignx_bam_result="$(run_timed "$run_id" alignx_pileup_bam "$ALIGNX" "$alignx_bam_stdout" "$alignx_bam_stderr" "${alignx_bam_args[@]}")"

  if [[ "$HAS_AXF1" -eq 1 ]]; then
    : >"$alignx_axf1_stderr"
    alignx_axf1_result="$(run_timed "$run_id" alignx_pileup_axf1 "$ALIGNX" "$alignx_axf1_stdout" "$alignx_axf1_stderr" pileup "$INPUT_AXF1" "$REGION")"
  fi

  if [[ -s "$samtools_stderr" ]]; then
    sed 's/^/samtools depth stderr: /' "$samtools_stderr" >&2
  fi
  if [[ -s "$alignx_bam_stderr" ]]; then
    sed 's/^/alignx pileup (BAM) stderr: /' "$alignx_bam_stderr" >&2
  fi
  if [[ "$HAS_AXF1" -eq 1 ]] && [[ -s "$alignx_axf1_stderr" ]]; then
    sed 's/^/alignx pileup (AXF1) stderr: /' "$alignx_axf1_stderr" >&2
  fi

  if [[ "$emit" == "true" ]]; then
    printf "%s\n" "$samtools_result"
    printf "%s\n" "$alignx_bam_result"
    if [[ "$HAS_AXF1" -eq 1 ]]; then
      printf "%s\n" "$alignx_axf1_result"
    fi
  fi
}

echo "Running $WARMUP warmup + $REPEATS timed iterations..." >&2
echo "  Region: $REGION" >&2
echo "  BAM:    $INPUT_BAM" >&2
if [[ "$HAS_AXF1" -eq 1 ]]; then
  echo "  AXF1:   $INPUT_AXF1" >&2
fi

for ((run_id = -WARMUP; run_id < 0; ++run_id)); do
  run_triple "$run_id" false >/dev/null
done

{
  printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
  for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
    run_triple "$run_id" true
  done
} >"$OUTPUT"

echo "Wrote $OUTPUT" >&2
write_summary "$OUTPUT" "$SUMMARY_OUTPUT"
echo "Wrote $SUMMARY_OUTPUT" >&2
