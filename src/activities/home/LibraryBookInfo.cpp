#include "LibraryBookInfo.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>
#include <vector>

#include "activities/reader/LibraryInsights.h"

namespace {
constexpr int kMinCatalogVersion = 1;
constexpr int kMaxCatalogVersion = 2;
constexpr size_t kMaxCatalogLineBytes = 2048;
constexpr int kMaxCatalogAuthors = 4096;
constexpr int kMaxCatalogSeries = 4096;
constexpr int kMaxCatalogGenres = 1024;
constexpr int kMaxCatalogSpiceLevels = 256;
constexpr size_t kMaxCachedSuggestionCandidates = 768;

class CatalogLineReader {
 public:
  explicit CatalogLineReader(FsFile& file) : file_(file), logicalPosition_(file.position()) {}

  bool available() const { return bufferPosition_ < bufferSize_ || file_.available() > 0; }
  uint32_t position() const { return logicalPosition_; }

  bool seek(const uint32_t position) {
    bufferPosition_ = 0;
    bufferSize_ = 0;
    if (!file_.seek(position)) return false;
    logicalPosition_ = position;
    return true;
  }

  bool readLine(std::string& line) {
    line.clear();
    while (available()) {
      if (bufferPosition_ >= bufferSize_) {
        const int read = file_.read(buffer_.data(), buffer_.size());
        if (read <= 0) break;
        bufferPosition_ = 0;
        bufferSize_ = static_cast<size_t>(read);
      }

      const char value = static_cast<char>(buffer_[bufferPosition_++]);
      ++logicalPosition_;
      if (value == '\n') return true;
      if (value == '\r') continue;
      if (line.size() >= kMaxCatalogLineBytes) {
        LOG_ERR("BOOKINFO", "Catalog line exceeds %u bytes", static_cast<unsigned>(kMaxCatalogLineBytes));
        return false;
      }
      line.push_back(value);
    }
    return !line.empty();
  }

