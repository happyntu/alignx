#include "query/axf_view.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <ostream>
#include <string_view>
#include <vector>

#include "format/axf_file.hpp"

namespace alignx::query {
namespace {

struct ParsedRegion {
    std::string reference;
    std::int32_t start = 0;
    std::int32_t end = 0;
};

std::expected<std::int32_t, std::string> parse_positive_i32(std::string_view text,
                                                            std::string_view label) {
    if (text.empty()) {
        return std::unexpected("missing " + std::string(label));
    }

    std::int64_t value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return std::unexpected("invalid " + std::string(label));
        }
        value = value * 10 + (ch - '0');
        if (value > std::numeric_limits<std::int32_t>::max()) {
            return std::unexpected(std::string(label) + " is too large");
        }
    }
    if (value <= 0) {
        return std::unexpected(std::string(label) + " must be positive");
    }
    return static_cast<std::int32_t>(value);
}

std::expected<ParsedRegion, std::string> parse_region(std::string_view region) {
    const std::size_t colon = region.find(':');
    const std::size_t dash = region.find('-', colon == std::string_view::npos ? 0 : colon + 1);
    if (colon == std::string_view::npos || dash == std::string_view::npos || colon == 0 ||
        dash <= colon + 1 || dash + 1 >= region.size()) {
        return std::unexpected("region must use ref:start-end");
    }

    auto one_based_start = parse_positive_i32(region.substr(colon + 1, dash - colon - 1), "start");
    auto one_based_end = parse_positive_i32(region.substr(dash + 1), "end");
    if (!one_based_start) {
        return std::unexpected(one_based_start.error());
    }
    if (!one_based_end) {
        return std::unexpected(one_based_end.error());
    }
    if (*one_based_start > *one_based_end) {
        return std::unexpected("region start must be <= end");
    }

    return ParsedRegion{.reference = std::string(region.substr(0, colon)),
                        .start = *one_based_start - 1,
                        .end = *one_based_end};
}

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

std::expected<bool, std::string> sam_line_overlaps_region(std::string_view line,
                                                          const ParsedRegion& region) {
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

    auto one_based_pos = parse_positive_i32(fields[3], "SAM POS");
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

std::expected<std::uint32_t, std::string> find_reference_id(const format::AxfFile& axf,
                                                            std::string_view reference) {
    for (std::size_t index = 0; index < axf.references.size(); ++index) {
        if (axf.references[index].name == reference) {
            return static_cast<std::uint32_t>(index);
        }
    }
    return std::unexpected("reference not found in AXF: " + std::string(reference));
}

} // namespace

std::expected<void, std::string> write_axf_region_sam(const std::filesystem::path& input,
                                                      const std::string& region,
                                                      std::ostream& out) {
    auto parsed_region = parse_region(region);
    if (!parsed_region) {
        return std::unexpected(parsed_region.error());
    }

    auto axf = format::read_axf_file(input);
    if (!axf) {
        return std::unexpected(axf.error());
    }

    auto ref_id = find_reference_id(*axf, parsed_region->reference);
    if (!ref_id) {
        return std::unexpected(ref_id.error());
    }

    auto blocks = axf->query_blocks(*ref_id, parsed_region->start, parsed_region->end);
    if (!blocks) {
        return std::unexpected(blocks.error());
    }

    for (const format::AxfBlock* block : *blocks) {
        std::string_view payload(reinterpret_cast<const char*>(block->payload.data()),
                                 block->payload.size());
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
                out.write(line.data(), static_cast<std::streamsize>(line.size()));
                if (line.back() != '\n') {
                    out.put('\n');
                }
            }
        }
    }

    return {};
}

} // namespace alignx::query
