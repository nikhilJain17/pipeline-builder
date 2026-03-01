#include "pipeline_builder.hpp"
#include <gtest/gtest.h>

using namespace pipeline;

auto src = []() { return 5; };
auto incr = [](int x) { return x + 1; };
auto triple = [](int x) { return x * 3; };
auto sum = [](std::pair<int, int> pair) { return pair.first + pair.second; };

auto message = []() { return std::string("Hello world"); };
auto name = []() { return std::string("Nikhil"); };
auto to_upper = [](const std::string& s) {
    std::string result = s;
    for (char& c : result) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return result;
};
auto string_to_bytes = [](const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
};
auto bytes_to_string = [](const std::vector<std::uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
};

auto sign_message = [](const std::pair<std::string, std::string>& msg_name_pair) {
    return std::string(msg_name_pair.first + "\nFrom " + msg_name_pair.second);
};

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

TEST(PipelineTest, DiamondDependency) {
    /*
           src
        /       \
      incr      triple
       \        /
          join
           |
           sum
    */
    Pipeline p;

    auto src_port = p.add_stage<int>("src", src).value();
    auto incr_port = p.add_stage<int>("increment", incr, src_port).value();
    auto triple_port = p.add_stage<int>("triple", triple, src_port).value();
    auto join_port = p.join("join", incr_port, triple_port).value();
    auto sum_port = p.add_stage<int>("sum", sum, join_port).value();
    Result<int> output = p.run(sum_port);
    ASSERT_TRUE(output.has_value());
    // (5 + 1) + (5 * 3) = 21
    ASSERT_EQ(output.value(), 21);
}

TEST(PipelineTest, FileIOTest) {
    /*
      msg      name
       |        |
    to_upper  to_upper
       |        |
    to_bytes   to_bytes
       |        |
    write     write
       |        |
    read      read 
       |        |
    to_str    to_str
       \       /
          join
           |
         sign_msg
    */
    const std::string msg_path = "msg.txt";
    const std::string name_path = "name.txt";
    {
        std::ofstream name_file(name_path, std::ios::binary);
        std::ofstream msg_file(msg_path, std::ios::binary);
    }

    Pipeline p;
    auto msg_port = p.add_stage<std::string>("message", message).value();
    auto upper_msg_port = p.add_stage<std::string>("upper_msg", to_upper, msg_port).value();
    auto upper_msg_to_bytes_port = p.add_stage<std::vector<uint8_t>>(
        "upper_msg_to_bytes", string_to_bytes, upper_msg_port).value();
    auto write_msg_port = p.write_bytes_to_file("write_msg", msg_path, upper_msg_to_bytes_port).value();

    auto name_port = p.add_stage<std::string>("name", name).value();
    auto upper_name_port = p.add_stage<std::string>("upper_name", to_upper, name_port).value();
    auto upper_name_to_bytes_port = p.add_stage<std::vector<uint8_t>>(
        "upper_name_to_bytes", string_to_bytes, upper_name_port).value();
    auto write_name_port = p.write_bytes_to_file("write_name", name_path, upper_name_to_bytes_port).value();

    auto read_name_bytes_port = p.read_bytes_from_file("read_name", name_path, write_name_port).value();
    auto read_name_str_port = p.add_stage<std::string>("name to str", bytes_to_string, read_name_bytes_port).value();

    auto read_msg_bytes_port = p.read_bytes_from_file("read_msg", msg_path, write_msg_port).value();
    auto read_msg_str_port = p.add_stage<std::string>("msg to str", bytes_to_string, read_msg_bytes_port).value();

    auto join_port = p.join("join", read_msg_str_port, read_name_str_port).value();
    auto signature_port = p.add_stage<std::string>("signature", sign_message, join_port).value();

    Result<std::string> out = p.run(signature_port);
    ASSERT_TRUE(out.has_value());
    ASSERT_EQ(out.value(), "HELLO WORLD\nFrom NIKHIL");   
}