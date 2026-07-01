#pragma once

#include "tree.hpp"
#include <filesystem>
#include <set>

std::shared_ptr<node_t> parse_file(const std::filesystem::path &file_path,
                                   const std::string           &language,
                                   const std::set<std::string> &atomic_types = {});
