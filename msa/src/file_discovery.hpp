#pragma once
#include "arguments.hpp"
#include <filesystem>
#include <map>
#include <string>
#include <vector>

// a file in a variant
struct variant_file final
{
    std::string           variant;
    std::filesystem::path filepath;
};

class file_families final
{
  public:
    void add_file(const std::string&           file_family_name,
                  const std::string&           variant_name,
                  const std::filesystem::path& absolute_path);
  private:
    std::map<std::string, std::vector<variant_file>>
        file_family_name_to_variant_paths_map {};
};

file_families discover_files(const options& options);
