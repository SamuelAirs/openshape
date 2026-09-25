#include "core/Uuid.h"

#include <gtest/gtest.h>

#include <unordered_set>

using namespace os;

TEST(Uuid, GeneratesUniqueVersion4)
{
    std::unordered_set<Uuid> seen;
    for (int i = 0; i < 10000; ++i) {
        const Uuid id = Uuid::generate();
        EXPECT_FALSE(id.isNil());
        EXPECT_EQ(id.bytes()[6] >> 4, 4);
        EXPECT_EQ(id.bytes()[8] & 0xC0, 0x80);
        EXPECT_TRUE(seen.insert(id).second);
    }
}

TEST(Uuid, RoundTripsThroughText)
{
    const Uuid id = Uuid::generate();
    const std::string text = id.toString();
    EXPECT_EQ(text.size(), 36u);
    const auto parsed = Uuid::parse(text);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(*parsed, id);
    EXPECT_EQ(Uuid::parse("{" + text + "}"), id);
}

TEST(Uuid, RejectsMalformed)
{
    EXPECT_FALSE(Uuid::parse("").has_value());
    EXPECT_FALSE(Uuid::parse("not-a-uuid").has_value());
    EXPECT_FALSE(Uuid::parse("123e4567-e89b-12d3-a456-42661417400").has_value());
    EXPECT_FALSE(Uuid::parse("123e4567-e89b-12d3-a456-4266141740000").has_value());
    EXPECT_FALSE(Uuid::parse("123e4567xe89b-12d3-a456-426614174000").has_value());
    EXPECT_FALSE(Uuid::parse("123e4567-e89b-12d3-a456-42661417400g").has_value());
    EXPECT_TRUE(Uuid::parse("123E4567-E89B-12D3-A456-426614174000").has_value());
}

TEST(Uuid, NilByDefault)
{
    EXPECT_TRUE(Uuid().isNil());
    EXPECT_EQ(Uuid().toString(), "00000000-0000-0000-0000-000000000000");
}
