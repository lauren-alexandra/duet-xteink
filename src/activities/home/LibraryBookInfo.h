#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct LibraryBookSearchResult {
  std::string path;
  std::string title;
  std::string author;
  std::string series;
};

struct LibraryBookSearchResponse {
  bool catalogAvailable = false;
  bool truncated = false;
  size_t totalMatches = 0;
  std::vector<LibraryBookSearchResult> books;
};

enum class LibraryBookSuggestionKind : uint8_t { Title, Author, Series };

struct LibraryBookSuggestion {
  std::string value;
  LibraryBookSuggestionKind kind = LibraryBookSuggestionKind::Title;
};

struct LibraryBookSuggestionResponse {
  bool catalogAvailable = false;
  std::vector<LibraryBookSuggestion> suggestions;
};

struct LibraryBookInfo {
  bool catalogAvailable = false;
  bool found = false;
  std::string title;
  std::string author;
  std::string series;
  std::string seriesIndex;
  std::string genre;
  std::string spice;
  std::string description;

  static LibraryBookInfo load(const std::string& devicePath, const std::string& titleHint = {},
                              const std::string& authorHint = {});
  static LibraryBookSearchResponse search(const std::string& query, size_t maxResults);
  static LibraryBookSuggestionResponse suggest(const std::string& query, size_t maxSuggestions);
  static bool hasCatalog();
  static void releaseSuggestionCache();
};
