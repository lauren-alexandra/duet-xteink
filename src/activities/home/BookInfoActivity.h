#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "LibraryBookInfo.h"
#include "activities/Activity.h"

class BookInfoActivity final : public Activity {
 public:
  BookInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string bookPath,
                   std::string fallbackTitle, std::string thumbnailPath)
      : Activity("BookInfo", renderer, mappedInput),
        bookPath(std::move(bookPath)),
        fallbackTitle(std::move(fallbackTitle)),
        thumbnailPath(std::move(thumbnailPath)) {}

  void onEnter() override;
  void loop() override;
 void render(RenderLock&&) override;

 private:
  struct CachedCoverBitmap {
    std::unique_ptr<uint8_t[]> rows;
    size_t rowDataSize = 0;
    int width = 0;
    int height = 0;
    int rowBytes = 0;
    bool topDown = false;
    uint8_t blackPaletteIndex = 0;

    bool isReady() const { return rows != nullptr && width > 0 && height > 0 && rowBytes > 0; }
  };

  void loadDetails();
  void loadCachedCover();
  void selectCachedThumbnail();

  std::string bookPath;
  std::string fallbackTitle;
  std::string thumbnailPath;
  std::string cachePath;
  LibraryBookInfo info;
  std::string statusText;
  CachedCoverBitmap cachedCover;
  bool detailsLoaded = false;
  bool coverLoadAttempted = false;
  unsigned long detailsLoadAt = 0UL;
};
