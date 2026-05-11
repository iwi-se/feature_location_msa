#pragma once
#include "arguments.hpp"
#include <filesystem>
#include <string>
#include <vector>

// a file in a variant
struct variant_file final
{
    std::string           variant;
    std::filesystem::path filepath;
};

struct file_family final
{
    std::string               name {};
    std::vector<variant_file> variants;
};

using file_families = std::vector<file_family>;

file_families discover_files(const options& options);
