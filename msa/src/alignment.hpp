#pragma once
#include "arguments.hpp"
#include "core.hpp"
#include <vector>

void align_file_variants(std::vector<file_variant>& variants,
                         const options&             options);

void align_guide_tree(file_family& family, const options& options);
