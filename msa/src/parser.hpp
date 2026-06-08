#pragma once

#include "tree.hpp"
#include <filesystem>

std::shared_ptr<node_t> parse_file(const std::filesystem::path &filePath,
                                   const std::string           &language);
