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
    Pipeline p;
    Result<Port<int>> src_res = p.add_stage<int>("src", src);
    ASSERT_TRUE(src_res.has_value());
    Port<int> src_p = src_res.value();

    Result<Port<int>> incr_res = p.add_stage<int>("incr", incr, src_p);
    ASSERT_TRUE(incr_res.has_value());
    Port<int> incr_p = incr_res.value();

    Result<Port<int>> triple_res = p.add_stage<int>("triple", triple, incr_p);
    ASSERT_TRUE(triple_res.has_value());
}

TEST(PipelineTest, RunSingleStage) {
    Pipeline p;

    auto src_res = p.add_stage<int>("src", src);
    ASSERT_TRUE(src_res.has_value());

    auto output = p.run<int>(src_res.value());
    ASSERT_TRUE(output.has_value());
    ASSERT_EQ(output.value(), 5);
    std::cout << "Single stage output: " << output.value() << "\n";
}

TEST(PipelineTest, RunMultipleStages) {
    Pipeline p;

    auto src_res = p.add_stage<int>("src", src);
    ASSERT_TRUE(src_res.has_value());
    auto src_p = src_res.value();

    auto incr_res = p.add_stage<int>("incr", incr, src_p);
    ASSERT_TRUE(incr_res.has_value());
    auto incr_p = incr_res.value();

    auto triple_res = p.add_stage<int>("triple", triple, incr_p);
    ASSERT_TRUE(triple_res.has_value());

    auto output = p.run<int>(triple_res.value());
    ASSERT_TRUE(output.has_value());
    ASSERT_EQ(output.value(), (5 + 1) * 3);
}

TEST(PipelineTest, RunDiamondDependency) {
    //        src
    //       /  \
    //    incr  triple
    //       \   /
    //        sum
    Pipeline p;

    auto src_res = p.add_stage<int>("src", src);
    ASSERT_TRUE(src_res.has_value());
    auto src_p = src_res.value();

    auto incr_res = p.add_stage<int>("incr", incr, src_p);
    ASSERT_TRUE(incr_res.has_value());
    auto incr_p = incr_res.value();

    auto triple_res = p.add_stage<int>("triple", triple, src_p);
    ASSERT_TRUE(triple_res.has_value());
    auto triple_p = triple_res.value();

    auto sum_res = p.add_stage<int>("sum", sum, incr_p, triple_p);
    ASSERT_TRUE(sum_res.has_value());

    auto output = p.run<int>(sum_res.value());
    if (!output.has_value()) {
        std::cout << "Error: " << output.error();
    }
    ASSERT_TRUE(output.has_value());

    // src = 5
    // incr = 6
    // triple = 15
    // sum = 6 + 15 = 21
    ASSERT_EQ(output.value(), 21);
}