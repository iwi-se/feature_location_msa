#include "preprocessing.hpp"
#include "core.hpp"
#include "parser.hpp"
#include <functional>
#include <iostream>
#include <string>

void load_asts(std::vector<file_variant> &variants, const options &options)
{
  for (auto &file_variant : variants)
  {
    try
    {
      auto ast { parseFile(file_variant.filepath,
                           render_language(options.language)) };
      file_variant.ast = ast;
    }
    catch (...)
    {
      std::cerr << "Unable to parse file " << file_variant.filepath
                << " of variant " << file_variant.variant << "." << std::endl;
    }
  }
}

std::vector<alignment_token> build_token_table(std::shared_ptr<Node> &ast)
{
  std::vector<alignment_token> tokenTable {};
  for (auto &leaf : ast->getLeafs())
  {
    tokenTable.push_back(
        alignment_token { alignment_token::TokenKind::Node, leaf });
  }
  return tokenTable;
}

void build_token_tables(std::vector<file_variant> &variants)
{
  for (auto &variant : variants)
  {
    if (variant.ast)
    {
      variant.token_table = build_token_table(*variant.ast);
    }
  }
}

hash_count build_hash_count(std::vector<file_variant> &variants)
{
  hash_count hash_count;
  for (auto &variant : variants)
  {
    if (!variant.token_table)
    {
      continue;
    }

    for (const auto &alignment_token : *variant.token_table)
    {
      if (alignment_token.is_node())
      {
        auto token { alignment_token.node };
        if (hash_count.m.find(token->getSubtreeHash()) == hash_count.m.end())
        {
          hash_count[token->getSubtreeHash()] = 1;
        }
        hash_count[token->getSubtreeHash()]++;
        if (hash_count[token->getSubtreeHash()] > hash_count.max)
        {
          hash_count.max = hash_count[token->getSubtreeHash()];
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
      seed ^= std::hash<size_t> {}(tok.node->getSubtreeHash()) + 0x9e37'79b9
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
        calculate_ngrams(*variant.token_table, options.n_gram_size));
  }
}
