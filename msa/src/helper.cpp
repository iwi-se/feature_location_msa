#include "helper.hpp"
#include <iostream>

void print_token_vector(const std::vector<alignment_token>& vec)
{
  for (const auto& tok : vec)
  {
    if (tok.is_filler())
    {
      std::cout << "FILLER";
    }
    else
    {
      std::cout << tok.node->get_ts_text();
    }
  }
  std::cout << "\n" << std::endl;
}
