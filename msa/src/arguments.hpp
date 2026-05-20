#pragma once
#include <filesystem>
#include <string>

constexpr bool FAST { true };

enum class language
{
  cpp,
  java
};

struct options
{
    language              language;
    std::filesystem::path path;
    size_t                n_gram_size { 6 };
};

options parse_cli_arguments(int argc, char* argv[]);

std::string render_language(const language& lang);
