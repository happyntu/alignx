#include "convert/axf_to_bam.hpp"

#include <string>

#include "format/axf1_file.hpp"
#include "io/bam_writer.hpp"

namespace alignx::convert {

std::expected<void, std::string>
convert_axf1_to_bam(const std::filesystem::path& input_axf,
                    const std::filesystem::path& output_bam,
                    std::optional<int> hts_threads) {
    auto reader = format::Axf1FileReader::open(input_axf);
    if (!reader) {
        return std::unexpected(reader.error());
    }

    const auto& file_index = reader->index();

    auto writer = io::BamWriter::open(output_bam, file_index.references, hts_threads);
    if (!writer) {
        return std::unexpected(writer.error());
    }

    for (const auto& chunk_entry : file_index.chunks) {
        auto chunk = reader->read_chunk(chunk_entry);
        if (!chunk) {
            return std::unexpected(chunk.error());
        }

        const auto& ref_name = file_index.references.at(chunk_entry.ref_id).name;

        for (const auto& record : chunk->records) {
            auto sam_line = format::format_axf1_sam_record(record, ref_name);
            if (!sam_line.empty() && sam_line.back() == '\n') {
                sam_line.pop_back();
            }

            auto write_result = writer->write_sam_line(sam_line);
            if (!write_result) {
                return std::unexpected(write_result.error());
            }
        }
    }

    auto close_result = writer->close();
    if (!close_result) {
        return std::unexpected(close_result.error());
    }

    return {};
}

} // namespace alignx::convert
