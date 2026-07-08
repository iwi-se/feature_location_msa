#pragma once
#include "core.hpp"
#include <string>
#include <unordered_map>

using combination_key = std::string;

combination_key compute_combination(const std::vector<file_variant> &variants,
                                    size_t                            col);

std::unordered_map<combination_key, size_t>
build_combination_counts(const std::vector<file_variant> &variants);

void refine_rare_combinations(std::vector<file_variant> &variants);
