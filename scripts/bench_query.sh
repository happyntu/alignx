#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-release/alignx"
SAMTOOLS="samtools"
INPUT_BAM=""
INPUT_AXF1=""
REGION=""
OUTPUT_DIR=""
WARMUP=1
REPEATS=5
FLAG_EXCLUDE=2308
MIN_MAPQ=20
SKIP_FILTER=0
SKIP_VIEW=0
SKIP_PILEUP=0

usage() {
  cat <<'USAGE'
Usage: scripts/bench_query.sh [options]

v1.0 query benchmark: view + pileup, unfiltered + filtered,
comparing samtools vs alignx-BAM vs alignx-AXF1.

Tool matrix (12 tool-filter combinations):
  View (unfiltered):  samtools_view, alignx_view_bam, alignx_view_axf1
  View (filtered):    samtools_view_filtered, alignx_view_bam_filtered, alignx_view_axf1_filtered
  Pileup (unfiltered): samtools_depth, alignx_pileup_bam, alignx_pileup_axf1
  Pileup (filtered):   samtools_depth_filtered, alignx_pileup_bam_filtered, alignx_pileup_axf1_filtered

Options:
  --alignx <path>        alignx executable (default: build/wsl-release/alignx)
  --samtools <path>       samtools executable (default: samtools)
  --input-bam <path>     input BAM file (required)
  --input-axf1 <path>    input AXF1 file (optional; auto-converts if absent)
  --region <region>       genomic region (required)
  --output-dir <path>    output directory for TSVs (required)
  --warmup <n>            warmup iterations (default: 1)
  --repeats <n>           timed iterations (default: 5)
  --flag-exclude <n>      FLAG bits to exclude for filtered tools (default: 2308)
  --min-mapq <n>          minimum MAPQ for filtered tools (default: 20)
  --skip-filter           skip filtered tool variants
  --skip-view             skip view benchmark (pileup only)
  --skip-pileup           skip pileup benchmark (view only)
  -h, --help              show this help

Output files (per region):
  view_<region>.tsv              raw timing
  view_<region>.summary.tsv      median/p95/min/max summary
  pileup_<region>.tsv            raw timing
  pileup_<region>.summary.tsv    summary
  combined_<region>.tsv          joined view + pileup medians with speedup ratios
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --alignx)        ALIGNX="$2";        shift 2 ;;
    --samtools)      SAMTOOLS="$2";       shift 2 ;;
    --input-bam)     INPUT_BAM="$2";     shift 2 ;;
    --input-axf1)    INPUT_AXF1="$2";    shift 2 ;;
    --region)        REGION="$2";        shift 2 ;;
    --output-dir)    OUTPUT_DIR="$2";    shift 2 ;;
    --warmup)        WARMUP="$2";        shift 2 ;;
    --repeats)       REPEATS="$2";       shift 2 ;;
    --flag-exclude)  FLAG_EXCLUDE="$2";  shift 2 ;;
    --min-mapq)      MIN_MAPQ="$2";     shift 2 ;;
    --skip-filter)   SKIP_FILTER=1;      shift ;;
    --skip-view)     SKIP_VIEW=1;        shift ;;
    --skip-pileup)   SKIP_PILEUP=1;     shift ;;
    -h|--help)       usage; exit 0 ;;
    *)               echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
  esac
done

# --- Utility functions ---

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

run_timed_shell() {
  local run_id="$1" tool="$2" stdout_file="$3" stderr_file="$4" cmd="$5"

  local start_ns end_ns exit_code stdout_bytes
  start_ns="$(date +%s%N)"
  set +e
  eval "$cmd" >"$stdout_file" 2>"$stderr_file"
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

[[ -n "$INPUT_BAM" ]] || { echo "--input-bam is required" >&2; exit 2; }
[[ -n "$REGION" ]] || { echo "--region is required" >&2; exit 2; }
[[ -n "$OUTPUT_DIR" ]] || { echo "--output-dir is required" >&2; exit 2; }
require_nonnegative_integer "--warmup" "$WARMUP"
require_nonnegative_integer "--repeats" "$REPEATS"
[[ "$REPEATS" -ge 1 ]] || { echo "--repeats must be at least 1" >&2; exit 2; }
require_nonnegative_integer "--flag-exclude" "$FLAG_EXCLUDE"
require_nonnegative_integer "--min-mapq" "$MIN_MAPQ"

require_executable "$ALIGNX"
require_executable "$SAMTOOLS"

[[ -f "$INPUT_BAM" ]] || { echo "Input BAM not found: $INPUT_BAM" >&2; exit 1; }

mkdir -p "$OUTPUT_DIR"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

region_safe="${REGION//:/_}"
region_safe="${region_safe//-/_}"
region_safe="${region_safe//,/}"

# --- AXF1 input ---

if [[ -z "$INPUT_AXF1" ]]; then
  echo "No --input-axf1 provided; auto-converting BAM to AXF1 lossless..." >&2
  INPUT_AXF1="$tmp_dir/auto_convert.axf1"
  LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}" "$ALIGNX" convert "$INPUT_BAM" -o "$INPUT_AXF1" --format AXF1 --region "$REGION"
  echo "  Auto-converted: $INPUT_AXF1 ($(stat -c%s "$INPUT_AXF1" 2>/dev/null || wc -c <"$INPUT_AXF1" | tr -d ' ') bytes)" >&2
