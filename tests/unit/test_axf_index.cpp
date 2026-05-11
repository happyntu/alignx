#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "index/axf_index.hpp"

namespace {

alignx::index::AxfIndex make_index() {
    alignx::index::AxfIndex index(2);
    index.add_interval(0,
                       {.start = 300, .end = 400, .chunk_offset = 30, .column_index_offset = 3000});
    index.add_interval(0,
                       {.start = 100, .end = 200, .chunk_offset = 10, .column_index_offset = 1000});
    index.add_interval(0,
                       {.start = 180, .end = 260, .chunk_offset = 20, .column_index_offset = 2000});
    index.add_interval(1,
                       {.start = 10, .end = 20, .chunk_offset = 40, .column_index_offset = 4000});
    auto validation = index.sort_and_validate();
    EXPECT_TRUE(validation) << validation.error();
    return index;
}

TEST(AxfIndex, SortsAndQueriesOverlaps) {
    auto index = make_index();

    const auto hits = index.query(0, 150, 220);
    ASSERT_TRUE(hits) << hits.error();

    ASSERT_EQ(hits->size(), 2);
    EXPECT_EQ(hits->at(0).chunk_offset, 10);
    EXPECT_EQ(hits->at(1).chunk_offset, 20);
}

TEST(AxfIndex, QueryReturnsNoOverlap) {
    auto index = make_index();

    const auto hits = index.query(0, 260, 300);
    ASSERT_TRUE(hits) << hits.error();

    EXPECT_TRUE(hits->empty());
}

TEST(AxfIndex, RejectsInvalidIntervalsAndQueries) {
    alignx::index::AxfIndex index(1);
    index.add_interval(0, {.start = 10, .end = 10, .chunk_offset = 1, .column_index_offset = 2});

    auto validation = index.sort_and_validate();
    EXPECT_FALSE(validation);
    EXPECT_NE(validation.error().find("start < end"), std::string::npos);

    auto query = index.query(2, 0, 1);
    EXPECT_FALSE(query);
    EXPECT_NE(query.error().find("out of range"), std::string::npos);

    query = index.query(0, 5, 5);
    EXPECT_FALSE(query);
    EXPECT_NE(query.error().find("start < end"), std::string::npos);
}

TEST(AxfIndex, WriteReadRoundTrip) {
    auto index = make_index();
    const auto path = std::filesystem::temp_directory_path() / "alignx_roundtrip.axf.idx";

    auto write = alignx::index::write_axf_index(index, path);
    ASSERT_TRUE(write) << write.error();

    auto read = alignx::index::read_axf_index(path);
    ASSERT_TRUE(read) << read.error();

    ASSERT_EQ(read->reference_count(), 2);
    ASSERT_EQ(read->intervals(0).size(), 3);
    ASSERT_EQ(read->intervals(1).size(), 1);

    const auto hits = read->query(0, 150, 220);
    ASSERT_TRUE(hits) << hits.error();
    ASSERT_EQ(hits->size(), 2);
    EXPECT_EQ(hits->at(0).start, 100);
    EXPECT_EQ(hits->at(0).end, 200);
    EXPECT_EQ(hits->at(0).chunk_offset, 10);
    EXPECT_EQ(hits->at(0).column_index_offset, 1000);
    EXPECT_EQ(hits->at(1).start, 180);
    EXPECT_EQ(hits->at(1).end, 260);
    EXPECT_EQ(hits->at(1).chunk_offset, 20);
    EXPECT_EQ(hits->at(1).column_index_offset, 2000);

    std::filesystem::remove(path);
}

TEST(AxfIndex, RejectsCorruptCrc) {
    auto index = make_index();
    const auto path = std::filesystem::temp_directory_path() / "alignx_corrupt.axf.idx";

    auto write = alignx::index::write_axf_index(index, path);
    ASSERT_TRUE(write) << write.error();

    {
        std::fstream file(path, std::ios::in | std::ios::out | std::ios::binary);
        ASSERT_TRUE(file);
        file.seekp(8, std::ios::beg);
        file.put('\x7f');
    }

    auto read = alignx::index::read_axf_index(path);
    EXPECT_FALSE(read);
    EXPECT_NE(read.error().find("CRC mismatch"), std::string::npos);

    std::filesystem::remove(path);
}

} // namespace
