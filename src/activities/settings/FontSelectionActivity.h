#pragma once

#include <SdCardFontRegistry.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class FontSelectionActivity final : public Activity {
 public:
  explicit FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 const SdCardFontRegistry* registry);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  void applyFontEntryForPreview(int index);
  void applyOriginalFontForPreview();
  void restoreOriginalFontSettings();
  int findCurrentFontRow(const char* sdFontFamilyName, uint8_t fontFamily) const;
  int moveSelectable(int currentIndex, int direction, int selectableSteps) const;
  void renderPreviewPane(int top, int height, int fontId, const char* fontName);
  void renderSampleBlock(int top, int height, int fontId, const char* roleLabel, const char* fontName,
                         uint8_t pointSize, bool showStyleRow) const;
  bool cacheOriginalPreview(int top, int height);

  struct FontEntry {
    std::string name;
    bool isBuiltin = false;
    bool isHeader = false;
    uint8_t settingIndex = 0;
    uint8_t category = 0;
  };

  const SdCardFontRegistry* registry_;
  ButtonNavigator buttonNavigator_;
  std::vector<FontEntry> fonts_;
  int selectedIndex_ = 0;
  int previewFontIndex_ = 0;
  uint8_t originalFontFamily_ = 0;
  uint8_t originalFontSize_ = 0;
  uint8_t originalFontPointSize_ = 14;
  uint8_t previewPointSize_ = 14;
  int originalFontLineHeight_ = 0;
  uint8_t originalSdFontPointSize_ = 0;
  char originalSdFontFamilyName_[64] = {};
  std::string originalFontName_;
  std::vector<uint8_t> originalPreviewBuffer_;
  int originalPreviewTop_ = 0;
  int originalPreviewHeight_ = 0;
  int originalPreviewTextLines_ = 2;

  ThemeMetrics metrics_ = {};
  int afterHeader = 0;
  int bottomReserved = 0;
  int usableHeight = 0;
  int previewHeight = 0;
};
