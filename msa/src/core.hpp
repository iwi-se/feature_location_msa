#pragma once
#include "tree.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

struct alignment_token
{
    enum class TokenKind
    {
      Node,
      Filler
    } token_kind;
    std::shared_ptr<Node> node {};
    int                   filler_size {};

    inline bool is_filler() const
    {
      return token_kind == TokenKind::Filler;
    }

    inline bool is_node() const
    {
      return token_kind == TokenKind::Node;
    }
};

bool operator== (const alignment_token &a, const alignment_token &b);

bool operator!= (const alignment_token &a, const alignment_token &b);

struct hash_count
{
    std::unordered_map<size_t, size_t> m {};
    size_t                             max {};

    inline size_t &operator[] (const size_t &a)
    {
      return m[a];
    }
};

using token_table = std::vector<alignment_token>;

class file_variant final
{
  public:
    std::string                          variant;
    std::filesystem::path                filepath;
    std::optional<std::shared_ptr<Node>> ast { std::nullopt };
    std::optional<token_table>           token_table { std::nullopt };
    std::optional<std::vector<size_t>>   hashed_ngrams { std::nullopt };

    file_variant(const file_variant &)             = delete; // never copy
    file_variant &operator= (const file_variant &) = delete; // never copy
};

struct file_family final
{
    std::string               name {};
    std::vector<file_variant> variants;
};

using file_families = std::vector<file_family>;
