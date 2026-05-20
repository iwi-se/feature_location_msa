#pragma once
#include "arguments.hpp"
#include "core.hpp"
#include <vector>

void load_asts(std::vector<file_variant>& variants, const options& options);

void build_token_tables(std::vector<file_variant>& variants);

hash_count build_hash_count(std::vector<file_variant>& variants);

void calculate_ngram_hashes(std::vector<file_variant>& variants,
                            const options&             options);

std::vector<size_t>
    hash_ngrams(const std::vector<std::vector<alignment_token>>& ngrams);

std::vector<std::vector<alignment_token>>
    calculate_ngrams(const std::vector<alignment_token>& a, size_t n);
