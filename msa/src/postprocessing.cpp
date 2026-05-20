#include "postprocessing.hpp"

void apply_filler_size(std::vector<file_variant> &aligned_file_variants)
{
  for (int i {}; i < aligned_file_variants[0].token_table->size(); ++i)
  {
    int maxLength {};
    for (auto &file : aligned_file_variants)
    {
      auto &seq { *file.token_table };
      if (seq[i].token_kind == alignment_token::token_kind::Filler)
      {
        continue;
      }
      if (seq[i].token_kind == alignment_token::token_kind::Node
          && seq[i].node->getTsText().size() > maxLength)
      {
        maxLength = seq[i].node->getTsText().size();
      }
    }

    for (auto &file : aligned_file_variants)
    {
      auto &seq { *file.token_table };
      seq[i].filler_size = maxLength;
    }
  }
}