else
  [[ -f "$INPUT_AXF1" ]] || { echo "Input AXF1 not found: $INPUT_AXF1" >&2; exit 1; }
fi

echo "" >&2
echo "Query benchmark" >&2
echo "  Region:       $REGION" >&2
echo "  BAM:          $INPUT_BAM" >&2
echo "  AXF1:         $INPUT_AXF1" >&2
echo "  Warmup:       $WARMUP  Repeats: $REPEATS" >&2
echo "  Filter:       --flag-exclude $FLAG_EXCLUDE --min-mapq $MIN_MAPQ" >&2
echo "  Skip filter:  $SKIP_FILTER" >&2
echo "  Skip view:    $SKIP_VIEW" >&2
echo "  Skip pileup:  $SKIP_PILEUP" >&2

# --- Build tool command functions ---

view_cmd() {
  local tool_id="$1"
  case "$tool_id" in
    samtools_view)
      echo "$SAMTOOLS view $INPUT_BAM $REGION" ;;
    samtools_view_filtered)
      echo "$SAMTOOLS view -F $FLAG_EXCLUDE -q $MIN_MAPQ $INPUT_BAM $REGION" ;;
    alignx_view_bam)
      echo "$ALIGNX view $INPUT_BAM $REGION" ;;
    alignx_view_bam_filtered)
      echo "$ALIGNX view $INPUT_BAM $REGION --flag-exclude $FLAG_EXCLUDE --min-mapq $MIN_MAPQ" ;;
    alignx_view_axf1)
      echo "$ALIGNX view $INPUT_AXF1 $REGION" ;;
    alignx_view_axf1_filtered)
      echo "$ALIGNX view $INPUT_AXF1 $REGION --flag-exclude $FLAG_EXCLUDE --min-mapq $MIN_MAPQ" ;;
  esac
}

pileup_cmd() {
  local tool_id="$1"
  case "$tool_id" in
    samtools_depth)
      echo "$SAMTOOLS depth -a -r $REGION $INPUT_BAM" ;;
    samtools_depth_filtered)
      echo "$SAMTOOLS depth -a -G $FLAG_EXCLUDE -Q $MIN_MAPQ -r $REGION $INPUT_BAM" ;;
    alignx_pileup_bam)
      echo "$ALIGNX pileup $INPUT_BAM $REGION" ;;
    alignx_pileup_bam_filtered)
      echo "$ALIGNX pileup $INPUT_BAM $REGION --flag-exclude $FLAG_EXCLUDE --min-mapq $MIN_MAPQ" ;;
    alignx_pileup_axf1)
      echo "$ALIGNX pileup $INPUT_AXF1 $REGION" ;;
    alignx_pileup_axf1_filtered)
      echo "$ALIGNX pileup $INPUT_AXF1 $REGION --flag-exclude $FLAG_EXCLUDE --min-mapq $MIN_MAPQ" ;;
  esac
}

# --- Build tool lists ---

VIEW_TOOLS=()
if [[ "$SKIP_VIEW" -eq 0 ]]; then
  VIEW_TOOLS+=(samtools_view alignx_view_bam alignx_view_axf1)
  if [[ "$SKIP_FILTER" -eq 0 ]]; then
    VIEW_TOOLS+=(samtools_view_filtered alignx_view_bam_filtered alignx_view_axf1_filtered)
  fi
fi

PILEUP_TOOLS=()
if [[ "$SKIP_PILEUP" -eq 0 ]]; then
  PILEUP_TOOLS+=(samtools_depth alignx_pileup_bam alignx_pileup_axf1)
  if [[ "$SKIP_FILTER" -eq 0 ]]; then
    PILEUP_TOOLS+=(samtools_depth_filtered alignx_pileup_bam_filtered alignx_pileup_axf1_filtered)
  fi
fi

# --- Phase 1: View correctness preflight ---

