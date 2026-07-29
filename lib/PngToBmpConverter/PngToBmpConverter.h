#pragma once

#include <HalStorage.h>

class Print;

class PngToBmpConverter {
 public:
  using CancelCallback = bool (*)(void*);

 private:
  static bool pngFileToBmpStreamInternal(FsFile& pngFile, Print& bmpOut, int targetWidth, int targetHeight, bool oneBit,
                                         bool crop = true, bool adaptiveContain = false,
                                         CancelCallback shouldCancel = nullptr, void* cancelContext = nullptr);

 public:
  static bool pngFileToBmpStream(FsFile& pngFile, Print& bmpOut, bool crop = true);
  static bool pngFileToBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                         bool adaptiveContain = false, CancelCallback shouldCancel = nullptr,
                                         void* cancelContext = nullptr);
  static bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight,
                                             bool adaptiveContain = false, CancelCallback shouldCancel = nullptr,
                                             void* cancelContext = nullptr);
};
