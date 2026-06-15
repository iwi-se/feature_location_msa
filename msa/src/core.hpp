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
    enum class token_kind
    {
      node,
      filler
    } token_kind;
    std::shared_ptr<node_t> node {};
    int                     filler_size {};

    inline bool is_filler() const
    {
      return token_kind == token_kind::filler;
    }

    inline bool is_node() const
    {
      return token_kind == token_kind::node;
    }
};

const alignment_token FILLER = { alignment_token::token_kind::filler, nullptr };

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

struct guide_tree_node
{
    size_t id { 0 };

    std::optional<size_t> variant_index;

    std::shared_ptr<guide_tree_node> left;
    std::shared_ptr<guide_tree_node> right;

    double similarity { 0.0 };
    size_t size { 1 };

    bool is_leaf() const
    {
      return variant_index.has_value();
    }
};

struct guide_tree
{
    std::shared_ptr<guide_tree_node> root;

    // optional: store leaves in order for fast lookup
    std::vector<std::shared_ptr<guide_tree_node>> leaves;

    // map variant index -> leaf node
    std::unordered_map<size_t, std::shared_ptr<guide_tree_node>> leaf_map;
};

struct file_variant_info final
{
    std::string           variant;
    std::filesystem::path filepath;
};

class file_variant final

{
  public:
    std::string                            variant;
    std::filesystem::path                  filepath;
    std::optional<std::shared_ptr<node_t>> ast { std::nullopt };
    std::optional<token_table>             m_token_table { std::nullopt };
    std::optional<std::vector<size_t>>     hashed_ngrams { std::nullopt };

    file_variant(const std::string           &variant_name,
                 const std::filesystem::path &filepath);
    file_variant(const file_variant &)             = delete; // never copy
    file_variant &operator= (const file_variant &) = delete; // never copy
    file_variant(file_variant &&);                           // only move
};

struct file_family_info final
{
    std::string                    name {};
    std::vector<file_variant_info> variants;
};

struct file_family final
{
    std::string               name {};
    std::vector<file_variant> variants;
    std::optional<guide_tree> m_guide_tree;

    file_family(const std::string &name, std::vector<file_variant> &&variants);
    file_family(const file_family_info &info);
    file_family(file_family &&);                           // only move
    file_family(const file_family &)             = delete; // never copy
    file_family &operator= (const file_family &) = delete; // never copy
};

using file_families = std::vector<file_family>;
