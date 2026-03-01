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

TEST(PipelineTest, WriteReadIncrementPipeline) {
    // Test using the filesystem as inputs/outputs
    // 1. Generate a number
    // 2. Write data to a file
    // 3. Read data from the file
    // 4. Increment number

    const std::string path = "message.txt";
    std::ofstream f("message.txt");
    f.close();

    Pipeline p;

    // Stage 1: generate number
    auto gen = p.add_stage<std::string>("generate", [] {
        return std::string("Hello world");  
    }).value();

    // transform to std::vector<uint8_t>
    auto string_to_vector = p.add_stage<std::vector<uint8_t>>("string to vector",
        [](const std::string& s) {
            return std::vector<std::uint8_t>(s.begin(), s.end());
        }, gen).value();

    // Stage 2: write number to file
    auto write = p.add_file_sink_bytes("write", string_to_vector, path).value();

    // Stage 3: read number back
    auto read = p.add_file_source_bytes("read", path, write).value();

    auto vector_to_string = p.add_stage<std::string>("vector to string",
        [](const std::vector<uint8_t>& v) {
            return std::string(v.begin(), v.end());
        }, read
    ).value();

    // Stage 4: increment
    auto append = p.add_stage<std::string>(
        "append",
        [](std::string s) { return s + std::string(", goodbye world"); },
        vector_to_string
    ).value();

    auto result = p.run<std::string>(append);

    if (!result.has_value()) {
        std::cout << result.error() << " is the error\n";
    }
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), "Hello world, goodbye world");
}