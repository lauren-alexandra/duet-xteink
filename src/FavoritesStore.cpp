#include "FavoritesStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>

#include "JsonSettingsIO.h"
#include "RecentBooksStore.h"

namespace {
constexpr char FAVORITES_FILE_JSON[] = DUET_STATE_ROOT_PATH "/favorites.json";

std::string fallbackTitle(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const std::string filename = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = filename.rfind('.');
  return dot == std::string::npos ? filename : filename.substr(0, dot);
}
}  // namespace

FavoritesStore FavoritesStore::instance;

int FavoritesStore::findBookIndex(const std::string& path) const {
  const auto found = std::find_if(favoriteBooks.begin(), favoriteBooks.end(),
                                  [&path](const FavoriteBook& book) { return book.path == path; });
  return found == favoriteBooks.end() ? -1 : static_cast<int>(found - favoriteBooks.begin());
}

void FavoritesStore::normalize() {
  std::vector<FavoriteBook> normalized;
  normalized.reserve(std::min(favoriteBooks.size(), MAX_FAVORITES));
  for (auto& book : favoriteBooks) {
    if (book.path.empty()) continue;
    auto found = std::find_if(normalized.begin(), normalized.end(),
                              [&book](const FavoriteBook& existing) { return existing.path == book.path; });
    if (found == normalized.end()) {
      if (book.title.empty()) book.title = fallbackTitle(book.path);
      normalized.push_back(std::move(book));
      if (normalized.size() >= MAX_FAVORITES) break;
      continue;
    }
    if (found->title.empty()) found->title = book.title;
    if (found->author.empty()) found->author = book.author;
    if (found->coverBmpPath.empty()) found->coverBmpPath = book.coverBmpPath;
  }
  favoriteBooks = std::move(normalized);
}

FavoriteBook FavoritesStore::getDataFromBook(const std::string& path) const {
  const auto& recents = RECENT_BOOKS.getBooks();
  const auto recent =
      std::find_if(recents.begin(), recents.end(), [&path](const RecentBook& book) { return book.path == path; });
  if (recent != recents.end()) {
    return {recent->path, recent->title.empty() ? fallbackTitle(path) : recent->title, recent->author,
            recent->coverBmpPath};
  }

  const RecentBook loaded = RECENT_BOOKS.getDataFromBook(path);
  return {path, loaded.title.empty() ? fallbackTitle(path) : loaded.title, loaded.author, loaded.coverBmpPath};
}

bool FavoritesStore::addBook(const std::string& path, const std::string& title, const std::string& author,
                             const std::string& coverBmpPath) {
  if (path.empty()) return false;
  const int existingIndex = findBookIndex(path);
  if (existingIndex >= 0) {
    auto& existing = favoriteBooks[existingIndex];
    if (!title.empty()) existing.title = title;
    if (!author.empty()) existing.author = author;
    if (!coverBmpPath.empty()) existing.coverBmpPath = coverBmpPath;
    return saveToFile();
  }
  if (favoriteBooks.size() >= MAX_FAVORITES) return false;

  FavoriteBook book = getDataFromBook(path);
  if (!title.empty()) book.title = title;
  if (!author.empty()) book.author = author;
  if (!coverBmpPath.empty()) book.coverBmpPath = coverBmpPath;
  favoriteBooks.push_back(std::move(book));
  return saveToFile();
}

bool FavoritesStore::removeBook(const std::string& path) {
  const int index = findBookIndex(path);
  if (index < 0) return false;
  favoriteBooks.erase(favoriteBooks.begin() + index);
  return saveToFile();
}

bool FavoritesStore::toggleBook(const std::string& path) {
  if (isFavorite(path)) {
    removeBook(path);
    return false;
  }
  addBook(path);
  return isFavorite(path);
}

bool FavoritesStore::isFavorite(const std::string& path) const { return findBookIndex(path) >= 0; }

bool FavoritesStore::moveBook(const int fromIndex, const int toIndex) {
  if (fromIndex < 0 || toIndex < 0 || fromIndex >= getCount() || toIndex >= getCount() || fromIndex == toIndex) {
    return false;
  }
  std::swap(favoriteBooks[fromIndex], favoriteBooks[toIndex]);
  return saveToFile();
}

bool FavoritesStore::updatePath(const std::string& oldPath, const std::string& newPath,
                                const std::string& oldCachePath, const std::string& newCachePath) {
  const int index = findBookIndex(oldPath);
  if (index < 0) return true;
  auto& book = favoriteBooks[index];
  book.path = newPath;
  if (!oldCachePath.empty() && !book.coverBmpPath.empty() && book.coverBmpPath.rfind(oldCachePath, 0) == 0) {
    book.coverBmpPath = newCachePath + book.coverBmpPath.substr(oldCachePath.size());
  }
  return saveToFile();
}

bool FavoritesStore::saveToFile() const {
  Storage.mkdir(DUET_STATE_ROOT_PATH "");
  return JsonSettingsIO::saveFavorites(*this, FAVORITES_FILE_JSON);
}

bool FavoritesStore::loadFromFile() {
  if (!Storage.existsForRead(FAVORITES_FILE_JSON)) return false;
  const String json = Storage.readFile(FAVORITES_FILE_JSON);
  if (json.isEmpty() || !JsonSettingsIO::loadFavorites(*this, json.c_str())) return false;
  normalize();
  LOG_DBG("FAV", "Loaded %d favorite book(s)", getCount());
  return true;
}
