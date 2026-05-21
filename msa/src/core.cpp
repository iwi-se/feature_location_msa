#include "core.hpp"

bool operator== (const alignment_token &a, const alignment_token &b)
{
  if (a.is_filler() && b.is_filler())
  {
    return true;
  }
  if (a.is_node() && b.is_node())
  {
    if (a.node->getTsText() == b.node->getTsText())
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

file_family::file_family(const std::string          &name,
                         std::vector<file_variant> &&variants)
    : name { name }
    , variants { std::move(variants) }
{ }

file_family::file_family(file_family &&other)
    : name { other.name }
    , variants { std::move(other.variants) }
{ }