if [[ "${#VIEW_TOOLS[@]}" -gt 0 ]]; then
  echo "" >&2
  echo "Phase 1: View correctness preflight..." >&2

  # Unfiltered baseline
  baseline_sam="$tmp_dir/view_baseline.sam"
  "$SAMTOOLS" view "$INPUT_BAM" "$REGION" >"$baseline_sam" 2>/dev/null
  baseline_sha="$(sha256sum <"$baseline_sam" | awk '{print $1}')"
  baseline_records="$(grep -cve '^$' "$baseline_sam" || true)"
  echo "  View baseline: $baseline_records records  SHA=${baseline_sha:0:16}..." >&2

  for tool_id in alignx_view_bam alignx_view_axf1; do
    cmd="$(view_cmd "$tool_id")"
    check_sam="$tmp_dir/check_${tool_id}.sam"
    eval "$cmd" >"$check_sam" 2>/dev/null
    check_sha="$(sha256sum <"$check_sam" | awk '{print $1}')"
    if [[ "$check_sha" != "$baseline_sha" ]]; then
      echo "  FAIL $tool_id: SHA mismatch" >&2
      exit 1
    fi
    echo "  OK $tool_id: SHA match" >&2
    rm -f "$check_sam"
  done

  # Filtered baseline
  if [[ "$SKIP_FILTER" -eq 0 ]]; then
    filt_sam="$tmp_dir/view_filt_baseline.sam"
    "$SAMTOOLS" view -F "$FLAG_EXCLUDE" -q "$MIN_MAPQ" "$INPUT_BAM" "$REGION" >"$filt_sam" 2>/dev/null
    filt_sha="$(sha256sum <"$filt_sam" | awk '{print $1}')"
    filt_records="$(grep -cve '^$' "$filt_sam" || true)"
    echo "  View filtered baseline: $filt_records records  SHA=${filt_sha:0:16}..." >&2

    for tool_id in alignx_view_bam_filtered alignx_view_axf1_filtered; do
      cmd="$(view_cmd "$tool_id")"
      check_sam="$tmp_dir/check_${tool_id}.sam"
      eval "$cmd" >"$check_sam" 2>/dev/null
      check_sha="$(sha256sum <"$check_sam" | awk '{print $1}')"
      if [[ "$check_sha" != "$filt_sha" ]]; then
        echo "  FAIL $tool_id: SHA mismatch" >&2
        exit 1
      fi
      echo "  OK $tool_id: SHA match" >&2
      rm -f "$check_sam"
    done
    rm -f "$filt_sam"
  fi

  rm -f "$baseline_sam"
  echo "View correctness passed." >&2
fi

# --- Phase 2: Pileup correctness preflight ---

if [[ "${#PILEUP_TOOLS[@]}" -gt 0 ]]; then
  echo "" >&2
  echo "Phase 2: Pileup correctness preflight..." >&2

  # Unfiltered baseline
  baseline_pileup="$tmp_dir/pileup_baseline.tsv"
  "$SAMTOOLS" depth -a -r "$REGION" "$INPUT_BAM" >"$baseline_pileup" 2>/dev/null
  baseline_sha="$(sha256sum <"$baseline_pileup" | awk '{print $1}')"
  baseline_lines="$(wc -l <"$baseline_pileup" | tr -d ' ')"
  echo "  Pileup baseline: $baseline_lines positions  SHA=${baseline_sha:0:16}..." >&2

  for tool_id in alignx_pileup_bam alignx_pileup_axf1; do
    cmd="$(pileup_cmd "$tool_id")"
    check_pileup="$tmp_dir/check_${tool_id}.tsv"
    eval "$cmd" >"$check_pileup" 2>/dev/null
    check_sha="$(sha256sum <"$check_pileup" | awk '{print $1}')"
    if [[ "$check_sha" != "$baseline_sha" ]]; then
      echo "  FAIL $tool_id: SHA mismatch" >&2
      exit 1
    fi
    echo "  OK $tool_id: SHA match" >&2
    rm -f "$check_pileup"
  done

  # Filtered baseline
  if [[ "$SKIP_FILTER" -eq 0 ]]; then
    filt_pileup="$tmp_dir/pileup_filt_baseline.tsv"
    "$SAMTOOLS" depth -a -G "$FLAG_EXCLUDE" -Q "$MIN_MAPQ" -r "$REGION" "$INPUT_BAM" >"$filt_pileup" 2>/dev/null
    filt_sha="$(sha256sum <"$filt_pileup" | awk '{print $1}')"
    filt_lines="$(wc -l <"$filt_pileup" | tr -d ' ')"
    echo "  Pileup filtered baseline: $filt_lines positions  SHA=${filt_sha:0:16}..." >&2

    for tool_id in alignx_pileup_bam_filtered alignx_pileup_axf1_filtered; do
      cmd="$(pileup_cmd "$tool_id")"
      check_pileup="$tmp_dir/check_${tool_id}.tsv"
      eval "$cmd" >"$check_pileup" 2>/dev/null
      check_sha="$(sha256sum <"$check_pileup" | awk '{print $1}')"
      if [[ "$check_sha" != "$filt_sha" ]]; then
        echo "  FAIL $tool_id: SHA mismatch" >&2
        exit 1
      fi
      echo "  OK $tool_id: SHA match" >&2
      rm -f "$check_pileup"
    done
    rm -f "$filt_pileup"
  fi

  rm -f "$baseline_pileup"
  echo "Pileup correctness passed." >&2
