# AXF MVP Correctness Smoke Results

These are correctness smoke checks for the AXF0 MVP path. They are not
benchmarks, do not use timing repeats, and should not be used for performance
claims.

## 2026-05-14 remote small-BAM smoke

Environment:

- Host: `missmi-server00`
- Work directory: `/mypool/alignx/tmp/axf_smoke_sv_sniffles_manual`
- Binary: `/mypool/alignx/bin/alignx`
- HTSlib runtime: `LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib`

Input:

- BAM:
  `/mypool/biotools-benchmark-data/run_workspace/biotools-benchmark/tests/fixtures/sv_sniffles_reads.bam`
- Region: `chr1:1-2000`

Command shape:

```bash
/mypool/alignx/bin/smoke_axf_roundtrip.sh \
  --alignx /mypool/alignx/bin/alignx \
  --input /mypool/biotools-benchmark-data/run_workspace/biotools-benchmark/tests/fixtures/sv_sniffles_reads.bam \
  --region chr1:1-2000 \
  --work-dir /mypool/alignx/tmp/axf_smoke_sv_sniffles_manual
```

Result:

- Status: `OK axf_roundtrip`
- Records: 1
- BAM SAM stdout bytes: 2041
- AXF SAM stdout bytes: 2041
- `diff.txt`: empty
- Generated AXF size: about 2.1 KiB

Notes:

- This verifies the remote `BAM -> AXF0 -> view` correctness path on a small
  indexed BAM fixture.
- The current `alignx convert` implementation converts the whole BAM, so large
  BAMs such as the 112 GiB HG002 PacBio BAM should not be used for this smoke
  until region-limited or chunked conversion is available.
