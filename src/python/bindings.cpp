#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "format/axf1_file.hpp"
#include "query/axf1_view.hpp"
#include "query/axf1_coverage.hpp"
#include "query/record_filter.hpp"
#include "convert/bam_to_axf.hpp"
#include "convert/axf_to_bam.hpp"

#ifdef ALIGNX_HAVE_HTSLIB
#include "io/bam_reader.hpp"
#include "io/fasta_reader.hpp"
#endif

namespace py = pybind11;

namespace {

template <typename T>
T unwrap(std::expected<T, std::string>&& result) {
    if (!result) {
        throw std::runtime_error(result.error());
    }
    return std::move(*result);
}

void unwrap_void(std::expected<void, std::string>&& result) {
    if (!result) {
        throw std::runtime_error(result.error());
    }
}

} // namespace

PYBIND11_MODULE(_alignx, m) {
    m.doc() = "alignx: columnar alignment format for selective-column I/O";

    // ── Enums ────────────────────────────────────────────────────────────────

    using alignx::format::Axf1ColumnId;
    py::enum_<Axf1ColumnId>(m, "ColumnId")
        .value("qname", Axf1ColumnId::qname)
        .value("flag", Axf1ColumnId::flag)
        .value("pos", Axf1ColumnId::pos)
        .value("mapq", Axf1ColumnId::mapq)
        .value("cigar", Axf1ColumnId::cigar)
        .value("mate_reference", Axf1ColumnId::mate_reference)
        .value("mate_pos", Axf1ColumnId::mate_pos)
        .value("template_length", Axf1ColumnId::template_length)
        .value("sequence", Axf1ColumnId::sequence)
        .value("quality", Axf1ColumnId::quality)
        .value("tags", Axf1ColumnId::tags);

    using alignx::format::Axf1CodecId;
    py::enum_<Axf1CodecId>(m, "CodecId")
        .value("raw", Axf1CodecId::raw)
        .value("pos_delta_varint", Axf1CodecId::pos_delta_varint)
        .value("flag_bitpack", Axf1CodecId::flag_bitpack)
        .value("mapq_rle", Axf1CodecId::mapq_rle)
        .value("seq_2bit_literal", Axf1CodecId::seq_2bit_literal)
        .value("cigar_token", Axf1CodecId::cigar_token)
        .value("qual_rle", Axf1CodecId::qual_rle)
        .value("qual_pack", Axf1CodecId::qual_pack)
        .value("qual_pack_compressed", Axf1CodecId::qual_pack_compressed)
        .value("qname_dict", Axf1CodecId::qname_dict)
        .value("tags_per_stream", Axf1CodecId::tags_per_stream)
        .value("cigar_dict", Axf1CodecId::cigar_dict)
        .value("compressed", Axf1CodecId::compressed)
        .value("seq_ref_delta", Axf1CodecId::seq_ref_delta);

    using alignx::format::Axf1Compression;
    py::enum_<Axf1Compression>(m, "Compression")
        .value("none", Axf1Compression::none)
        .value("zstd", Axf1Compression::zstd);

    using alignx::format::Axf1QualityLossy;
    py::enum_<Axf1QualityLossy>(m, "QualityLossy")
        .value("none", Axf1QualityLossy::none)
        .value("illumina8", Axf1QualityLossy::illumina8);

    // ── Structs (read-only) ──────────────────────────────────────────────────

    using alignx::format::Axf1Record;
    py::class_<Axf1Record>(m, "Record")
        .def_readonly("qname", &Axf1Record::qname)
        .def_readonly("flag", &Axf1Record::flag)
        .def_readonly("pos", &Axf1Record::pos)
        .def_readonly("mapq", &Axf1Record::mapq)
        .def_readonly("cigar", &Axf1Record::cigar)
        .def_readonly("mate_reference", &Axf1Record::mate_reference)
        .def_readonly("mate_pos", &Axf1Record::mate_pos)
        .def_readonly("template_length", &Axf1Record::template_length)
        .def_readonly("sequence", &Axf1Record::sequence)
        .def_readonly("quality", &Axf1Record::quality)
        .def_readonly("tags", &Axf1Record::tags)
        .def("__repr__", [](const Axf1Record& r) {
            return "<alignx.Record qname='" + r.qname + "' pos=" + std::to_string(r.pos) +
                   " flag=" + std::to_string(r.flag) + ">";
        });

    using alignx::format::Axf1Reference;
    py::class_<Axf1Reference>(m, "Reference")
        .def_readonly("name", &Axf1Reference::name)
        .def_readonly("length", &Axf1Reference::length)
        .def("__repr__", [](const Axf1Reference& r) {
            return "<alignx.Reference '" + r.name + "' length=" + std::to_string(r.length) + ">";
        });

    using alignx::format::Axf1Chunk;
    py::class_<Axf1Chunk>(m, "Chunk")
        .def_readonly("ref_id", &Axf1Chunk::ref_id)
        .def_readonly("start_pos", &Axf1Chunk::start_pos)
        .def_readonly("end_pos", &Axf1Chunk::end_pos)
        .def_readonly("records", &Axf1Chunk::records)
        .def("column_array", [](const Axf1Chunk& chunk, const std::string& col) -> py::object {
            const auto& recs = chunk.records;
            const auto n = static_cast<py::ssize_t>(recs.size());
            if (col == "pos") {
                auto arr = py::array_t<std::int32_t>(n);
                auto ptr = arr.mutable_data();
                for (py::ssize_t i = 0; i < n; ++i) ptr[i] = recs[static_cast<std::size_t>(i)].pos;
                return std::move(arr);
            } else if (col == "flag") {
                auto arr = py::array_t<std::uint16_t>(n);
                auto ptr = arr.mutable_data();
                for (py::ssize_t i = 0; i < n; ++i) ptr[i] = recs[static_cast<std::size_t>(i)].flag;
                return std::move(arr);
            } else if (col == "mapq") {
                auto arr = py::array_t<std::uint8_t>(n);
                auto ptr = arr.mutable_data();
                for (py::ssize_t i = 0; i < n; ++i) ptr[i] = recs[static_cast<std::size_t>(i)].mapq;
                return std::move(arr);
            } else if (col == "mate_pos") {
                auto arr = py::array_t<std::int32_t>(n);
                auto ptr = arr.mutable_data();
                for (py::ssize_t i = 0; i < n; ++i) ptr[i] = recs[static_cast<std::size_t>(i)].mate_pos;
                return std::move(arr);
            } else if (col == "template_length") {
                auto arr = py::array_t<std::int32_t>(n);
                auto ptr = arr.mutable_data();
                for (py::ssize_t i = 0; i < n; ++i) ptr[i] = recs[static_cast<std::size_t>(i)].template_length;
                return std::move(arr);
            }
            throw std::invalid_argument(
                "column_array supports: pos, flag, mapq, mate_pos, template_length");
        }, py::arg("column"), "Extract a numeric column as a numpy array.")
        .def("__repr__", [](const Axf1Chunk& c) {
            return "<alignx.Chunk ref_id=" + std::to_string(c.ref_id) +
                   " range=[" + std::to_string(c.start_pos) + "," + std::to_string(c.end_pos) +
                   ") records=" + std::to_string(c.records.size()) + ">";
        });

    using alignx::format::Axf1ChunkIndexEntry;
    py::class_<Axf1ChunkIndexEntry>(m, "ChunkIndex")
        .def_readonly("ref_id", &Axf1ChunkIndexEntry::ref_id)
        .def_readonly("start_pos", &Axf1ChunkIndexEntry::start_pos)
        .def_readonly("end_pos", &Axf1ChunkIndexEntry::end_pos)
        .def_readonly("record_count", &Axf1ChunkIndexEntry::record_count)
        .def_readonly("chunk_offset", &Axf1ChunkIndexEntry::chunk_offset)
        .def_readonly("chunk_length", &Axf1ChunkIndexEntry::chunk_length)
        .def("overlaps", &Axf1ChunkIndexEntry::overlaps, py::arg("start"), py::arg("end"))
        .def("__repr__", [](const Axf1ChunkIndexEntry& e) {
            return "<alignx.ChunkIndex ref_id=" + std::to_string(e.ref_id) +
                   " range=[" + std::to_string(e.start_pos) + "," + std::to_string(e.end_pos) +
                   ") records=" + std::to_string(e.record_count) + ">";
        });

    using alignx::format::Axf1FileMetadata;
    using alignx::format::Axf1FileIndexMetadata;
    py::class_<Axf1FileIndexMetadata>(m, "FileMetadata")
        .def_readonly("source_path", &Axf1FileIndexMetadata::source_path)
        .def_readonly("conversion_region", &Axf1FileIndexMetadata::conversion_region)
        .def_readonly("is_subset", &Axf1FileIndexMetadata::is_subset)
        .def("__repr__", [](const Axf1FileIndexMetadata& m) {
            std::string repr = "<alignx.FileMetadata";
            if (!m.source_path.empty())
                repr += " source='" + m.source_path + "'";
            if (!m.conversion_region.empty())
                repr += " region='" + m.conversion_region + "'";
            if (m.is_subset)
                repr += " subset=true";
            repr += ">";
            return repr;
        });

    using alignx::format::Axf1FileIndex;
    py::class_<Axf1FileIndex>(m, "FileIndex")
        .def_readonly("metadata", &Axf1FileIndex::metadata)
        .def_readonly("references", &Axf1FileIndex::references)
        .def_readonly("chunks", &Axf1FileIndex::chunks)
        .def("query_chunks", [](const Axf1FileIndex& idx, std::uint32_t ref_id,
                                std::int32_t start, std::int32_t end) {
            auto result = idx.query_chunks(ref_id, start, end);
            if (!result) {
                throw std::runtime_error(result.error());
            }
            std::vector<Axf1ChunkIndexEntry> entries;
            entries.reserve(result->size());
            for (const auto* ptr : *result) {
                entries.push_back(*ptr);
            }
            return entries;
        }, py::arg("ref_id"), py::arg("start"), py::arg("end"))
        .def("__repr__", [](const Axf1FileIndex& idx) {
            return "<alignx.FileIndex refs=" + std::to_string(idx.references.size()) +
                   " chunks=" + std::to_string(idx.chunks.size()) + ">";
        });

    using alignx::format::Axf1ChunkReadProfile;
    py::class_<Axf1ChunkReadProfile>(m, "ChunkReadProfile")
        .def_readonly("bytes_read", &Axf1ChunkReadProfile::bytes_read)
        .def_readonly("total_payload_bytes", &Axf1ChunkReadProfile::total_payload_bytes)
        .def_readonly("selected_payload_bytes", &Axf1ChunkReadProfile::selected_payload_bytes)
        .def_readonly("total_columns", &Axf1ChunkReadProfile::total_columns)
        .def_readonly("selected_columns", &Axf1ChunkReadProfile::selected_columns);

    // ── Structs (read-write, for options) ────────────────────────────────────

    using alignx::format::Axf1WriteOptions;
    py::class_<Axf1WriteOptions>(m, "WriteOptions")
        .def(py::init<>())
        .def_readwrite("quality_compression", &Axf1WriteOptions::quality_compression)
        .def_readwrite("column_compression", &Axf1WriteOptions::column_compression)
        .def_readwrite("quality_lossy", &Axf1WriteOptions::quality_lossy)
        .def_property("reference_fasta",
            [](const Axf1WriteOptions& o) -> std::optional<std::string> {
                if (o.reference_fasta) return o.reference_fasta->string();
                return std::nullopt;
            },
            [](Axf1WriteOptions& o, const std::optional<std::string>& v) {
                if (v) o.reference_fasta = std::filesystem::path(*v);
                else o.reference_fasta = std::nullopt;
            });

    using alignx::query::RecordFilter;
    py::class_<RecordFilter>(m, "RecordFilter")
        .def(py::init<>())
        .def_readwrite("flag_exclude", &RecordFilter::flag_exclude)
        .def_readwrite("min_mapq", &RecordFilter::min_mapq)
        .def("is_active", &RecordFilter::is_active);

    using alignx::analysis::CoverageResult;
    py::class_<CoverageResult>(m, "CoverageResult", py::buffer_protocol())
        .def_readonly("reference", &CoverageResult::reference)
        .def_readonly("start", &CoverageResult::start)
        .def_readonly("end", &CoverageResult::end)
        .def_property_readonly("depth", [](CoverageResult& c) {
            return py::array_t<std::uint32_t>(
                {static_cast<py::ssize_t>(c.depth.size())},
                {sizeof(std::uint32_t)},
                c.depth.data(),
                py::cast(c));
        })
        .def_readonly("records_counted", &CoverageResult::records_counted)
        .def_buffer([](CoverageResult& c) -> py::buffer_info {
            return py::buffer_info(
                c.depth.data(), sizeof(std::uint32_t),
                py::format_descriptor<std::uint32_t>::format(),
                1, {static_cast<py::ssize_t>(c.depth.size())},
                {sizeof(std::uint32_t)});
        })
        .def("__repr__", [](const CoverageResult& c) {
            return "<alignx.CoverageResult ref='" + c.reference +
                   "' range=[" + std::to_string(c.start) + "," + std::to_string(c.end) +
                   ") records=" + std::to_string(c.records_counted) + ">";
        })
        .def("__len__", [](const CoverageResult& c) { return c.depth.size(); });

    // ── Axf1FileReader ───────────────────────────────────────────────────────

    using alignx::format::Axf1FileReader;
    py::class_<Axf1FileReader>(m, "Axf1FileReader")
        .def_property_readonly("index", &Axf1FileReader::index,
                               py::return_value_policy::reference_internal)
        .def("query_chunks", [](Axf1FileReader& reader, std::uint32_t ref_id,
                                std::int32_t start, std::int32_t end) {
            auto result = reader.query_chunks(ref_id, start, end);
            if (!result) {
                throw std::runtime_error(result.error());
            }
            std::vector<Axf1ChunkIndexEntry> entries;
            entries.reserve(result->size());
            for (const auto* ptr : *result) {
                entries.push_back(*ptr);
            }
            return entries;
        }, py::arg("ref_id"), py::arg("start"), py::arg("end"))
        .def("read_chunk", [](Axf1FileReader& reader, const Axf1ChunkIndexEntry& chunk) {
            return unwrap(reader.read_chunk(chunk));
        }, py::arg("chunk"))
        .def("read_chunk_columns", [](Axf1FileReader& reader, const Axf1ChunkIndexEntry& chunk,
                                      const std::vector<Axf1ColumnId>& columns) {
            return unwrap(reader.read_chunk_columns(chunk, columns));
        }, py::arg("chunk"), py::arg("columns"))
        .def("__repr__", [](const Axf1FileReader& reader) {
            const auto& idx = reader.index();
            return "<alignx.Axf1FileReader refs=" + std::to_string(idx.references.size()) +
                   " chunks=" + std::to_string(idx.chunks.size()) +
                   " size=" + std::to_string(reader.file_size()) + ">";
        });

    m.def("open", [](const std::string& path) {
        return unwrap(Axf1FileReader::open(std::filesystem::path(path)));
    }, py::arg("path"), "Open an AXF1 file for reading.");

    // ── High-level convenience functions ─────────────────────────────────────

    m.def("view", [](const std::string& input, const std::string& region,
                     std::optional<std::string> reference,
                     std::uint16_t flag_exclude, std::uint8_t min_mapq) {
        std::ostringstream oss;
        RecordFilter filter;
        filter.flag_exclude = flag_exclude;
        filter.min_mapq = min_mapq;
        std::optional<std::filesystem::path> ref_path;
        if (reference) ref_path = std::filesystem::path(*reference);
        unwrap_void(alignx::query::write_axf1_region_sam(
            std::filesystem::path(input), region, oss, filter, ref_path));
        return oss.str();
    }, py::arg("input"), py::arg("region"),
       py::arg("reference") = py::none(),
       py::arg("flag_exclude") = 0,
       py::arg("min_mapq") = 0,
       "Query a region from an AXF1 file and return SAM text.");

    m.def("coverage", [](const std::string& input, const std::string& region,
                         std::uint16_t flag_exclude, std::uint8_t min_mapq) {
        RecordFilter filter;
        filter.flag_exclude = flag_exclude;
        filter.min_mapq = min_mapq;
        return unwrap(alignx::query::compute_axf1_coverage(
            std::filesystem::path(input), region, filter));
    }, py::arg("input"), py::arg("region"),
       py::arg("flag_exclude") = 0,
       py::arg("min_mapq") = 0,
       "Compute per-base coverage for a region from an AXF1 file.");

    m.def("convert", [](const std::string& input, const std::string& output,
                        std::optional<std::string> region,
                        const std::string& compression,
                        const std::string& quality_lossy,
                        std::optional<std::string> reference) {
        Axf1WriteOptions options;
        if (compression == "zstd") {
            options.column_compression = Axf1Compression::zstd;
            options.quality_compression = Axf1Compression::zstd;
        } else if (compression != "none" && !compression.empty()) {
            throw std::invalid_argument("unsupported compression: " + compression);
        }
        if (quality_lossy == "illumina8") {
            options.quality_lossy = Axf1QualityLossy::illumina8;
        } else if (quality_lossy != "none" && !quality_lossy.empty()) {
            throw std::invalid_argument("unsupported quality_lossy: " + quality_lossy);
        }
        if (reference) options.reference_fasta = std::filesystem::path(*reference);
        unwrap_void(alignx::convert::convert_bam_to_axf1_mvp(
            std::filesystem::path(input), std::filesystem::path(output), region, options));
    }, py::arg("input"), py::arg("output"),
       py::arg("region") = py::none(),
       py::arg("compression") = "none",
       py::arg("quality_lossy") = "none",
       py::arg("reference") = py::none(),
       "Convert a BAM/CRAM file to AXF1 format.");

    m.def("export_bam", [](const std::string& input, const std::string& output,
                           std::optional<int> hts_threads) {
        unwrap_void(alignx::convert::convert_axf1_to_bam(
            std::filesystem::path(input), std::filesystem::path(output), hts_threads));
    }, py::arg("input"), py::arg("output"),
       py::arg("hts_threads") = py::none(),
       "Export an AXF1 file to BAM format.");

    // ── BamReader ────────────────────────────────────────────────────────────

#ifdef ALIGNX_HAVE_HTSLIB
    using alignx::io::BamRecord;
    py::class_<BamRecord>(m, "BamRecord")
        .def_readonly("qname", &BamRecord::qname)
        .def_readonly("reference_name", &BamRecord::reference_name)
        .def_readonly("position", &BamRecord::position)
        .def_readonly("end_position", &BamRecord::end_position)
        .def_readonly("template_length", &BamRecord::template_length)
        .def_readonly("flag", &BamRecord::flag)
        .def_readonly("mapq", &BamRecord::mapq)
        .def("is_unmapped", &BamRecord::is_unmapped)
        .def("__repr__", [](const BamRecord& r) {
            return "<alignx.BamRecord qname='" + r.qname +
                   "' pos=" + std::to_string(r.position) +
                   " ref='" + r.reference_name + "'>";
        });

    using alignx::io::BamReference;
    py::class_<BamReference>(m, "BamReference")
        .def_readonly("name", &BamReference::name)
        .def_readonly("length", &BamReference::length);

    using alignx::io::BamReader;
    py::class_<BamReader>(m, "BamReader")
        .def_static("open", [](const std::string& path, std::optional<int> threads) {
            return unwrap(BamReader::open(std::filesystem::path(path), threads));
        }, py::arg("path"), py::arg("hts_threads") = py::none())
        .def("fetch", [](BamReader& reader, const std::string& region) {
            unwrap_void(reader.fetch(region));
        }, py::arg("region"))
        .def("next_record", [](BamReader& reader) -> std::optional<BamRecord> {
            auto result = reader.next_record();
            if (!result) throw std::runtime_error(result.error());
            return std::move(*result);
        })
        .def("references", [](const BamReader& reader) {
            return unwrap(reader.references());
        })
        .def("has_index", &BamReader::has_index)
        .def("__iter__", [](BamReader& reader) -> BamReader& { return reader; })
        .def("__next__", [](BamReader& reader) {
            auto result = reader.next_record();
            if (!result) throw std::runtime_error(result.error());
            if (!result->has_value()) throw py::stop_iteration();
            return std::move(**result);
        })
        .def("__enter__", [](BamReader& reader) -> BamReader& { return reader; })
        .def("__exit__", [](BamReader&, py::object, py::object, py::object) {})
        .def("__repr__", [](const BamReader& reader) {
            return "<alignx.BamReader refs=" + std::to_string(reader.reference_count()) +
                   " indexed=" + (reader.has_index() ? "true" : "false") + ">";
        });

    using alignx::io::FastaContig;
    py::class_<FastaContig>(m, "FastaContig")
        .def_readonly("name", &FastaContig::name)
        .def_readonly("length", &FastaContig::length);

    using alignx::io::FastaReader;
    py::class_<FastaReader>(m, "FastaReader")
        .def_static("open", [](const std::string& path) {
            return unwrap(FastaReader::open(std::filesystem::path(path)));
        }, py::arg("path"))
        .def("contigs", [](const FastaReader& reader) {
            return unwrap(reader.contigs());
        })
        .def("fetch_sequence", [](const FastaReader& reader, const std::string& contig,
                                  std::int32_t start, std::int32_t end) {
            return unwrap(reader.fetch_sequence(contig, start, end));
        }, py::arg("contig"), py::arg("start"), py::arg("end"))
        .def("fetch_contig", [](const FastaReader& reader, const std::string& contig) {
            return unwrap(reader.fetch_contig(contig));
        }, py::arg("contig"))
        .def("compute_contig_sha256", [](const FastaReader& reader, const std::string& contig) {
            auto result = reader.compute_contig_sha256(contig);
            if (!result) throw std::runtime_error(result.error());
            return py::bytes(reinterpret_cast<const char*>(result->data()), result->size());
        }, py::arg("contig"))
        .def("__enter__", [](FastaReader& reader) -> FastaReader& { return reader; })
        .def("__exit__", [](FastaReader&, py::object, py::object, py::object) {})
        .def("__repr__", [](const FastaReader& reader) {
            return std::string("<alignx.FastaReader>");
        });
#else
    m.def("_check_htslib", []() {
        throw std::runtime_error("alignx built without htslib; BamReader and FastaReader unavailable");
    });
#endif

    m.attr("__version__") = "0.1.0";
    m.attr("has_htslib") =
#ifdef ALIGNX_HAVE_HTSLIB
        true;
#else
        false;
#endif
    m.attr("has_zstd") =
#ifdef ALIGNX_HAVE_ZSTD
        true;
#else
        false;
#endif
    m.attr("has_avx2") =
#ifdef ALIGNX_HAVE_AVX2
        true;
#else
        false;
#endif
}
