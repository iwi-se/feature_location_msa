#pragma once

#include "tree.hpp"
#include <filesystem>

std::shared_ptr<Node> parseFile(const std::filesystem::path &filePath,
                                const std::string           &language);
