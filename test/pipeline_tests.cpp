#include <gtest/gtest.h>
#include "pipeline_builder.hpp"

using namespace pipeline;

TEST(PipelineTest, SimpleStage) {
    Pipeline p;
    Result src_res = p.add_stage<int>("src", [](int x) { return 5; });
    EXPECT_TRUE(src_res.has_value());
}

TEST(PipelineTest, DuplicateStage) {
    Pipeline p;
    Result src_res = p.add_stage<int>("src", [](int x) { return 5; });
    EXPECT_TRUE(src_res.has_value());
    Result err = p.add_stage<int>("src", [](int x) { return 5; });
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), Error::StageAlreadyExists);
}
