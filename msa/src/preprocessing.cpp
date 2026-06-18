#include "preprocessing.hpp"
#include "core.hpp"
#include "parser.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>

struct cached_ast
{
    std::filesystem::path   representative_file;
    std::shared_ptr<node_t> ast;
};

using ast_cache = std::unordered_map<std::uintmax_t, std::vector<cached_ast>>;

// Stolen from here:
// https://stackoverflow.com/questions/6163611/compare-two-files
bool compare_files(const std::filesystem::path &p1,
                   const std::filesystem::path &p2)
{
  std::ifstream f1(p1, std::ifstream::binary | std::ifstream::ate);
  std::ifstream f2(p2, std::ifstream::binary | std::ifstream::ate);

  if (f1.fail() || f2.fail())
  {
    return false; // file problem
  }

  if (f1.tellg() != f2.tellg())
  {
    return false; // size mismatch
  }

  // seek back to beginning and use std::equal to compare contents
  f1.seekg(0, std::ifstream::beg);
  f2.seekg(0, std::ifstream::beg);
  return std::equal(std::istreambuf_iterator<char>(f1.rdbuf()),
                    std::istreambuf_iterator<char>(),
                    std::istreambuf_iterator<char>(f2.rdbuf()));
}

std::shared_ptr<node_t> find_existing_ast(const std::filesystem::path &filepath,
                                          ast_cache                   &cache)
{
  const auto size = std::filesystem::file_size(filepath);

  auto bucket_it = cache.find(size);
  if (bucket_it == cache.end())
  {
    return {};
  }

  for (const auto &candidate : bucket_it->second)
  {
    if (compare_files(filepath, candidate.representative_file))
    {
      return candidate.ast;
    }
  }

  return {};
}

void load_asts(std::vector<file_variant> &variants, const options &options)
{
  ast_cache cache;

  for (auto &variant : variants)
  {
    try
    {
      if (auto existing_ast = find_existing_ast(variant.filepath, cache))
      {
        variant.ast = std::move(existing_ast);
        continue;
      }

      auto ast
          = parse_file(variant.filepath, render_language(options.m_language));

      variant.ast = ast;

      const auto size = std::filesystem::file_size(variant.filepath);

      cache[size].push_back(
          { .representative_file = variant.filepath, .ast = std::move(ast) });
    }
    catch (const std::exception &e)
    {
      std::cerr << "Unable to parse file " << variant.filepath << " of variant "
                << variant.variant << ": " << e.what() << '\n';
    }
    catch (...)
    {
      std::cerr << "Unable to parse file " << variant.filepath << " of variant "
                << variant.variant << ".\n";
    }
  }
}

std::vector<alignment_token> build_token_table(std::shared_ptr<node_t> &ast)
{
  std::vector<alignment_token> token_table {};
  for (auto &leaf : ast->get_leaves())
  {
    token_table.push_back(
        alignment_token { alignment_token::token_kind::node, leaf.lock() });
  }
  return token_table;
}

void build_token_tables(std::vector<file_variant> &variants)
{
  for (auto &variant : variants)
  {
    if (variant.ast)
    {
      variant.m_token_table = build_token_table(*variant.ast);
    }
  }
}

hash_count build_hash_count(std::vector<file_variant> &variants)
{
  hash_count hash_count;
  for (auto &variant : variants)
  {
    if (!variant.m_token_table)
    {
      continue;
    }

    for (const auto &alignment_token : *variant.m_token_table)
    {
      if (alignment_token.is_node())
      {
        auto token { alignment_token.node };
        if (hash_count.m.find(token->get_subtree_hash()) == hash_count.m.end())
        {
          hash_count[token->get_subtree_hash()] = 1;
        }
        hash_count[token->get_subtree_hash()]++;
        if (hash_count[token->get_subtree_hash()] > hash_count.max)
        {
          hash_count.max = hash_count[token->get_subtree_hash()];
        }
      }
    }
  }
  return hash_count;
}

std::vector<std::vector<alignment_token>>
    calculate_ngrams(const std::vector<alignment_token> &a, size_t n)
{
  if (n == 0)
  {
    throw std::invalid_argument("n must be greater than 0");
  }

  std::vector<std::vector<alignment_token>> ngrams;
  if (a.size() < n)
  {
    return ngrams;
  }

  for (size_t i = 0; i <= a.size() - n; ++i)
  {
    std::vector<alignment_token> gram(a.begin() + i, a.begin() + i + n);
    ngrams.push_back(gram);
  }

  return ngrams;
}

std::vector<size_t>
    hash_ngrams(const std::vector<std::vector<alignment_token>> &ngrams)
{
  std::vector<size_t> ngram_hashes {};
  for (const auto &ngram : ngrams)
  {
    std::size_t seed = ngram.size();
    for (const alignment_token &tok : ngram)
    {
      seed ^= std::hash<size_t> {}(tok.node->get_subtree_hash()) + 0x9e37'79b9
              + (seed << 6) + (seed >> 2);
    }
    ngram_hashes.push_back(seed);
  }
  return ngram_hashes;
}

void calculate_ngram_hashes(std::vector<file_variant> &variants,
                            const options             &options)
{
  for (auto &variant : variants)
  {
    variant.hashed_ngrams = hash_ngrams(
        calculate_ngrams(*variant.m_token_table, options.n_gram_size));
  }
}
