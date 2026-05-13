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
- This run predates `alignx convert --region`, so it used full-file conversion
  on a tiny BAM fixture.

## 2026-05-14 remote HG002 region-limited smoke

Environment:

- Host: `missmi-server00`
- Work directory:
  `/mypool/alignx/tmp/axf_smoke_hg002_chr20_10000000_10010000`
- Binary: `/mypool/alignx/bin/alignx`
- HTSlib runtime: `LD_LIBRARY_PATH=/home/happyntu/miniconda3/envs/hg002sv/lib`

Input:

- BAM:
  `/mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam`
- BAM size: about 112 GiB
- Region: `chr20:10000000-10010000`

Command shape:

```bash
/mypool/alignx/bin/smoke_axf_roundtrip.sh \
  --alignx /mypool/alignx/bin/alignx \
  --input /mypool/biotools-benchmark-data/hg002_downloads/HG002.SequelII.merged_15kb_20kb.pbmm2.GRCh38.haplotag.10x.bam \
  --region chr20:10000000-10010000 \
  --work-dir /mypool/alignx/tmp/axf_smoke_hg002_chr20_10000000_10010000
```

Result:

- Status: `OK axf_roundtrip`
- Records: 107
- BAM SAM stdout bytes: 3,133,962
- AXF SAM stdout bytes: 3,133,962
- `diff.txt`: empty
- Generated AXF size: about 3.0 MiB

Notes:

- This verifies `alignx convert --region` on a large indexed HG002 BAM without
  converting the full 112 GiB file.
- This is still a correctness smoke only: no timing repeats, no profiling, and
  no benchmark claims.
