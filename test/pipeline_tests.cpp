#include "pipeline_builder.hpp"
#include <gtest/gtest.h>

using namespace pipeline;

auto src = []() { return 5; };
auto incr = [](int x) { return x + 1; };
auto triple = [](int x) { return x * 3; };
auto sum = [](int x, int y) { return x + y; };

TEST(PipelineTest, BuildSimpleStage) {
    Pipeline p;
    Result<Port<int>> src_res = p.add_stage<int>("src", src);
    EXPECT_TRUE(src_res.has_value());
}

TEST(PipelineTest, DuplicateStage) {
    Pipeline p;
    Result<Port<int>> src_res = p.add_stage<int>("src", src);
    Result<Port<int>> err = p.add_stage<int>("src", src);

    EXPECT_TRUE(src_res.has_value());
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(err.error(), Error::StageAlreadyExists);
}

TEST(PipelineTest, WireWithPorts) {
    /*
        src
       /    \
    incr ->  triple
    */
    Pipeline p;
    Result<Port<int>> src_res = p.add_stage<int>("src", src);
    ASSERT_TRUE(src_res.has_value());
    Port<int> src_p = src_res.value();

    Result<Port<int>> incr_res = p.add_stage<int>("incr", incr, src_p);
    ASSERT_TRUE(incr_res.has_value());
    Port<int> incr_p = incr_res.value();

    Result<Port<int>> triple_res =
        p.add_stage<int>("triple", triple, src_p, incr_p);
    ASSERT_TRUE(triple_res.has_value());
}

TEST(PipelineTest, RunSingleStage) {}

TEST(PipelineTest, RunMultipleStages) {}

TEST(PipelineTest, RunDiamondDependency) {}