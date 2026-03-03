#include <iostream>
#include <cassert>
#include "pipeline_builder.hpp"
using namespace pipeline;

std::string convert_to_string(const std::vector<uint8_t>& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

std::unordered_map<char, size_t> letter_count(const std::string& str) {
    std::unordered_map<char, size_t> freq;
    for (auto& c : str) {
        if (!freq.contains(c)) freq.emplace(c, 0);
        freq[c]++;
    }
    return freq;
}

std::string to_lowercase(const std::string& in) {
    std::string result = in;
    for (char &c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return result;
}

// Letter count
int main() {
    /*
        - read file
        - convert from vector of bytes to string   
        - count letter frequency
        - output
    */
    Pipeline p;

    Result<Port<std::vector<uint8_t>>> read_result 
        = p.read_bytes_from_file("read", "/Users/nikhil.jain/Documents/Personal_Work/lm_studio_interview/pipeline-builder/src/foo.txt");
    
    if (!read_result.has_value()) {
        std::cout << "Error was: " << read_result.error() << "\n";
        return -1;
    }
    std::cout << "Successfully read from file.\n";
    auto read_port = read_result.value();
    auto conversion_port = p.add_stage("convert", convert_to_string, read_port).value();
    auto lc_port = p.add_stage("lc", letter_count, conversion_port).value();
    auto result = p.run(lc_port);
    if (result.has_value()) {
        for (auto& kv : result.value()) {
            std::cout << kv.first << " -> " << kv.second << "\n";
        }
    } else {
        std::cout << result.error();
    }

}