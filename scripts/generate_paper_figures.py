#!/usr/bin/env python3
"""Generate benchmark figures for the AXF1 paper."""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

OUTPUT_DIR = Path(__file__).parent.parent / "docs" / "research" / "figures"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

plt.rcParams.update({
    'font.size': 9,
    'font.family': 'sans-serif',
    'axes.titlesize': 10,
    'axes.labelsize': 9,
    'xtick.labelsize': 8,
    'ytick.labelsize': 8,
    'legend.fontsize': 8,
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
})

COLORS = {
    'samtools': '#4472C4',
    'axf1': '#ED7D31',
    'axf1_filtered': '#A5A5A5',
    'cram': '#70AD47',
    'bam': '#4472C4',
    'axf1_zstd': '#FFC000',
    'axf1_lossy_zstd': '#5B9BD5',
    'illumina': '#9B59B6',
}


def fig2_view_latency():
    """Figure 2: Region query latency — PacBio + Illumina."""
    fig, axes = plt.subplots(1, 2, figsize=(7.5, 3.2), sharey=False)

    regions = ['chr1:1M-2M', 'chrY:20M-21M', 'chr1:121M-142M']
    x = np.arange(len(regions))
    width = 0.35

    # PacBio
    ax = axes[0]
    samtools = [236, 169, 3223]
    axf1 = [44, 35, 854]
    bars1 = ax.bar(x - width/2, samtools, width, label='samtools view', color=COLORS['samtools'])
    bars2 = ax.bar(x + width/2, axf1, width, label='AXF1 view', color=COLORS['axf1'])
    ax.set_yscale('log')
    ax.set_ylabel('Latency (ms, log scale)')
    ax.set_title('PacBio SequelII (10x)')
    ax.set_xticks(x)
    ax.set_xticklabels(regions, rotation=15, ha='right')
    ax.legend(loc='upper left')
    for bar, val in zip(bars2, axf1):
        ax.annotate(f'{val}ms', xy=(bar.get_x() + bar.get_width()/2, val),
                    xytext=(0, 3), textcoords='offset points', ha='center', fontsize=7)

    # Illumina
    ax = axes[1]
    samtools_ill = [438, 190, 6339]
    axf1_ill = [137, 64, 1642]
    bars1 = ax.bar(x - width/2, samtools_ill, width, label='samtools view -F4', color=COLORS['samtools'])
    bars2 = ax.bar(x + width/2, axf1_ill, width, label='AXF1 view', color=COLORS['axf1'])
    ax.set_yscale('log')
    ax.set_title('Illumina 2x250bp (300x)')
    ax.set_xticks(x)
    ax.set_xticklabels(regions, rotation=15, ha='right')
    ax.legend(loc='upper left')
    for bar, val in zip(bars2, axf1_ill):
        ax.annotate(f'{val}ms', xy=(bar.get_x() + bar.get_width()/2, val),
                    xytext=(0, 3), textcoords='offset points', ha='center', fontsize=7)

    fig.suptitle('Figure 2. Region Query Latency', fontsize=11, y=1.02)
    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig2_view_latency.png")
    fig.savefig(OUTPUT_DIR / "fig2_view_latency.pdf")
    plt.close(fig)
    print(f"  fig2_view_latency saved")


def fig3_file_size():
    """Figure 3: File size relative to BAM."""
    fig, ax = plt.subplots(figsize=(6, 3.5))

    configs = ['BAM', 'CRAM', 'AXF1\nlossless', 'AXF1\n+zstd\n(per-col)', 'AXF1\n+zstd\n(qual)', 'AXF1\nlossy+zstd']
    # chr1:1M-2M ratios
    chr1 = [1.00, 0.65, 1.58, 1.02, 1.22, 0.52]
    chrY = [1.00, 0.63, 1.84, np.nan, 1.33, 0.63]
    centro = [1.00, 0.77, 2.27, np.nan, 1.74, 0.96]

    x = np.arange(len(configs))
    width = 0.25

    ax.bar(x - width, chr1, width, label='chr1:1M-2M', color=COLORS['samtools'])
    ax.bar(x, chrY, width, label='chrY:20M-21M', color=COLORS['axf1'])
    ax.bar(x + width, centro, width, label='chr1:121M-142M', color=COLORS['cram'])

    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=0.8, alpha=0.5)
    ax.set_ylabel('Size relative to BAM')
    ax.set_xticks(x)
    ax.set_xticklabels(configs)
    ax.legend(loc='upper right')
    ax.set_ylim(0, 2.5)
    ax.set_title('Figure 3. File Size (ratio vs BAM)')

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig3_file_size.png")
    fig.savefig(OUTPUT_DIR / "fig3_file_size.pdf")
    plt.close(fig)
    print(f"  fig3_file_size saved")


def fig4_decode_comparison():
    """Figure 4: Decode latency — BAM vs CRAM vs AXF1."""
    fig, ax = plt.subplots(figsize=(5.5, 3.2))

    regions = ['chr1:1M-2M', 'chrY:20M-21M', 'chr1:121M-142M']
    x = np.arange(len(regions))
    width = 0.25

    bam = [236, 169, 3223]
    cram = [673, 376, 11411]
    axf1 = [44, 35, 854]

    ax.bar(x - width, bam, width, label='BAM (samtools)', color=COLORS['bam'])
    ax.bar(x, cram, width, label='CRAM', color=COLORS['cram'])
    ax.bar(x + width, axf1, width, label='AXF1', color=COLORS['axf1'])

    ax.set_yscale('log')
    ax.set_ylabel('Decode latency (ms, log scale)')
    ax.set_xticks(x)
    ax.set_xticklabels(regions)
    ax.legend(loc='upper left')
    ax.set_title('Figure 4. Full-Record Decode Comparison')

    for i, val in enumerate(axf1):
        ax.annotate(f'{val}ms', xy=(x[i] + width, val),
                    xytext=(0, 3), textcoords='offset points', ha='center', fontsize=7)

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig4_decode_comparison.png")
    fig.savefig(OUTPUT_DIR / "fig4_decode_comparison.pdf")
    plt.close(fig)
    print(f"  fig4_decode_comparison saved")


