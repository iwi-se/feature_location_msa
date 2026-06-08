#include "output.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>

std::string render_token(const alignment_token &t)
{
  if (t.is_filler())
  {
    std::string res;
    for (size_t i { 0 }; i < t.filler_size; ++i)
    {
      res += " ";
    }
    return res;
  }
  if (t.is_node())
  {
    auto tokenText { t.node->get_ts_text() };
    for (auto &ch : tokenText)
    {
      if (std::isspace(ch))
      {
        ch = ' ';
      }
    }
    return tokenText;
  }
  return "";
}

void write_human_output(const file_family &file_family, const options &options)
{
  std::string outputFilePath
      = options.output_directory / (file_family.name + ".txt");

  std::ofstream outputFile(outputFilePath);
  if (outputFile.is_open())
  {
    outputFile << "Alignment for: " << file_family.name << ":\n";
    for (const auto &file : file_family.variants)
    {
      outputFile << file.filepath.string();
      outputFile << "\n";

      auto &seq { *file.m_token_table };
      for (const auto &tok : seq)
      {
        outputFile << render_token(tok);
      }
      outputFile << "\n";
    }
    outputFile << "\n";
    outputFile.close();
  }
  else
  {
    std::cout << "Unable to open file: " << outputFilePath << "\n";
  }
}

void write_machine_output(const file_family &file_family,
                          const options     &options)
{
  std::string outputFilePath
      = options.output_directory / (file_family.name + ".output");

  std::ofstream outputFile(outputFilePath);
  if (outputFile.is_open())
  {
    outputFile << file_family.name << "\n";
    for (const auto &variant : file_family.variants)
    {
      // outputFile << variant << "\n";
      outputFile << variant.filepath.string();
      outputFile << "\n";
      for (const auto &tok : *variant.m_token_table)
      {
        if (tok.is_filler())
        {
          outputFile << 0;
        }
        else
        {
          outputFile << 1;
        }
      }
      outputFile << "\n";
    }
    outputFile.close();
  }
  else
  {
    std::cout << "Unable to open file: " << outputFilePath << "\n";
  }
}

void output(const file_family &file_family, const options &options)
{
  std::filesystem::create_directory(options.output_directory);
  write_human_output(file_family, options);
  write_machine_output(file_family, options);
}
