#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-release/alignx"
SAMTOOLS="samtools"
INPUT_BAM=""
REFERENCE=""
REGION=""
OUTPUT_DIR=""
WARMUP=1
REPEATS=5
SKIP_CRAM=0

usage() {
  cat <<'USAGE'
Usage: scripts/bench_compression.sh [options]

Compression benchmark: file size, encode time, and decode time for
BAM, CRAM, and AXF1 (multiple configurations) on a single region.

Configs benchmarked:
  bam             samtools view -b -h region extract
  cram            samtools view -C -h region extract (needs --reference)
  axf1            alignx convert --format AXF1 (lossless, no zstd)
  axf1_zstd       alignx convert --format AXF1 --axf1-quality-compression zstd
  axf1_lossy      alignx convert --format AXF1 --axf1-quality-lossy illumina8
  axf1_lossy_zstd alignx convert --format AXF1 --axf1-quality-lossy illumina8
                    --axf1-quality-compression zstd

Options:
  --alignx <path>      alignx executable (default: build/wsl-release/alignx)
  --samtools <path>     samtools executable (default: samtools)
  --input-bam <path>   input BAM file (required)
  --reference <path>   GRCh38 reference FASTA for CRAM (optional; skips CRAM if absent)
  --region <region>     genomic region (required)
  --output-dir <path>   output directory for TSVs and files (required)
  --warmup <n>          warmup iterations (default: 1)
  --repeats <n>         timed iterations (default: 5)
  --skip-cram           skip CRAM config even if reference is available
  -h, --help            show this help

Output files (per region):
  size.tsv              config, file_bytes, records, ratio_vs_bam
  encode.tsv            raw timing: run_id, config, wall_time_ms, exit_code, stdout_bytes
  encode.summary.tsv    median/p95/min/max summary
  decode.tsv            raw timing
  decode.summary.tsv    summary
  combined.tsv          config, file_bytes, ratio, encode_median_ms, decode_median_ms
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --alignx)      ALIGNX="$2";      shift 2 ;;
    --samtools)    SAMTOOLS="$2";     shift 2 ;;
    --input-bam)   INPUT_BAM="$2";   shift 2 ;;
    --reference)   REFERENCE="$2";   shift 2 ;;
    --region)      REGION="$2";      shift 2 ;;
    --output-dir)  OUTPUT_DIR="$2";  shift 2 ;;
    --warmup)      WARMUP="$2";      shift 2 ;;
    --repeats)     REPEATS="$2";     shift 2 ;;
    --skip-cram)   SKIP_CRAM=1;      shift ;;
    -h|--help)     usage; exit 0 ;;
    *)             echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
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

require_executable "$ALIGNX"
require_executable "$SAMTOOLS"

[[ -f "$INPUT_BAM" ]] || { echo "Input BAM not found: $INPUT_BAM" >&2; exit 1; }

HAS_CRAM=0
if [[ "$SKIP_CRAM" -eq 0 ]] && [[ -n "$REFERENCE" ]]; then
  [[ -f "$REFERENCE" ]] || { echo "Reference FASTA not found: $REFERENCE" >&2; exit 1; }
  [[ -f "${REFERENCE}.fai" ]] || { echo "Reference FAI not found: ${REFERENCE}.fai" >&2; exit 1; }
  HAS_CRAM=1
fi
if [[ "$SKIP_CRAM" -eq 0 ]] && [[ -z "$REFERENCE" ]]; then
  echo "WARNING: --reference not provided; skipping CRAM configs" >&2
fi

mkdir -p "$OUTPUT_DIR"
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT

region_safe="${REGION//:/_}"
region_safe="${region_safe//-/_}"

# --- Build config list ---

declare -a CONFIGS=()
CONFIGS+=(bam)
if [[ "$HAS_CRAM" -eq 1 ]]; then
  CONFIGS+=(cram)
fi
CONFIGS+=(axf1 axf1_zstd axf1_lossy axf1_lossy_zstd)

echo "Compression benchmark" >&2
echo "  Region:  $REGION" >&2
echo "  BAM:     $INPUT_BAM" >&2
echo "  Ref:     ${REFERENCE:-(none)}" >&2
echo "  Configs: ${CONFIGS[*]}" >&2
echo "  Warmup:  $WARMUP  Repeats: $REPEATS" >&2

# --- Encode command per config ---

encode_cmd() {
  local config="$1" outfile="$2"
  case "$config" in
    bam)
      echo "$SAMTOOLS view -b -h $INPUT_BAM $REGION -o $outfile"
      ;;
    cram)
      echo "$SAMTOOLS view -C -h --reference $REFERENCE $INPUT_BAM $REGION -o $outfile"
      ;;
    axf1)
      echo "$ALIGNX convert $INPUT_BAM -o $outfile --format AXF1 --region $REGION"
      ;;
    axf1_zstd)
      echo "$ALIGNX convert $INPUT_BAM -o $outfile --format AXF1 --region $REGION --axf1-quality-compression zstd"
      ;;
    axf1_lossy)
      echo "$ALIGNX convert $INPUT_BAM -o $outfile --format AXF1 --region $REGION --axf1-quality-lossy illumina8"
      ;;
    axf1_lossy_zstd)
      echo "$ALIGNX convert $INPUT_BAM -o $outfile --format AXF1 --region $REGION --axf1-quality-lossy illumina8 --axf1-quality-compression zstd"
      ;;
  esac
}

