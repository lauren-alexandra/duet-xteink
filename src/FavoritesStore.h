#pragma once

#include <string>
#include <vector>

struct FavoriteBook {
  std::string path;
  std::string title;
  std::string author;
  std::string coverBmpPath;
};

class FavoritesStore;
namespace JsonSettingsIO {
bool saveFavorites(const FavoritesStore& store, const char* path);
bool loadFavorites(FavoritesStore& store, const char* json);
}  // namespace JsonSettingsIO

class FavoritesStore {
 public:
  static FavoritesStore& getInstance() { return instance; }

  bool addBook(const std::string& path, const std::string& title = {}, const std::string& author = {},
               const std::string& coverBmpPath = {});
  bool removeBook(const std::string& path);
  bool toggleBook(const std::string& path);
  bool isFavorite(const std::string& path) const;
  bool moveBook(int fromIndex, int toIndex);
  bool updatePath(const std::string& oldPath, const std::string& newPath, const std::string& oldCachePath,
                  const std::string& newCachePath);

  const std::vector<FavoriteBook>& getBooks() const { return favoriteBooks; }
  int getCount() const { return static_cast<int>(favoriteBooks.size()); }

  bool saveToFile() const;
  bool loadFromFile();
  FavoriteBook getDataFromBook(const std::string& path) const;

 private:
  static FavoritesStore instance;
  static constexpr size_t MAX_FAVORITES = 200;
  std::vector<FavoriteBook> favoriteBooks;

  int findBookIndex(const std::string& path) const;
  void normalize();

  friend bool JsonSettingsIO::saveFavorites(const FavoritesStore&, const char*);
  friend bool JsonSettingsIO::loadFavorites(FavoritesStore&, const char*);
};

#define FAVORITES FavoritesStore::getInstance()
