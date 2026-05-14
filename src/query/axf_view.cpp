#include "query/axf_view.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "format/axf_file.hpp"
#include "query/region.hpp"

namespace alignx::query {
namespace {

std::vector<std::string_view> split_tab_fields(std::string_view line, std::size_t max_fields) {
    std::vector<std::string_view> fields;
    fields.reserve(max_fields);

    std::size_t begin = 0;
    while (fields.size() + 1 < max_fields) {
        const std::size_t tab = line.find('\t', begin);
        if (tab == std::string_view::npos) {
            break;
        }
        fields.push_back(line.substr(begin, tab - begin));
        begin = tab + 1;
    }
    fields.push_back(line.substr(begin));
    return fields;
}

std::expected<std::int32_t, std::string> cigar_reference_span(std::string_view cigar) {
    if (cigar.empty() || cigar == "*") {
        return 1;
    }

    std::int64_t span = 0;
    std::int64_t length = 0;
    bool saw_op = false;
    for (char ch : cigar) {
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            length = length * 10 + (ch - '0');
            if (length > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR operation length is too large");
            }
            continue;
        }

        if (length == 0) {
            return std::unexpected("invalid CIGAR string");
        }
        switch (ch) {
        case 'M':
        case 'D':
        case 'N':
        case '=':
        case 'X':
            span += length;
            if (span > std::numeric_limits<std::int32_t>::max()) {
                return std::unexpected("CIGAR reference span is too large");
            }
            break;
        case 'I':
        case 'S':
        case 'H':
        case 'P':
            break;
        default:
            return std::unexpected("invalid CIGAR operation");
        }
        length = 0;
        saw_op = true;
    }
    if (!saw_op || length != 0) {
        return std::unexpected("invalid CIGAR string");
    }
    return static_cast<std::int32_t>(std::max<std::int64_t>(span, 1));
}

std::expected<std::int32_t, std::string> parse_sam_pos(std::string_view text) {
    if (text.empty()) {
        return std::unexpected("missing SAM POS");
    }

    std::int64_t value = 0;
    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return std::unexpected("invalid SAM POS");
        }
        value = value * 10 + (ch - '0');
        if (value > std::numeric_limits<std::int32_t>::max()) {
            return std::unexpected("SAM POS is too large");
        }
    }
    if (value <= 0) {
        return std::unexpected("SAM POS must be positive");
    }
    return static_cast<std::int32_t>(value);
}

std::expected<bool, std::string> sam_line_overlaps_region(std::string_view line,
                                                          const SamRegion& region) {
    if (!line.empty() && line.back() == '\n') {
        line.remove_suffix(1);
    }

    const auto fields = split_tab_fields(line, 7);
    if (fields.size() < 6) {
        return std::unexpected("AXF payload contains malformed SAM line");
    }
    if (fields[2] != region.reference) {
        return false;
    }

    auto one_based_pos = parse_sam_pos(fields[3]);
    if (!one_based_pos) {
        return std::unexpected(one_based_pos.error());
    }
    auto span = cigar_reference_span(fields[5]);
    if (!span) {
        return std::unexpected(span.error());
    }

    const std::int32_t start = *one_based_pos - 1;
    const std::int32_t end = start + *span;
    return start < region.end && region.start < end;
}

std::expected<std::uint32_t, std::string>
find_reference_id(const std::vector<format::AxfReference>& references, std::string_view reference) {
    for (std::size_t index = 0; index < references.size(); ++index) {
        if (references[index].name == reference) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::unexpected("reference not found in AXF: " + std::string(reference));
}

} // namespace

std::expected<void, std::string> write_axf_region_sam(const std::filesystem::path& input,
                                                      const std::string& region,
                                                      std::ostream& out) {
    auto parsed_region = parse_sam_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    auto axf = format::read_axf_index_metadata(input);
    if (!axf) {
        return std::unexpected(axf.error());
    }

    auto ref_id = find_reference_id(axf->references, parsed_region->reference);
    if (!ref_id) {
        return std::unexpected(ref_id.error());
    }

    auto blocks = axf->query_blocks(*ref_id, parsed_region->start, parsed_region->end);
    if (!blocks) {
        return std::unexpected(blocks.error());
    }

    std::string output;
    for (const format::AxfBlockIndexEntry* block : *blocks) {
        auto block_payload = format::read_axf_block_payload(input, *block);
        if (!block_payload) {
            return std::unexpected(block_payload.error());
        }
        std::string_view payload(reinterpret_cast<const char*>(block_payload->data()),
                                 block_payload->size());
        while (!payload.empty()) {
            const std::size_t newline = payload.find('\n');
            const std::string_view line =
                newline == std::string_view::npos ? payload : payload.substr(0, newline + 1);
            payload = newline == std::string_view::npos ? std::string_view{}
                                                        : payload.substr(newline + 1);
            if (line.empty()) {
                continue;
            }

            auto overlaps = sam_line_overlaps_region(line, *parsed_region);
            if (!overlaps) {
                return std::unexpected(overlaps.error());
            }
            if (*overlaps) {
                output.append(line.data(), line.size());
                if (line.back() != '\n') {
                    output.push_back('\n');
                }
            }
        }
    }

    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    return {};
}

} // namespace alignx::query