decode_cmd() {
  local config="$1" infile="$2"
  case "$config" in
    bam)
      echo "$SAMTOOLS view $infile"
      ;;
    cram)
      echo "$SAMTOOLS view --reference $REFERENCE $infile"
      ;;
    axf1|axf1_zstd|axf1_lossy|axf1_lossy_zstd)
      echo "$ALIGNX view $infile $REGION"
      ;;
  esac
}

config_extension() {
  local config="$1"
  case "$config" in
    bam) echo "bam" ;;
    cram) echo "cram" ;;
    *) echo "axf1" ;;
  esac
}

skip_sha_check() {
  local config="$1"
  case "$config" in
    axf1_lossy|axf1_lossy_zstd) return 0 ;;
    cram) return 0 ;;
    *) return 1 ;;
  esac
}

# --- Phase 1: Correctness preflight ---

echo "" >&2
echo "Phase 1: Correctness preflight..." >&2

bam_baseline_sam="$tmp_dir/baseline.sam"
"$SAMTOOLS" view "$INPUT_BAM" "$REGION" >"$bam_baseline_sam" 2>/dev/null
bam_baseline_sha="$(sha256sum <"$bam_baseline_sam" | awk '{print $1}')"
bam_baseline_records="$(grep -cve '^$' "$bam_baseline_sam" || true)"
echo "  Baseline: $bam_baseline_records records  SHA=$bam_baseline_sha" >&2

for config in "${CONFIGS[@]}"; do
  ext="$(config_extension "$config")"
  outfile="$tmp_dir/preflight_${config}.${ext}"
  cmd="$(encode_cmd "$config" "$outfile")"
  eval "$cmd" >/dev/null 2>/dev/null

  decode="$(decode_cmd "$config" "$outfile")"
  decoded_sam="$tmp_dir/preflight_${config}.sam"
  eval "$decode" >"$decoded_sam" 2>/dev/null

  decoded_sha="$(sha256sum <"$decoded_sam" | awk '{print $1}')"
  decoded_records="$(grep -cve '^$' "$decoded_sam" || true)"

  if skip_sha_check "$config"; then
    if [[ "$decoded_records" -ne "$bam_baseline_records" ]]; then
      echo "  FAIL $config: record count $decoded_records != baseline $bam_baseline_records" >&2
      exit 1
    fi
    echo "  OK $config (record count): $decoded_records records" >&2
  else
    if [[ "$decoded_sha" != "$bam_baseline_sha" ]]; then
      echo "  FAIL $config: SHA mismatch  got=$decoded_sha  expected=$bam_baseline_sha" >&2
      exit 1
    fi
    echo "  OK $config: SHA match  $decoded_records records" >&2
  fi

  rm -f "$outfile" "$decoded_sam"
done

echo "Correctness preflight passed." >&2

# --- Phase 2: File size measurement ---

echo "" >&2
echo "Phase 2: File size measurement..." >&2

SIZE_TSV="$OUTPUT_DIR/size_${region_safe}.tsv"
printf "config\tfile_bytes\trecords\tratio_vs_bam\n" >"$SIZE_TSV"

declare -A FILE_BYTES
bam_bytes=0

for config in "${CONFIGS[@]}"; do
  ext="$(config_extension "$config")"
  outfile="$tmp_dir/size_${config}.${ext}"
  cmd="$(encode_cmd "$config" "$outfile")"
  eval "$cmd" >/dev/null 2>/dev/null

  file_bytes="$(stat -c%s "$outfile" 2>/dev/null || wc -c <"$outfile" | tr -d ' ')"
  FILE_BYTES[$config]="$file_bytes"

  if [[ "$config" == "bam" ]]; then
    bam_bytes="$file_bytes"
  fi

  rm -f "$outfile"
done

for config in "${CONFIGS[@]}"; do
  file_bytes="${FILE_BYTES[$config]}"
  if [[ "$bam_bytes" -gt 0 ]]; then
    ratio="$(awk -v a="$file_bytes" -v b="$bam_bytes" 'BEGIN { printf "%.4f", a / b }')"
  else
    ratio="N/A"
  fi
  printf "%s\t%d\t%d\t%s\n" "$config" "$file_bytes" "$bam_baseline_records" "$ratio" >>"$SIZE_TSV"
  echo "  $config: $file_bytes bytes  ratio=$ratio" >&2
done

echo "Wrote $SIZE_TSV" >&2

