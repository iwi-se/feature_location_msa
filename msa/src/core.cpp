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