fi

# --- Phase 3: View timing ---

stdout_file="$tmp_dir/timed_stdout"
stderr_file="$tmp_dir/timed_stderr"

if [[ "${#VIEW_TOOLS[@]}" -gt 0 ]]; then
  echo "" >&2
  echo "Phase 3: View timing ($WARMUP warmup + $REPEATS repeats)..." >&2

  VIEW_TSV="$OUTPUT_DIR/view_${region_safe}.tsv"
  VIEW_SUMMARY="$OUTPUT_DIR/view_${region_safe}.summary.tsv"

  # Warmup
  for ((w = 1; w <= WARMUP; ++w)); do
    for tool_id in "${VIEW_TOOLS[@]}"; do
      cmd="$(view_cmd "$tool_id")"
      eval "$cmd" >/dev/null 2>/dev/null
    done
  done

  # Timed runs
  {
    printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
    for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
      for tool_id in "${VIEW_TOOLS[@]}"; do
        cmd="$(view_cmd "$tool_id")"
        result="$(run_timed_shell "$run_id" "$tool_id" "$stdout_file" "$stderr_file" "$cmd")"
        printf "%s\n" "$result"
      done
    done
  } >"$VIEW_TSV"

  write_summary "$VIEW_TSV" "$VIEW_SUMMARY"
  echo "Wrote $VIEW_TSV" >&2
  echo "Wrote $VIEW_SUMMARY" >&2
fi

# --- Phase 4: Pileup timing ---

if [[ "${#PILEUP_TOOLS[@]}" -gt 0 ]]; then
  echo "" >&2
  echo "Phase 4: Pileup timing ($WARMUP warmup + $REPEATS repeats)..." >&2

  PILEUP_TSV="$OUTPUT_DIR/pileup_${region_safe}.tsv"
  PILEUP_SUMMARY="$OUTPUT_DIR/pileup_${region_safe}.summary.tsv"

  # Warmup
  for ((w = 1; w <= WARMUP; ++w)); do
    for tool_id in "${PILEUP_TOOLS[@]}"; do
      cmd="$(pileup_cmd "$tool_id")"
      eval "$cmd" >/dev/null 2>/dev/null
    done
  done

  # Timed runs
  {
    printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
    for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
      for tool_id in "${PILEUP_TOOLS[@]}"; do
        cmd="$(pileup_cmd "$tool_id")"
        result="$(run_timed_shell "$run_id" "$tool_id" "$stdout_file" "$stderr_file" "$cmd")"
        printf "%s\n" "$result"
      done
    done
  } >"$PILEUP_TSV"

  write_summary "$PILEUP_TSV" "$PILEUP_SUMMARY"
  echo "Wrote $PILEUP_TSV" >&2
  echo "Wrote $PILEUP_SUMMARY" >&2
fi

# --- Phase 5: Combined summary ---

echo "" >&2
echo "Phase 5: Combined summary..." >&2

COMBINED_TSV="$OUTPUT_DIR/combined_${region_safe}.tsv"

{
  printf "tool\tmedian_ms\n"

  if [[ -f "${VIEW_SUMMARY:-}" ]]; then
    awk -F '\t' 'NR > 1 { printf "%s\t%s\n", $1, $4 }' "$VIEW_SUMMARY"
  fi

  if [[ -f "${PILEUP_SUMMARY:-}" ]]; then
    awk -F '\t' 'NR > 1 { printf "%s\t%s\n", $1, $4 }' "$PILEUP_SUMMARY"
  fi
} >"$COMBINED_TSV"

echo "Wrote $COMBINED_TSV" >&2

echo "" >&2
echo "=== Combined results ===" >&2
column -t -s $'\t' "$COMBINED_TSV" >&2

echo "" >&2
echo "OK bench_query: $REGION" >&2
