#!/usr/bin/env bash
set -euo pipefail

ALIGNX="build/wsl-release/alignx"
INPUT_BAM="tests/toy_data/toy_alignment.sorted.bam"
WORK_DIR="benchmarks/results/axf1_chunk_sizing_plan"
MANIFEST_OUT=""
INSPECT_AXF1=""
REGIONS=()
VARIANTS=()

usage() {
  cat <<'USAGE'
Usage: scripts/plan_axf1_chunk_sizing.sh [options]

Options:
  --alignx <path>      alignx executable (default: build/wsl-release/alignx)
  --input <path>       input BAM (default: tests/toy_data/toy_alignment.sorted.bam)
  --work-dir <path>    planning output directory (default: benchmarks/results/axf1_chunk_sizing_plan)
  --region <region>    add a query region; may be repeated
  --variant <name>     add a policy variant; may be repeated
                       variants: baseline, smaller_chunks, denser_chunks, span_biased, span_tight
  --manifest-out <path>
                       write a TSV manifest of planned commands
  --inspect-axf1 <path>
                       print metadata-only inspection commands for an existing AXF1 file
  -h, --help           show this help
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
    --work-dir)
      WORK_DIR="$2"
      shift 2
      ;;
    --region)
      REGIONS+=("$2")
      shift 2
      ;;
    --variant)
      VARIANTS+=("$2")
      shift 2
      ;;
    --manifest-out)
      MANIFEST_OUT="$2"
      shift 2
      ;;
    --inspect-axf1)
      INSPECT_AXF1="$2"
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

if [[ ${#REGIONS[@]} -eq 0 ]]; then
  REGIONS=("chr1:1000000-1010000" "chr1:1000000-2000000" "chr1:20000000-21000000" "chr1:50000000-51000000")
fi
if [[ ${#VARIANTS[@]} -eq 0 ]]; then
  VARIANTS=("baseline" "smaller_chunks" "denser_chunks" "span_biased" "span_tight")
fi

variant_env() {
  case "$1" in
    baseline)
      printf "%s" ""
      ;;
    smaller_chunks)
      printf "%s" "ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES=131072 ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES=262144 ALIGNX_AXF1_MAX_RECORDS=2048 ALIGNX_AXF1_MAX_GENOMIC_SPAN=500000"
      ;;
    denser_chunks)
      printf "%s" "ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES=524288 ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES=1048576 ALIGNX_AXF1_MAX_RECORDS=8192 ALIGNX_AXF1_MAX_GENOMIC_SPAN=1000000"
      ;;
    span_biased)
      printf "%s" "ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES=262144 ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES=524288 ALIGNX_AXF1_MAX_RECORDS=4096 ALIGNX_AXF1_MAX_GENOMIC_SPAN=250000"
      ;;
    span_tight)
      printf "%s" "ALIGNX_AXF1_TARGET_UNCOMPRESSED_BYTES=262144 ALIGNX_AXF1_MAX_UNCOMPRESSED_BYTES=524288 ALIGNX_AXF1_MAX_RECORDS=4096 ALIGNX_AXF1_MAX_GENOMIC_SPAN=50000"
      ;;
    *)
      echo "Unknown variant: $1" >&2
      exit 2
      ;;
  esac
}

variant_suffix() {
  case "$1" in
    baseline) printf "%s" baseline ;;
    smaller_chunks) printf "%s" smaller_chunks ;;
    denser_chunks) printf "%s" denser_chunks ;;
    span_biased) printf "%s" span_biased ;;
    span_tight) printf "%s" span_tight ;;
    *) echo "Unknown variant: $1" >&2; exit 2 ;;
  esac
}

shell_join() {
  local out=""
  local arg
  for arg in "$@"; do
    if [[ -n "$out" ]]; then
      out+=" "
    fi
    printf -v out "%s%q" "$out" "$arg"
  done
  printf "%s" "$out"
}

mkdir -p "$WORK_DIR"

if [[ -n "$INSPECT_AXF1" ]]; then
  echo "mode\tinspect_only"
  echo "axf1\t$INSPECT_AXF1"
  echo "summary_command\t$(shell_join python scripts/inspect_axf1_metadata.py "$INSPECT_AXF1")"
  echo "columns_command\t$(shell_join python scripts/summarize_axf1_columns.py "$INSPECT_AXF1")"
  exit 0
fi

if [[ -n "$MANIFEST_OUT" ]]; then
  mkdir -p "$(dirname "$MANIFEST_OUT")"
  {
    printf "variant\tregion\toutput_axf\tcommand\n"
    for variant in "${VARIANTS[@]}"; do
      env_prefix="$(variant_env "$variant")"
      suffix="$(variant_suffix "$variant")"
      for region in "${REGIONS[@]}"; do
        output_axf="$WORK_DIR/${suffix}_${region//[:]/_}.axf1"
        command=""
        if [[ -n "$env_prefix" ]]; then
          command="$env_prefix "
        fi
        command+="$(shell_join "$ALIGNX" convert "$INPUT_BAM" -o "$output_axf" --format AXF1 --region "$region")"
        printf "%s\t%s\t%s\t%s\n" "$variant" "$region" "$output_axf" "$command"
      done
    done
  } >"$MANIFEST_OUT"
  echo "Wrote $MANIFEST_OUT"
  exit 0
fi

echo "Planning only. Use --manifest-out to write the command matrix."
echo "Work dir: $WORK_DIR"
for variant in "${VARIANTS[@]}"; do
  printf 'variant\t%s\t%s\n' "$variant" "$(variant_env "$variant")"
done
for region in "${REGIONS[@]}"; do
  printf 'region\t%s\n' "$region"
done