 private:
  FsFile& file_;
  std::array<uint8_t, 512> buffer_{};
  size_t bufferPosition_ = 0;
  size_t bufferSize_ = 0;
  uint32_t logicalPosition_ = 0;
};

template <size_t N>
size_t splitFields(const std::string& line, std::array<std::string_view, N>& fields) {
  size_t count = 0;
  size_t start = 0;
  while (count < N) {
    const size_t tab = line.find('\t', start);
    if (tab == std::string::npos) {
      fields[count++] = std::string_view(line).substr(start);
      break;
    }
    fields[count++] = std::string_view(line).substr(start, tab - start);
    start = tab + 1;
  }
  return count;
}

bool parseInt(const std::string_view value, int& out) {
  if (value.empty() || value.size() >= 24) return false;
  char buffer[24];
  std::copy(value.begin(), value.end(), buffer);
  buffer[value.size()] = '\0';
  char* end = nullptr;
  const long parsed = std::strtol(buffer, &end, 10);
  if (end == buffer || *end != '\0' || parsed < std::numeric_limits<int>::min() ||
      parsed > std::numeric_limits<int>::max()) {
    return false;
  }
  out = static_cast<int>(parsed);
  return true;
}

bool readValidHeader(CatalogLineReader& reader, int& version, std::string& line) {
  std::array<std::string_view, 10> fields{};
  if (!reader.readLine(line) || splitFields(line, fields) != 7 || fields[0] != "M" ||
      !parseInt(fields[1], version) ||
      version < kMinCatalogVersion || version > kMaxCatalogVersion) {
    LOG_ERR("BOOKINFO", "Invalid or unsupported library catalog header");
    return false;
  }
  return true;
}

std::string searchKey(const std::string_view value) {
  std::string key;
  key.reserve(value.size());
  bool lastWasSpace = true;
  for (const unsigned char c : value) {
    if (c >= 0x80) {
      key.push_back(static_cast<char>(c));
      lastWasSpace = false;
    } else if (std::isalnum(c)) {
      key.push_back(static_cast<char>(std::tolower(c)));
      lastWasSpace = false;
    } else if (!lastWasSpace) {
      key.push_back(' ');
      lastWasSpace = true;
    }
  }
  while (!key.empty() && key.back() == ' ') key.pop_back();
  return key;
}

bool matchesSearch(const std::string_view value, const std::string& queryKey) {
  return !value.empty() && searchKey(value).find(queryKey) != std::string::npos;
}

std::string titleFromPath(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string_view::npos ? 0 : slash + 1;
  size_t end = path.find_last_of('.');
  if (end == std::string_view::npos || end < start) end = path.size();
  return std::string(path.substr(start, end - start));
}

std::string_view basenameFromPath(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  return path.substr(slash == std::string_view::npos ? 0 : slash + 1);
}

uint64_t searchResultKey(const std::string_view path) {
  const size_t slash = path.find_last_of('/');
  const std::string normalized = searchKey(path.substr(slash == std::string_view::npos ? 0 : slash + 1));
  uint64_t hash = 14695981039346656037ull;
  for (const unsigned char c : normalized) {
    hash ^= c;
    hash *= 1099511628211ull;
  }
  return hash;
}

bool containsId(const std::vector<int>& ids, const int id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

int suggestionMatchRank(const std::string& candidateKey, const std::string& queryKey, size_t& matchPosition) {
  matchPosition = 0;
  if (candidateKey == queryKey) return 0;
  if (candidateKey.size() >= queryKey.size() && candidateKey.compare(0, queryKey.size(), queryKey) == 0) return 1;

  const size_t found = candidateKey.find(queryKey);
  if (found == std::string::npos) return -1;
  matchPosition = found;
  if (found > 0 && candidateKey[found - 1] == ' ') return 2;
  return 3;
}

int suggestionKindRank(const LibraryBookSuggestionKind kind) {
  switch (kind) {
    case LibraryBookSuggestionKind::Author:
      return 0;
    case LibraryBookSuggestionKind::Series:
      return 1;
    case LibraryBookSuggestionKind::Title:
    default:
      return 2;
  }
}

struct RankedSuggestion {
  LibraryBookSuggestion suggestion;
  std::string key;
  int rank = 0;
  size_t matchPosition = 0;
};

struct CachedSuggestionCandidate {
  LibraryBookSuggestion suggestion;
  std::string key;
};

struct SuggestionQueryCache {
  std::string queryKey;
  std::vector<CachedSuggestionCandidate> matches;
  bool catalogAvailable = false;
};

SuggestionQueryCache& suggestionQueryCache() {
  static SuggestionQueryCache cache;
  return cache;
}

bool suggestionLess(const RankedSuggestion& left, const RankedSuggestion& right) {
  if (left.rank != right.rank) return left.rank < right.rank;
  if (left.matchPosition != right.matchPosition) return left.matchPosition < right.matchPosition;
  const int leftKind = suggestionKindRank(left.suggestion.kind);
  const int rightKind = suggestionKindRank(right.suggestion.kind);
  if (leftKind != rightKind) return leftKind < rightKind;
  if (left.key.size() != right.key.size()) return left.key.size() < right.key.size();
  return left.key < right.key;
}

void considerSuggestion(std::vector<RankedSuggestion>& suggestions, const std::string_view value,
                        const LibraryBookSuggestionKind kind, const std::string& queryKey,
                        const size_t maxSuggestions) {
  if (value.empty() || maxSuggestions == 0) return;
  const std::string candidateKey = searchKey(value);
  size_t matchPosition = 0;
  const int rank = suggestionMatchRank(candidateKey, queryKey, matchPosition);
  if (rank < 0) return;

  RankedSuggestion candidate{{std::string(value), kind}, candidateKey, rank, matchPosition};

  const auto duplicate = std::find_if(suggestions.begin(), suggestions.end(), [&candidateKey](const auto& item) {
    return item.key == candidateKey;
  });
  if (duplicate != suggestions.end()) {
    if (suggestionLess(candidate, *duplicate)) {
      *duplicate = std::move(candidate);
      std::sort(suggestions.begin(), suggestions.end(), suggestionLess);
    }
    return;
  }

  suggestions.push_back(std::move(candidate));
  std::sort(suggestions.begin(), suggestions.end(), suggestionLess);
  if (suggestions.size() > maxSuggestions) suggestions.pop_back();
}

void collectSuggestionCandidate(std::vector<CachedSuggestionCandidate>& candidates, const std::string_view value,
                                const LibraryBookSuggestionKind kind, const std::string& queryKey) {
  if (value.empty() || candidates.size() >= kMaxCachedSuggestionCandidates) return;
  const std::string candidateKey = searchKey(value);
  size_t matchPosition = 0;
  if (suggestionMatchRank(candidateKey, queryKey, matchPosition) < 0) return;

  const auto duplicate = std::find_if(candidates.begin(), candidates.end(), [&candidateKey](const auto& candidate) {
    return candidate.key == candidateKey;
  });
  if (duplicate != candidates.end()) {
    if (suggestionKindRank(kind) < suggestionKindRank(duplicate->suggestion.kind)) {
      duplicate->suggestion = {std::string(value), kind};
    }
    return;
  }
  candidates.push_back({{std::string(value), kind}, candidateKey});
}
}  // namespace

LibraryBookInfo LibraryBookInfo::load(const std::string& devicePath, const std::string& titleHint,
                                      const std::string& authorHint) {
  LibraryBookInfo info;
  FsFile file;
  if (!Storage.openFileForRead("BOOKINFO", LibraryInsights::CATALOG_PATH, file)) return info;

  std::string line;
  line.reserve(768);
  CatalogLineReader reader(file);
  int version = 0;
  if (!readValidHeader(reader, version, line)) {
    file.close();
    return info;
  }
  info.catalogAvailable = true;

  std::array<std::string_view, 10> fields{};
  const size_t headerFieldCount = splitFields(line, fields);
  int authorCount = 0;
  int seriesCount = 0;
  int genreCount = 0;
  int spiceCount = 0;
  if (headerFieldCount != 7 || !parseInt(fields[2], authorCount) || !parseInt(fields[3], seriesCount) ||
      !parseInt(fields[4], genreCount) || !parseInt(fields[5], spiceCount) || authorCount < 0 ||
      authorCount > kMaxCatalogAuthors || seriesCount < 0 || seriesCount > kMaxCatalogSeries || genreCount < 0 ||
      genreCount > kMaxCatalogGenres || spiceCount < 0 || spiceCount > kMaxCatalogSpiceLevels) {
    LOG_ERR("BOOKINFO", "Invalid library catalog dimensions");
    file.close();
    return info;
  }

  // Keep only four-byte line offsets while scanning. This avoids the old
  // second pass over the catalog without retaining hundreds of metadata names.
  std::vector<uint32_t> authorOffsets(static_cast<size_t>(authorCount), 0);
  std::vector<uint32_t> seriesOffsets(static_cast<size_t>(seriesCount), 0);
  std::vector<uint32_t> genreOffsets(static_cast<size_t>(genreCount), 0);
  std::vector<uint32_t> spiceOffsets(static_cast<size_t>(spiceCount), 0);

  int authorId = -1;
  int seriesId = -1;
  int genreId = -1;
  int spiceId = -1;
  int hintedAuthorId = -1;
  int bestMatchRank = 0;
  const std::string titleHintKey = searchKey(titleHint);
  const std::string authorHintKey = searchKey(authorHint);
  while (reader.available()) {
    const uint32_t lineOffset = reader.position();
    if (!reader.readLine(line)) break;
    const size_t count = splitFields(line, fields);
    if (count >= 3 && fields[0].size() == 1 && fields[0] != "B") {
      int id = -1;
      if (!parseInt(fields[1], id) || id < 0) continue;
      std::vector<uint32_t>* offsets = nullptr;
      switch (fields[0][0]) {
        case 'A':
          offsets = &authorOffsets;
          if (!authorHintKey.empty() && searchKey(fields[2]) == authorHintKey) hintedAuthorId = id;
          break;
        case 'S':
          offsets = &seriesOffsets;
          break;
        case 'G':
          offsets = &genreOffsets;
          break;
        case 'P':
          offsets = &spiceOffsets;
          break;
        default:
          break;
      }
      if (offsets && static_cast<size_t>(id) < offsets->size()) (*offsets)[static_cast<size_t>(id)] = lineOffset;
      continue;
    }
    if (fields[0] != "B" || count < 8) continue;

    int candidateAuthorId = -1;
    int candidateSeriesId = -1;
    int candidateGenreId = -1;
    int candidateSpiceId = -1;
    if (!parseInt(fields[2], candidateAuthorId) || !parseInt(fields[3], candidateSeriesId) ||
        !parseInt(fields[4], candidateGenreId) || !parseInt(fields[5], candidateSpiceId)) {
      continue;
    }
    const bool exactPath = fields[7] == devicePath;
    const bool basenameMatch = basenameFromPath(fields[7]) == basenameFromPath(devicePath);
    const std::string candidateTitle =
        version >= 2 && count >= 9 && !fields[8].empty() ? std::string(fields[8]) : titleFromPath(fields[7]);
    const bool titleMatch = !titleHintKey.empty() && searchKey(candidateTitle) == titleHintKey &&
                            (authorHintKey.empty() || hintedAuthorId < 0 || candidateAuthorId == hintedAuthorId);
    const int matchRank = exactPath ? 3 : (basenameMatch ? 2 : (titleMatch ? 1 : 0));
    if (matchRank <= bestMatchRank) continue;

    authorId = candidateAuthorId;
    seriesId = candidateSeriesId;
    genreId = candidateGenreId;
    spiceId = candidateSpiceId;
    info.seriesIndex = std::string(fields[6]);
    if (version >= 2 && count >= 10) {
      info.title = std::string(fields[8]);
      info.description = std::string(fields[9]);
    }
    info.found = true;
    bestMatchRank = matchRank;
    if (exactPath) break;
  }
  if (!info.found) {
    file.close();
    return info;
  }

  const auto readNameAt = [&](const std::vector<uint32_t>& offsets, const int id, std::string& value) {
    if (id < 0 || static_cast<size_t>(id) >= offsets.size()) return;
    const uint32_t offset = offsets[static_cast<size_t>(id)];
    if (offset == 0 || !reader.seek(offset) || !reader.readLine(line)) return;
    const size_t count = splitFields(line, fields);
    if (count >= 3) value = std::string(fields[2]);
  };
  readNameAt(authorOffsets, authorId, info.author);
  readNameAt(seriesOffsets, seriesId, info.series);
  readNameAt(genreOffsets, genreId, info.genre);
  readNameAt(spiceOffsets, spiceId, info.spice);
  file.close();
  return info;
}

LibraryBookSearchResponse LibraryBookInfo::search(const std::string& query, const size_t maxResults) {
  LibraryBookSearchResponse response;
  const std::string queryKey = searchKey(query);
  if (queryKey.empty()) return response;

  FsFile file;
  if (!Storage.openFileForRead("BOOKSEARCH", LibraryInsights::CATALOG_PATH, file)) return response;

  std::string line;
  line.reserve(768);
  CatalogLineReader reader(file);
  int version = 0;
  if (!readValidHeader(reader, version, line)) {
    file.close();
    return response;
  }
  response.catalogAvailable = true;

  std::array<std::string_view, 10> fields{};
  const size_t headerFieldCount = splitFields(line, fields);
  int authorCount = 0;
  int seriesCount = 0;
  if (headerFieldCount != 7 || !parseInt(fields[2], authorCount) || !parseInt(fields[3], seriesCount) ||
      authorCount < 0 || authorCount > kMaxCatalogAuthors || seriesCount < 0 || seriesCount > kMaxCatalogSeries) {
    LOG_ERR("BOOKSEARCH", "Invalid library catalog dimensions");
    file.close();
    return response;
  }

  std::vector<int> matchingAuthorIds;
  std::vector<int> matchingSeriesIds;
  std::vector<int> resultAuthorIds;
  std::vector<int> resultSeriesIds;
  std::vector<uint64_t> seenBooks;
  matchingAuthorIds.reserve(16);
  matchingSeriesIds.reserve(16);
  resultAuthorIds.reserve(std::min<size_t>(maxResults, 32));
  resultSeriesIds.reserve(std::min<size_t>(maxResults, 32));
  seenBooks.reserve(256);
  response.books.reserve(std::min<size_t>(maxResults, 32));
  std::vector<uint32_t> authorOffsets(static_cast<size_t>(authorCount), 0);
  std::vector<uint32_t> seriesOffsets(static_cast<size_t>(seriesCount), 0);

  while (reader.available()) {
    const uint32_t lineOffset = reader.position();
    if (!reader.readLine(line)) break;
    const size_t count = splitFields(line, fields);
    if (count < 3 || fields[0].size() != 1) continue;

    int id = -1;
    if (fields[0] == "A") {
      if (parseInt(fields[1], id)) {
        if (id >= 0 && static_cast<size_t>(id) < authorOffsets.size()) authorOffsets[static_cast<size_t>(id)] = lineOffset;
        if (matchesSearch(fields[2], queryKey)) matchingAuthorIds.push_back(id);
      }
      continue;
    }
    if (fields[0] == "S") {
      if (parseInt(fields[1], id)) {
        if (id >= 0 && static_cast<size_t>(id) < seriesOffsets.size()) seriesOffsets[static_cast<size_t>(id)] = lineOffset;
        if (matchesSearch(fields[2], queryKey)) matchingSeriesIds.push_back(id);
      }
      continue;
    }
    if (fields[0] != "B" || count < 8) continue;

    int authorId = -1;
    int seriesId = -1;
    if (!parseInt(fields[2], authorId) || !parseInt(fields[3], seriesId)) continue;

    const std::string title = version >= 2 && count >= 9 && !fields[8].empty() ? std::string(fields[8])
                                                                               : titleFromPath(fields[7]);
    if (!matchesSearch(title, queryKey) && !containsId(matchingAuthorIds, authorId) &&
        !containsId(matchingSeriesIds, seriesId) && !matchesSearch(fields[7], queryKey)) {
      continue;
    }

    const uint64_t bookKey = searchResultKey(fields[7]);
    if (std::find(seenBooks.begin(), seenBooks.end(), bookKey) != seenBooks.end()) continue;
    seenBooks.push_back(bookKey);
    ++response.totalMatches;

    if (response.books.size() >= maxResults) {
      response.truncated = true;
      continue;
    }
    response.books.push_back({std::string(fields[7]), title, {}, {}});
    resultAuthorIds.push_back(authorId);
    resultSeriesIds.push_back(seriesId);
  }

  // Dictionary rows precede books. Keep their four-byte offsets during the
  // search pass, then seek directly to the handful of labels needed by the
  // result set instead of reopening and rereading the whole catalog.
  std::vector<std::pair<int, std::string>> resolvedAuthors;
  std::vector<std::pair<int, std::string>> resolvedSeries;
  resolvedAuthors.reserve(std::min<size_t>(resultAuthorIds.size(), 32));
  resolvedSeries.reserve(std::min<size_t>(resultSeriesIds.size(), 32));
  const auto resolveName = [&](const std::vector<uint32_t>& offsets, const int id,
                               std::vector<std::pair<int, std::string>>& resolved) -> std::string {
    const auto cached = std::find_if(resolved.begin(), resolved.end(), [id](const auto& item) {
      return item.first == id;
    });
    if (cached != resolved.end()) return cached->second;
    if (id < 0 || static_cast<size_t>(id) >= offsets.size()) return {};
    const uint32_t offset = offsets[static_cast<size_t>(id)];
    if (offset == 0 || !reader.seek(offset) || !reader.readLine(line)) return {};
    const size_t count = splitFields(line, fields);
    if (count < 3) return {};
    resolved.emplace_back(id, std::string(fields[2]));
    return resolved.back().second;
  };
  for (size_t i = 0; i < response.books.size(); ++i) {
    response.books[i].author = resolveName(authorOffsets, resultAuthorIds[i], resolvedAuthors);
    response.books[i].series = resolveName(seriesOffsets, resultSeriesIds[i], resolvedSeries);
  }
  file.close();

  std::sort(response.books.begin(), response.books.end(), [](const auto& left, const auto& right) {
    const std::string leftTitle = searchKey(left.title);
    const std::string rightTitle = searchKey(right.title);
    if (leftTitle != rightTitle) return leftTitle < rightTitle;
    return searchKey(left.author) < searchKey(right.author);
  });
  return response;
}

LibraryBookSuggestionResponse LibraryBookInfo::suggest(const std::string& query, const size_t maxSuggestions) {
  LibraryBookSuggestionResponse response;
  const std::string queryKey = searchKey(query);
  if (queryKey.size() < 2 || maxSuggestions == 0) return response;

  auto& cache = suggestionQueryCache();
  const bool refineExisting = cache.catalogAvailable && !cache.queryKey.empty() &&
                              queryKey.size() >= cache.queryKey.size() &&
                              queryKey.compare(0, cache.queryKey.size(), cache.queryKey) == 0;
  const unsigned long startedAt = millis();
  if (refineExisting) {
    cache.matches.erase(
        std::remove_if(cache.matches.begin(), cache.matches.end(), [&queryKey](const auto& candidate) {
          size_t matchPosition = 0;
          return suggestionMatchRank(candidate.key, queryKey, matchPosition) < 0;
        }),
        cache.matches.end());
  } else {
    cache.matches.clear();
    cache.catalogAvailable = false;

    FsFile file;
    if (!Storage.openFileForRead("BOOKSUGGEST", LibraryInsights::CATALOG_PATH, file)) return response;

    std::string line;
    line.reserve(768);
    CatalogLineReader reader(file);
    int version = 0;
    if (!readValidHeader(reader, version, line)) {
      file.close();
      return response;
    }
    cache.catalogAvailable = true;

    std::array<std::string_view, 10> fields{};
    while (reader.readLine(line)) {
      const size_t count = splitFields(line, fields);
      if (count < 3 || fields[0].size() != 1) continue;

      if (fields[0] == "A") {
        collectSuggestionCandidate(cache.matches, fields[2], LibraryBookSuggestionKind::Author, queryKey);
        continue;
      }
      if (fields[0] == "S") {
        collectSuggestionCandidate(cache.matches, fields[2], LibraryBookSuggestionKind::Series, queryKey);
        continue;
      }
      if (fields[0] != "B" || count < 8) continue;

      if (version >= 2 && count >= 9 && !fields[8].empty()) {
        collectSuggestionCandidate(cache.matches, fields[8], LibraryBookSuggestionKind::Title, queryKey);
      } else {
        const std::string title = titleFromPath(fields[7]);
        collectSuggestionCandidate(cache.matches, title, LibraryBookSuggestionKind::Title, queryKey);
      }
    }
    file.close();
  }
  cache.queryKey = queryKey;
  response.catalogAvailable = cache.catalogAvailable;

  std::vector<RankedSuggestion> ranked;
  ranked.reserve(maxSuggestions + 1);
  for (const auto& candidate : cache.matches) {
    considerSuggestion(ranked, candidate.suggestion.value, candidate.suggestion.kind, queryKey, maxSuggestions);
  }

  response.suggestions.reserve(ranked.size());
  for (auto& item : ranked) response.suggestions.push_back(std::move(item.suggestion));
  LOG_DBG("BOOKSUGGEST", "%s query '%s' from %u candidate(s) in %lums", refineExisting ? "Refined" : "Scanned",
          query.c_str(), static_cast<unsigned>(cache.matches.size()), millis() - startedAt);
  return response;
}

bool LibraryBookInfo::hasCatalog() { return Storage.existsForRead(LibraryInsights::CATALOG_PATH); }

void LibraryBookInfo::releaseSuggestionCache() {
  auto& cache = suggestionQueryCache();
  std::string{}.swap(cache.queryKey);
  std::vector<CachedSuggestionCandidate>{}.swap(cache.matches);
  cache.catalogAvailable = false;
}
