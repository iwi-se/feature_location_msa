#include "postprocessing.hpp"

void apply_filler_size(std::vector<file_variant> &aligned_file_variants)
{
  for (int i {}; i < aligned_file_variants[0].m_token_table->size(); ++i)
  {
    int maxLength {};
    for (auto &file : aligned_file_variants)
    {
      auto &seq { *file.m_token_table };
      if (seq[i].token_kind == alignment_token::token_kind::filler)
      {
        continue;
      }
      if (seq[i].token_kind == alignment_token::token_kind::node
          && seq[i].node->get_ts_text().size() > maxLength)
      {
        maxLength = seq[i].node->get_ts_text().size();
      }
    }

    for (auto &file : aligned_file_variants)
    {
      auto &seq { *file.m_token_table };
      seq[i].filler_size = maxLength;
    }
  }
}
