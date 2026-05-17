#!/usr/bin/env bash
set -euo pipefail

ALIGNX="/mypool/alignx/bin/alignx"
BAM="/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam"
REF="/mypool/biotools-benchmark-data/references/GRCh38/GCA_000001405.15_GRCh38_no_alt_analysis_set.fasta"
REGION="chr1:1000000-2000000"
OUTDIR="/mypool/alignx/results/mt_encode"
WARMUP=1
REPEATS=5

export LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib

mkdir -p "$OUTDIR"

echo "=== Sequential encode ==="
for i in $(seq 1 $WARMUP); do
  ALIGNX_SEQUENTIAL_ENCODE=1 "$ALIGNX" convert "$BAM" -o "$OUTDIR/seq.axf1" \
    --format AXF1 --region "$REGION" --reference "$REF" --axf1-compression zstd 2>/dev/null
done

printf "run_id\tmode\twall_ms\n" > "$OUTDIR/timing.tsv"
for i in $(seq 1 $REPEATS); do
  t0=$(date +%s%N)
  ALIGNX_SEQUENTIAL_ENCODE=1 "$ALIGNX" convert "$BAM" -o "$OUTDIR/seq.axf1" \
    --format AXF1 --region "$REGION" --reference "$REF" --axf1-compression zstd 2>/dev/null
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  printf "%d\tsequential\t%d\n" "$i" "$ms" >> "$OUTDIR/timing.tsv"
done
SEQ_SHA=$(sha256sum "$OUTDIR/seq.axf1" | awk '{print $1}')

echo "=== Parallel encode ==="
for i in $(seq 1 $WARMUP); do
  "$ALIGNX" convert "$BAM" -o "$OUTDIR/par.axf1" \
    --format AXF1 --region "$REGION" --reference "$REF" --axf1-compression zstd 2>/dev/null
done

for i in $(seq 1 $REPEATS); do
  t0=$(date +%s%N)
  "$ALIGNX" convert "$BAM" -o "$OUTDIR/par.axf1" \
    --format AXF1 --region "$REGION" --reference "$REF" --axf1-compression zstd 2>/dev/null
  t1=$(date +%s%N)
  ms=$(( (t1 - t0) / 1000000 ))
  printf "%d\tparallel\t%d\n" "$i" "$ms" >> "$OUTDIR/timing.tsv"
done
PAR_SHA=$(sha256sum "$OUTDIR/par.axf1" | awk '{print $1}')

echo ""
echo "=== SHA-256 check ==="
echo "Sequential: $SEQ_SHA"
echo "Parallel:   $PAR_SHA"
if [ "$SEQ_SHA" = "$PAR_SHA" ]; then
  echo "PASS: output identical"
else
  echo "FAIL: output differs!"
fi

SEQ_SAM_SHA=$("$ALIGNX" view "$OUTDIR/seq.axf1" "$REGION" 2>/dev/null | sha256sum | awk '{print $1}')
PAR_SAM_SHA=$("$ALIGNX" view "$OUTDIR/par.axf1" "$REGION" 2>/dev/null | sha256sum | awk '{print $1}')
echo "SAM seq: $SEQ_SAM_SHA"
echo "SAM par: $PAR_SAM_SHA"
if [ "$SEQ_SAM_SHA" = "$PAR_SAM_SHA" ]; then
  echo "PASS: SAM stdout identical"
else
  echo "FAIL: SAM stdout differs!"
fi

echo ""
echo "=== Timing results ==="
cat "$OUTDIR/timing.tsv"

echo ""
echo "=== Summary (median) ==="
awk -F'\t' 'NR>1 {a[$2][++c[$2]]=$3} END {for (m in a) {n=c[m]; asort(a[m]); med = (n%2==1) ? a[m][int(n/2)+1] : (a[m][n/2]+a[m][n/2+1])/2; printf "%s\tmedian=%d ms\n", m, med}}' "$OUTDIR/timing.tsv"
