#pragma once
#include "arguments.hpp"
#include "core.hpp"
#include <unordered_map>
#include <vector>

void align_file_variants(std::vector<file_variant>& variants,
                         const options&             options);

void refine_alignment(std::vector<file_variant>&          variants,
                      const std::vector<size_t>&          distinct_indices,
                      const hash_count&                   hash_count,
                      std::unordered_map<size_t, double>& cache);
