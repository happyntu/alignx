#include "convert/bam_to_axf.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

#include "format/axf_file.hpp"
#include "io/bam_reader.hpp"

namespace alignx::convert {
namespace {

struct PendingBlock {
    bool has_records = false;
    std::int32_t start_pos = -1;
    std::int32_t end_pos = -1;
    std::uint32_t record_count = 0;
    std::vector<unsigned char> payload;
};

void append_line(PendingBlock& block, std::string_view line, const io::BamRecord& record) {
    if (!block.has_records) {
        block.start_pos = record.position;
        block.end_pos = record.end_position;
        block.has_records = true;
    } else {
        if (record.position < block.start_pos) {
            block.start_pos = record.position;
        }
        if (record.end_position > block.end_pos) {
            block.end_pos = record.end_position;
        }
    }

    block.record_count += 1;
    block.payload.insert(block.payload.end(), line.begin(), line.end());
    if (line.empty() || line.back() != '\n') {
        block.payload.push_back('\n');
    }
}

} // namespace

std::expected<void, std::string> convert_bam_to_axf_mvp(const std::filesystem::path& input_bam,
                                                        const std::filesystem::path& output_axf) {
    auto reader = io::BamReader::open(input_bam);
    if (!reader) {
        return std::unexpected(reader.error());
    }

    auto references = reader->references();
    if (!references) {
        return std::unexpected(references.error());
    }

    format::AxfFile file;
    file.references.reserve(references->size());
    for (const io::BamReference& reference : *references) {
        file.references.push_back({.name = reference.name, .length = reference.length});
    }

    std::vector<PendingBlock> blocks(references->size());
    for (;;) {
        auto record_view = reader->next_record_view();
        if (!record_view) {
            return std::unexpected(record_view.error());
        }
        if (!record_view->has_value()) {
            break;
        }
        const io::BamRecordView& view = **record_view;
        if (view.record.is_unmapped() || view.record.position < 0 ||
            view.record.end_position <= view.record.position) {
            continue;
        }

        std::size_t ref_id = references->size();
        for (std::size_t index = 0; index < references->size(); ++index) {
            if (references->at(index).name == view.record.reference_name) {
                ref_id = index;
                break;
            }
        }
        if (ref_id >= references->size()) {
            return std::unexpected("failed to map BAM record reference name to reference id");
        }
        append_line(blocks.at(ref_id), view.sam_line, view.record);
    }

    for (std::size_t ref_id = 0; ref_id < blocks.size(); ++ref_id) {
        const PendingBlock& block = blocks[ref_id];
        if (!block.has_records) {
            continue;
        }
        file.blocks.push_back({.ref_id = static_cast<std::uint32_t>(ref_id),
                               .start_pos = block.start_pos,
                               .end_pos = block.end_pos,
                               .record_count = block.record_count,
                               .payload = block.payload});
    }

    return format::write_axf_file(file, output_axf);
}

} // namespace alignx::convert