def fig_s1_pileup_comparison():
    """Figure S1: Pileup latency — PacBio vs Illumina."""
    fig, axes = plt.subplots(1, 2, figsize=(7, 3.2))

    regions = ['chr1:1M-2M', 'chrY:20M-21M', 'chr1:121M-142M']
    x = np.arange(len(regions))
    width = 0.35

    # PacBio (streaming pileup: skip-zero + buffered writer)
    ax = axes[0]
    samtools_pb = [279, 220, 4792]
    axf1_pb = [78, 74, 1177]
    ax.bar(x - width/2, samtools_pb, width, label='samtools depth', color=COLORS['samtools'])
    ax.bar(x + width/2, axf1_pb, width, label='AXF1 pileup', color=COLORS['axf1'])
    ax.set_yscale('log')
    ax.set_ylabel('Latency (ms, log scale)')
    ax.set_title('PacBio (10x)')
    ax.set_xticks(x)
    ax.set_xticklabels(regions, rotation=15, ha='right')
    ax.legend(loc='upper left')

    # Illumina
    ax = axes[1]
    samtools_ill = [372, 230, 4955]
    axf1_ill = [305, 249, 5337]
    ax.bar(x - width/2, samtools_ill, width, label='samtools depth', color=COLORS['samtools'])
    ax.bar(x + width/2, axf1_ill, width, label='AXF1 pileup', color=COLORS['axf1'])
    ax.set_yscale('log')
    ax.set_title('Illumina (300x)')
    ax.set_xticks(x)
    ax.set_xticklabels(regions, rotation=15, ha='right')
    ax.legend(loc='upper left')

    fig.suptitle('Figure S1. Pileup Latency: PacBio vs Illumina', fontsize=11, y=1.02)
    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig_s1_pileup_comparison.png")
    fig.savefig(OUTPUT_DIR / "fig_s1_pileup_comparison.pdf")
    plt.close(fig)
    print(f"  fig_s1_pileup_comparison saved")


def fig_s2_encode_time():
    """Figure S2: Encode time comparison."""
    fig, ax = plt.subplots(figsize=(6, 3.5))

    configs = ['BAM', 'CRAM', 'AXF1\nlossless', 'AXF1\nlossy', 'AXF1\nlossy+zstd']
    # chr1:1M-2M (ms)
    chr1 = [1503, 14072, 1768, 1074, 1404]
    chrY = [988, 13478, 1085, 694, 884]
    centro = [29656, 31897, 31678, 18983, 24896]

    x = np.arange(len(configs))
    width = 0.25

    ax.bar(x - width, chr1, width, label='chr1:1M-2M', color=COLORS['samtools'])
    ax.bar(x, chrY, width, label='chrY:20M-21M', color=COLORS['axf1'])
    ax.bar(x + width, centro, width, label='chr1:121M-142M', color=COLORS['cram'])

    ax.set_yscale('log')
    ax.set_ylabel('Encode time (ms, log scale)')
    ax.set_xticks(x)
    ax.set_xticklabels(configs)
    ax.legend(loc='upper right')
    ax.set_title('Figure S2. Encode Time')

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig_s2_encode_time.png")
    fig.savefig(OUTPUT_DIR / "fig_s2_encode_time.pdf")
    plt.close(fig)
    print(f"  fig_s2_encode_time saved")


def fig_s3_cross_platform_speedup():
    """Figure S3: Cross-platform speedup summary."""
    fig, ax = plt.subplots(figsize=(5, 3.5))

    categories = ['View\n(1 Mb)', 'View\n(21 Mb)', 'Pileup\n(1 Mb)', 'Pileup\n(21 Mb)']
    x = np.arange(len(categories))
    width = 0.35

    pacbio = [5.1, 3.8, 3.3, 4.1]  # midpoint of ranges; pileup updated for streaming
    illumina = [3.1, 3.9, 1.06, 0.93]

    ax.bar(x - width/2, pacbio, width, label='PacBio 10x', color=COLORS['axf1'])
    ax.bar(x + width/2, illumina, width, label='Illumina 300x', color=COLORS['illumina'])

    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=0.8, alpha=0.5)
    ax.set_ylabel('Speedup vs samtools')
    ax.set_xticks(x)
    ax.set_xticklabels(categories)
    ax.legend(loc='upper right')
    ax.set_title('Figure S3. Cross-Platform Speedup')
    ax.set_ylim(0, 6.5)

    plt.tight_layout()
    fig.savefig(OUTPUT_DIR / "fig_s3_cross_platform_speedup.png")
    fig.savefig(OUTPUT_DIR / "fig_s3_cross_platform_speedup.pdf")
    plt.close(fig)
    print(f"  fig_s3_cross_platform_speedup saved")


if __name__ == '__main__':
    print("Generating paper figures...")
    fig2_view_latency()
    fig3_file_size()
    fig4_decode_comparison()
    fig_s1_pileup_comparison()
    fig_s2_encode_time()
    fig_s3_cross_platform_speedup()
    print(f"All figures saved to {OUTPUT_DIR}")
