#include "core.hpp"
#include <optional>

bool operator== (const alignment_token &a, const alignment_token &b)
{
  if (a.is_filler() && b.is_filler())
  {
    return true;
  }
  if (a.is_node() && b.is_node())
  {
    if (a.node->get_ts_text() == b.node->get_ts_text())
    {
      return true;
    }
  }
  return false;
}

bool operator!= (const alignment_token &a, const alignment_token &b)
{
  return !(a == b);
}

file_variant::file_variant(const std::string           &variant_name,
                           const std::filesystem::path &filepath)
    : variant { variant_name }
    , filepath { filepath }
{ }

file_variant::file_variant(file_variant &&other)
    : variant { other.variant }
    , filepath { other.filepath }
{
  if (other.ast)
  {
    this->ast = std::move(*other.ast);
    other.ast = std::nullopt;
  }

  if (other.m_token_table)
  {
    this->m_token_table = std::move(*other.m_token_table);
    other.m_token_table = std::nullopt;
  }

  if (other.hashed_ngrams)
  {
    this->hashed_ngrams = std::move(*other.hashed_ngrams);
    other.hashed_ngrams = std::nullopt;
  }
}

file_family::file_family(const std::string          &name,
                         std::vector<file_variant> &&variants)
    : name { name }
    , variants { std::move(variants) }
{ }

file_family::file_family(file_family &&other)
    : name { other.name }
    , variants { std::move(other.variants) }
{ }