# --- Phase 3: Encode timing ---

echo "" >&2
echo "Phase 3: Encode timing ($WARMUP warmup + $REPEATS repeats)..." >&2

ENCODE_TSV="$OUTPUT_DIR/encode_${region_safe}.tsv"
ENCODE_SUMMARY="$OUTPUT_DIR/encode_${region_safe}.summary.tsv"

encode_stdout="$tmp_dir/encode_stdout"
encode_stderr="$tmp_dir/encode_stderr"

for ((w = 1; w <= WARMUP; ++w)); do
  for config in "${CONFIGS[@]}"; do
    ext="$(config_extension "$config")"
    outfile="$tmp_dir/warmup_${config}.${ext}"
    cmd="$(encode_cmd "$config" "$outfile")"
    eval "$cmd" >/dev/null 2>/dev/null
    rm -f "$outfile"
  done
done

{
  printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
  for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
    for config in "${CONFIGS[@]}"; do
      ext="$(config_extension "$config")"
      outfile="$tmp_dir/enc_${config}.${ext}"
      cmd="$(encode_cmd "$config" "$outfile")"
      result="$(run_timed_shell "$run_id" "$config" "$encode_stdout" "$encode_stderr" "$cmd")"
      printf "%s\n" "$result"
      rm -f "$outfile"
    done
  done
} >"$ENCODE_TSV"

write_summary "$ENCODE_TSV" "$ENCODE_SUMMARY"
echo "Wrote $ENCODE_TSV" >&2
echo "Wrote $ENCODE_SUMMARY" >&2

# --- Phase 4: Decode timing ---

echo "" >&2
echo "Phase 4: Decode timing ($WARMUP warmup + $REPEATS repeats)..." >&2

DECODE_TSV="$OUTPUT_DIR/decode_${region_safe}.tsv"
DECODE_SUMMARY="$OUTPUT_DIR/decode_${region_safe}.summary.tsv"

decode_stdout="$tmp_dir/decode_stdout"
decode_stderr="$tmp_dir/decode_stderr"

declare -A DECODE_FILES
for config in "${CONFIGS[@]}"; do
  ext="$(config_extension "$config")"
  outfile="$tmp_dir/decode_input_${config}.${ext}"
  cmd="$(encode_cmd "$config" "$outfile")"
  eval "$cmd" >/dev/null 2>/dev/null
  DECODE_FILES[$config]="$outfile"
done

for ((w = 1; w <= WARMUP; ++w)); do
  for config in "${CONFIGS[@]}"; do
    infile="${DECODE_FILES[$config]}"
    cmd="$(decode_cmd "$config" "$infile")"
    eval "$cmd" >/dev/null 2>/dev/null
  done
done

{
  printf "run_id\ttool\twall_time_ms\texit_code\tstdout_bytes\n"
  for ((run_id = 1; run_id <= REPEATS; ++run_id)); do
    for config in "${CONFIGS[@]}"; do
      infile="${DECODE_FILES[$config]}"
      cmd="$(decode_cmd "$config" "$infile")"
      result="$(run_timed_shell "$run_id" "$config" "$decode_stdout" "$decode_stderr" "$cmd")"
      printf "%s\n" "$result"
    done
  done
} >"$DECODE_TSV"

write_summary "$DECODE_TSV" "$DECODE_SUMMARY"
echo "Wrote $DECODE_TSV" >&2
echo "Wrote $DECODE_SUMMARY" >&2

# --- Phase 5: Combined summary ---

echo "" >&2
echo "Phase 5: Combined summary..." >&2

COMBINED_TSV="$OUTPUT_DIR/combined_${region_safe}.tsv"

awk -F '\t' '
  BEGIN {
    OFS = "\t"
  }
  FILENAME == ARGV[1] && FNR > 1 {
    size_config[++size_n] = $1
    size_bytes[$1] = $2
    size_records[$1] = $3
    size_ratio[$1] = $4
  }
  FILENAME == ARGV[2] && FNR > 1 {
    encode_median[$1] = $4
  }
  FILENAME == ARGV[3] && FNR > 1 {
    decode_median[$1] = $4
  }
  END {
    print "config", "file_bytes", "ratio_vs_bam", "records", "encode_median_ms", "decode_median_ms"
    for (i = 1; i <= size_n; ++i) {
      c = size_config[i]
      printf "%s\t%s\t%s\t%s\t%s\t%s\n", c, size_bytes[c], size_ratio[c], size_records[c], encode_median[c], decode_median[c]
    }
  }
' "$SIZE_TSV" "$ENCODE_SUMMARY" "$DECODE_SUMMARY" >"$COMBINED_TSV"

echo "Wrote $COMBINED_TSV" >&2

echo "" >&2
echo "=== Combined results ===" >&2
column -t -s $'\t' "$COMBINED_TSV" >&2

echo "" >&2
echo "OK bench_compression: $REGION" >&2
