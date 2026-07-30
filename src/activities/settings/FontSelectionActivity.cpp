#include "FontSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr uint8_t INVALID_STORED_FONT_SIZE = 0xFF;
constexpr const char* ELLIPSIS_UTF8 = "\xe2\x80\xa6";
constexpr int FONT_PREVIEW_MAX_TEXT_LINES = 4;
constexpr int FONT_PREVIEW_SECTION_GAP = 2;
constexpr int FONT_PREVIEW_PANE_GAP = 3;
constexpr int FONT_PREVIEW_MIN_PERCENT = 34;

enum FontCategory : uint8_t {
  CATEGORY_SERIF = 0,
  CATEGORY_SANS_SERIF,
  CATEGORY_MONOSPACE,
  CATEGORY_ACCESSIBILITY,
  CATEGORY_HANDWRITTEN_SCRIPT,
  CATEGORY_DECORATIVE,
  CATEGORY_COUNT,
};

constexpr const char* CATEGORY_LABELS[CATEGORY_COUNT] = {
    "Serif", "Sans Serif", "Mono / Typewriter", "Accessibility", "Handwritten / Script", "Decorative",
};

std::string normalizedFamilyKey(const std::string& name) {
  std::string key;
  key.reserve(name.size());
  for (const unsigned char ch : name) {
    if (std::isalnum(ch)) key.push_back(static_cast<char>(std::tolower(ch)));
  }
  return key;
}

bool keyMatchesAny(const std::string& key, const std::initializer_list<const char*> names) {
  for (const char* name : names) {
    if (key == name) return true;
  }
  return false;
}

uint8_t pickerPreviewPointSize(const std::string& familyName) {
  return normalizedFamilyKey(familyName).find("dyslex") != std::string::npos ? 14 : 16;
}

uint8_t categorizeFamily(const std::string& name) {
  const std::string key = normalizedFamilyKey(name);

  if (key.find("dyslex") != std::string::npos || key.find("disleks") != std::string::npos ||
      key.find("legible") != std::string::npos || key.find("hyperlegible") != std::string::npos ||
      keyMatchesAny(key, {"lexend", "lexenddeca", "readexpro", "andika"})) {
    return CATEGORY_ACCESSIBILITY;
  }

  if (keyMatchesAny(key,
                    {"alexbrush", "allura", "applechancery", "bradleyhand", "caveat", "comicneue", "dancingscript",
                     "greatvibes", "italianno", "kaushanscript", "lobster", "pacifico", "parisienne", "patrickhand",
                     "petitformalscript", "pinyonscript", "sacramento", "tangerine", "yellowtail"})) {
    return CATEGORY_HANDWRITTEN_SCRIPT;
  }

  if (keyMatchesAny(key, {"courierprime", "ibmplexmono", "sourcecodepro", "specialelite", "texgyrecursor"})) {
    return CATEGORY_MONOSPACE;
  }

  if (keyMatchesAny(key, {"abrilfatface", "bungee", "cinzel", "frederickathegreat", "herculanum", "monoton", "rye"})) {
    return CATEGORY_DECORATIVE;
  }

  if (key.find("sans") != std::string::npos ||
      keyMatchesAny(key, {"archivonarrow", "arialrounded", "comfortaa", "firasans", "inter", "lato", "nunjito",
                          "nunito", "nvancizarsans", "nvjost", "oswald", "quicksand", "sciencegothic", "skia",
                          "texgyreadventor", "texgyreheros", "texgyreheroscondensed", "ubuntu", "ysabeau"})) {
    return CATEGORY_SANS_SERIF;
  }

  return CATEGORY_SERIF;
}

uint8_t closestSizeIndex(const std::vector<uint8_t>& sizes, const uint8_t targetPointSize) {
  if (sizes.empty()) return 0;

  uint8_t bestIndex = 0;
  uint8_t bestDiff = UINT8_MAX;
  for (size_t i = 0; i < sizes.size(); i++) {
    const uint8_t size = sizes[i];
    const uint8_t diff = size > targetPointSize ? size - targetPointSize : targetPointSize - size;
    if (diff < bestDiff || (diff == bestDiff && size < sizes[bestIndex])) {
      bestIndex = static_cast<uint8_t>(i);
      bestDiff = diff;
    }
  }
  return bestIndex;
}

uint8_t closestBuiltinStoredSize(const uint8_t targetPointSize) {
  uint8_t bestStored = 0;
  uint8_t bestPointSize = 0;
  uint8_t bestDiff = UINT8_MAX;

  for (uint8_t i = 0; i < CrossPointSettings::FONT_SIZE_COUNT; i++) {
    const auto size = static_cast<CrossPointSettings::FONT_SIZE>(i);
    const uint8_t stored = CrossPointSettings::getStoredReaderFontSize(size);
    if (stored == INVALID_STORED_FONT_SIZE) continue;

    const uint8_t pointSize = CrossPointSettings::getReaderFontPointSize(size);
    const uint8_t diff = pointSize > targetPointSize ? pointSize - targetPointSize : targetPointSize - pointSize;
    if (diff < bestDiff || (diff == bestDiff && pointSize < bestPointSize)) {
      bestStored = stored;
      bestPointSize = pointSize;
      bestDiff = diff;
    }
  }
  return bestStored;
}

uint8_t currentFontPointSize(const SdCardFontRegistry* registry) {
  if (registry && SETTINGS.sdFontFamilyName[0] != '\0') {
    const SdCardFontFamilyInfo* family = registry->findFamily(SETTINGS.sdFontFamilyName);
    if (family) {
      const std::vector<uint8_t> sizes = family->availableSizes();
      if (!sizes.empty()) {
        if (SETTINGS.sdFontPointSize != 0) {
          return sizes[closestSizeIndex(sizes, SETTINGS.sdFontPointSize)];
        }
        const uint8_t index =
            SETTINGS.fontSize < sizes.size() ? SETTINGS.fontSize : static_cast<uint8_t>(sizes.size() - 1);
        return sizes[index];
      }
    }
  }
  return CrossPointSettings::getReaderFontPointSize(SETTINGS.getEffectiveReaderFontSize());
}

std::vector<std::string> fontPreviewLines(GfxRenderer& renderer, const int fontId, const int width,
                                          const char* fontName) {
  if (fontId == 0 || width <= 0) return {};
  // SD-card font advance metrics can be slightly tighter than their visible
  // glyph spacing. OpenDyslexic needs a larger safety margin than the other
  // families or its final words visibly run past the measured line width.
  const std::string familyKey = normalizedFamilyKey(fontName ? fontName : "");
  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (familyKey.find("dyslex") != std::string::npos &&
      strcmp(previewText, "The quick brown fox jumps over the lazy dog.") == 0) {
    return {"The quick brown", "fox jumps over", "the lazy dog."};
  }
  const int widthPercent = familyKey.find("dyslex") != std::string::npos ? 65 : 80;
  const int wrapWidth = std::max(1, width * widthPercent / 100);
  return renderer.wrappedText(fontId, previewText, wrapWidth, FONT_PREVIEW_MAX_TEXT_LINES);
}

}  // namespace

FontSelectionActivity::FontSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                             const SdCardFontRegistry* registry)
    : Activity("FontSelect", renderer, mappedInput), registry_(registry) {}

void FontSelectionActivity::onEnter() {
  Activity::onEnter();

  // Get metrics and calculate layout dimensions
  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;
  // Keep the list useful even at the largest preview size. render() expands
  // this only when the two-line comparison needs it.
  previewHeight = std::max(previewHeight, usableHeight * FONT_PREVIEW_MIN_PERCENT / 100);

  originalFontFamily_ = SETTINGS.fontFamily;
  originalFontSize_ = SETTINGS.fontSize;
  originalSdFontPointSize_ = SETTINGS.sdFontPointSize;
  strncpy(originalSdFontFamilyName_, SETTINGS.sdFontFamilyName, sizeof(originalSdFontFamilyName_) - 1);
  originalSdFontFamilyName_[sizeof(originalSdFontFamilyName_) - 1] = '\0';
  originalFontPointSize_ = currentFontPointSize(registry_);
  previewPointSize_ = 16;

  std::vector<FontEntry> familyEntries;
  familyEntries.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));

  const std::string lexendName = I18N.get(StrId::STR_LEXEND_DECA);
  const std::string bitterName = I18N.get(StrId::STR_BITTER);
  familyEntries.push_back({lexendName, true, false, 0, categorizeFamily(lexendName)});
  const bool nvBitterInstalled = registry_ && registry_->findFamily("NV Bitter") != nullptr;
  const bool builtinBitterIsCurrent = originalSdFontFamilyName_[0] == '\0' && originalFontFamily_ == 1;
  if (!nvBitterInstalled || builtinBitterIsCurrent) {
    familyEntries.push_back({bitterName, true, false, 1, categorizeFamily(bitterName)});
  }

  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      familyEntries.push_back({families[i].name, false, false,
                               static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i),
                               categorizeFamily(families[i].name)});
    }
  }

  std::sort(familyEntries.begin(), familyEntries.end(), [](const FontEntry& left, const FontEntry& right) {
    if (left.category != right.category) return left.category < right.category;
    const std::string leftKey = normalizedFamilyKey(left.name);
    const std::string rightKey = normalizedFamilyKey(right.name);
    return leftKey == rightKey ? left.name < right.name : leftKey < rightKey;
  });

  fonts_.clear();
  fonts_.reserve(familyEntries.size() + CATEGORY_COUNT);
  for (uint8_t category = 0; category < CATEGORY_COUNT; category++) {
    const auto first = std::find_if(familyEntries.begin(), familyEntries.end(),
                                    [category](const FontEntry& entry) { return entry.category == category; });
    if (first == familyEntries.end()) continue;
    fonts_.push_back({CATEGORY_LABELS[category], false, true, 0, category});
    for (const auto& entry : familyEntries) {
      if (entry.category == category) fonts_.push_back(entry);
    }
  }

  selectedIndex_ = findCurrentFontRow(SETTINGS.sdFontFamilyName, SETTINGS.fontFamily);
  previewFontIndex_ = selectedIndex_;
  originalFontName_ = fonts_[selectedIndex_].name;
  previewPointSize_ = pickerPreviewPointSize(originalFontName_);
  originalPreviewBuffer_.clear();
  applyOriginalFontForPreview();
  const int originalFontId = SETTINGS.getReaderFontId();
  originalFontLineHeight_ = renderer.getTextHeight(originalFontId);
  originalPreviewTextLines_ =
      std::clamp(static_cast<int>(fontPreviewLines(renderer, originalFontId,
                                                   renderer.getScreenWidth() - (metrics_.previewPadding * 2),
                                                   originalFontName_.c_str())
                                      .size()),
                 1, FONT_PREVIEW_MAX_TEXT_LINES);

  requestUpdate();
}

void FontSelectionActivity::onExit() { Activity::onExit(); }

#ifdef SIMULATOR
bool FontSelectionActivity::simulatorPreviewFamily(const char* familyName) {
  if (!registry_ || !familyName || familyName[0] == '\0') return false;

  for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
    if (fonts_[i].isHeader || fonts_[i].name != familyName) continue;
    selectedIndex_ = i;
    previewFontIndex_ = i;
    applyFontEntryForPreview(i);
    requestUpdate();
    return true;
  }

  const auto& families = registry_->getFamilies();
  for (int i = 0; i < static_cast<int>(families.size()); i++) {
    if (families[i].name != familyName) continue;
    const int targetIndex = static_cast<int>(fonts_.size());
    fonts_.push_back({families[i].name, false, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i),
                      categorizeFamily(families[i].name)});
    selectedIndex_ = targetIndex;
    previewFontIndex_ = targetIndex;
    applyFontEntryForPreview(targetIndex);
    requestUpdate();
    return true;
  }

  return false;
}

void FontSelectionActivity::simulatorRenderPreviewSpecimen() {
  renderer.clearScreen();
  const int previewFontId = SETTINGS.getReaderFontId();
  const char* previewFontName = (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size()))
                                    ? fonts_[previewFontIndex_].name.c_str()
                                    : nullptr;
  renderSampleBlock(8, 284, previewFontId, "Preview", previewFontName, currentFontPointSize(registry_), true);
  renderer.displayBuffer();
}
#endif

void FontSelectionActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    mappedInput.suppressNextBackRelease();
    restoreOriginalFontSettings();
    finish();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (selectedIndex_ == previewFontIndex_) {
      handleSelection();
    } else {
      previewFontIndex_ = selectedIndex_;
      applyFontEntryForPreview(selectedIndex_);
      requestUpdate();
    }
    return;
  }

  const int listSize = static_cast<int>(fonts_.size());
  const int pageItems =
      UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false, previewHeight + metrics_.verticalSpacing);

  buttonNavigator_.onNextRelease([this] {
    selectedIndex_ = moveSelectable(selectedIndex_, 1, 1);
    requestUpdate();
  });

  buttonNavigator_.onPreviousRelease([this] {
    selectedIndex_ = moveSelectable(selectedIndex_, -1, 1);
    requestUpdate();
  });

  buttonNavigator_.onNextContinuous([this, pageItems] {
    selectedIndex_ = moveSelectable(selectedIndex_, 1, pageItems);
    requestUpdate();
  });

  buttonNavigator_.onPreviousContinuous([this, pageItems] {
    selectedIndex_ = moveSelectable(selectedIndex_, -1, pageItems);
    requestUpdate();
  });
}

int FontSelectionActivity::findCurrentFontRow(const char* sdFontFamilyName, const uint8_t fontFamily) const {
  for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
    const FontEntry& entry = fonts_[i];
    if (entry.isHeader) continue;
    if (sdFontFamilyName[0] != '\0') {
      if (!entry.isBuiltin && entry.name == sdFontFamilyName) return i;
    } else if (entry.isBuiltin && entry.settingIndex == fontFamily) {
      return i;
    }
  }

  for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
    if (!fonts_[i].isHeader) return i;
  }
  return 0;
}

int FontSelectionActivity::moveSelectable(int currentIndex, const int direction, const int selectableSteps) const {
  const int listSize = static_cast<int>(fonts_.size());
  if (listSize <= 0 || direction == 0) return currentIndex;

  int index = currentIndex;
  const int steps = std::max(1, selectableSteps);
  for (int step = 0; step < steps; step++) {
    do {
      index = (index + direction + listSize) % listSize;
    } while (fonts_[index].isHeader && index != currentIndex);
  }
  return index;
}

void FontSelectionActivity::applyFontEntryForPreview(const int index) {
  if (index < 0 || index >= static_cast<int>(fonts_.size()) || fonts_[index].isHeader) return;

  const auto& font = fonts_[index];
  const uint8_t targetPointSize = pickerPreviewPointSize(font.name);
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.sdFontPointSize = 0;
    SETTINGS.fontSize = closestBuiltinStoredSize(targetPointSize);
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx >= 0 && sdIdx < static_cast<int>(families.size())) {
      const auto sizes = families[sdIdx].availableSizes();
      SETTINGS.fontSize = closestSizeIndex(sizes, targetPointSize);
      SETTINGS.sdFontPointSize = sizes.empty() ? 0 : sizes[SETTINGS.fontSize];
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  sdFontSystem.ensureLoaded(renderer);
}

void FontSelectionActivity::applyOriginalFontForPreview() {
  SETTINGS.fontFamily = originalFontFamily_;
  if (originalSdFontFamilyName_[0] == '\0') {
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.sdFontPointSize = 0;
    SETTINGS.fontSize = closestBuiltinStoredSize(previewPointSize_);
  } else if (registry_) {
    const SdCardFontFamilyInfo* family = registry_->findFamily(originalSdFontFamilyName_);
    if (family) {
      const std::vector<uint8_t> sizes = family->availableSizes();
      SETTINGS.fontSize = closestSizeIndex(sizes, previewPointSize_);
      SETTINGS.sdFontPointSize = sizes.empty() ? 0 : sizes[SETTINGS.fontSize];
      strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName_, sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  sdFontSystem.ensureLoaded(renderer);
}

void FontSelectionActivity::restoreOriginalFontSettings() {
  SETTINGS.fontFamily = originalFontFamily_;
  SETTINGS.fontSize = originalFontSize_;
  SETTINGS.sdFontPointSize = originalSdFontPointSize_;
  strncpy(SETTINGS.sdFontFamilyName, originalSdFontFamilyName_, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  sdFontSystem.ensureLoaded(renderer);
}

void FontSelectionActivity::handleSelection() {
  const auto& font = fonts_[selectedIndex_];
  if (font.isHeader) return;
  if (font.settingIndex < CrossPointSettings::BUILTIN_FONT_COUNT) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.sdFontPointSize = 0;
    SETTINGS.fontSize = closestBuiltinStoredSize(originalFontPointSize_);
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      const std::vector<uint8_t> sizes = families[sdIdx].availableSizes();
      SETTINGS.fontSize = closestSizeIndex(sizes, originalFontPointSize_);
      SETTINGS.sdFontPointSize = sizes.empty() ? 0 : sizes[SETTINGS.fontSize];
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
    }
  }
  sdFontSystem.ensureLoaded(renderer);
  mappedInput.suppressNextConfirmRelease();
  finish();
}

void FontSelectionActivity::renderSampleBlock(const int top, const int height, const int fontId, const char* roleLabel,
                                              const char* fontName, const uint8_t pointSize,
                                              const bool showStyleRow) const {
  const int left = metrics_.previewPadding;
  const int width = renderer.getScreenWidth() - (metrics_.previewPadding * 2);
  if (width <= 0 || height <= 0) return;

  const int labelFontId = UI_10_FONT_ID;
  const int labelH = renderer.getTextHeight(labelFontId);
  if (fontId == 0) return;

  char labelBuf[128];
  snprintf(labelBuf, sizeof(labelBuf), "%s: %s (%u pt)", roleLabel, fontName ? fontName : "", pointSize);
  const std::string label = renderer.truncatedText(labelFontId, labelBuf, width);
  renderer.drawText(labelFontId, left, top, label.c_str(), true, EpdFontFamily::BOLD);

  const int lineH = renderer.getTextHeight(fontId);
  if (lineH <= 0) return;

  struct StyleSample {
    const char* label;
    EpdFontFamily::Style style;
  };
  constexpr StyleSample styleSamples[] = {
      {"Normal", EpdFontFamily::REGULAR},
      {"Italic", EpdFontFamily::ITALIC},
      {"Bold", EpdFontFamily::BOLD},
  };

  const char* previewText = I18N.get(StrId::STR_FONT_PREVIEW_TEXT);
  if (auto* fcm = renderer.getFontCacheManager()) {
    char prewarmBuf[256];
    snprintf(prewarmBuf, sizeof(prewarmBuf), "%s Aa %s", previewText, ELLIPSIS_UTF8);
    fcm->prewarmCache(fontId, prewarmBuf, showStyleRow ? 0x07 : 0x01);
  }

  int y = top + labelH + FONT_PREVIEW_SECTION_GAP;
  if (showStyleRow) {
    int sampleX = left;
    int specimenCursorX = left;
    constexpr int sampleGap = 5;
    const int sampleFontId = SMALL_FONT_ID;
    const int sampleLineHeight = renderer.getTextHeight(sampleFontId);
    std::array<int, std::size(styleSamples)> specimenX = {};
    std::array<EpdFontFamily::Style, std::size(styleSamples)> specimenStyle = {};
    bool hasSynthetic = false;
    for (size_t i = 0; i < std::size(styleSamples); i++) {
      const auto& sample = styleSamples[i];
      const bool available = sample.style == EpdFontFamily::REGULAR ||
                             renderer.hasDistinctFontStyle(fontId, EpdFontFamily::REGULAR, sample.style);
      const bool synthetic = available && renderer.isSyntheticFontStyle(fontId, sample.style);
      specimenStyle[i] = available ? sample.style : EpdFontFamily::REGULAR;
      std::string displayLabel = sample.label;
      if (synthetic) {
        displayLabel += "*";
        hasSynthetic = true;
      }
      // These are picker labels, not type specimens. Always draw them with the
      // known-good UI face so a family without native italic/bold can never
      // turn its label into question marks.
      const int sampleWidth = renderer.getTextWidth(sampleFontId, displayLabel.c_str(), EpdFontFamily::REGULAR);
      if (sampleX + sampleWidth > left + width) break;
      renderer.drawText(sampleFontId, sampleX, y, displayLabel.c_str(), true, EpdFontFamily::REGULAR);
      // Keep the three actual type specimens compact. Aligning each Aa under
      // its differently sized label made Normal sit much farther from Italic
      // and Bold even though they are one comparison row.
      specimenX[i] = specimenCursorX;
      specimenCursorX += renderer.getTextWidth(fontId, "Aa", specimenStyle[i]) + 10;
      sampleX += sampleWidth;
      if (i + 1 < std::size(styleSamples)) {
        const char* separator = " / ";
        renderer.drawText(sampleFontId, sampleX, y, separator);
        sampleX += renderer.getTextWidth(sampleFontId, separator) + sampleGap;
      }
    }
    const char* availabilityNote = hasSynthetic ? "* simulated" : nullptr;
    if (availabilityNote) {
      const int noteWidth = renderer.getTextWidth(sampleFontId, availabilityNote);
      renderer.drawText(sampleFontId, left + width - noteWidth, y, availabilityNote);
    }
    y += sampleLineHeight + 1;

    // These specimens use the selected family's real or synthesized outlines.
    // Older font files may still fall back to regular until their repaired
    // multi-style .cpfont replacements are installed.
    for (size_t i = 0; i < std::size(styleSamples); i++) {
      renderer.drawText(fontId, specimenX[i], y, "Aa", true, specimenStyle[i]);
    }
    y += lineH + 1;
  }

  const int textBottomLimit = top + height;
  const int maxLines = std::min(FONT_PREVIEW_MAX_TEXT_LINES, std::max(0, (textBottomLimit - y) / (lineH + 1)));
  const auto lines = fontPreviewLines(renderer, fontId, width, fontName);
  for (int lineIndex = 0; lineIndex < std::min(maxLines, static_cast<int>(lines.size())); lineIndex++) {
    if (y + lineH > textBottomLimit) break;
    renderer.drawText(fontId, left, y, lines[lineIndex].c_str());
    y += lineH + 1;
  }
}

bool FontSelectionActivity::cacheOriginalPreview(const int top, const int height) {
  if (!originalPreviewBuffer_.empty() && originalPreviewTop_ == top && originalPreviewHeight_ == height) return true;

  const uint8_t savedFontFamily = SETTINGS.fontFamily;
  const uint8_t savedFontSize = SETTINGS.fontSize;
  const uint8_t savedSdFontPointSize = SETTINGS.sdFontPointSize;
  char savedSdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName)] = {};
  strncpy(savedSdFontFamilyName, SETTINGS.sdFontFamilyName, sizeof(savedSdFontFamilyName) - 1);

  applyOriginalFontForPreview();
  const int originalFontId = SETTINGS.getReaderFontId();
  renderSampleBlock(top, height, originalFontId, "A  Current", originalFontName_.c_str(), previewPointSize_, false);

  const size_t bufferSize = renderer.getRegionByteSize(0, top, renderer.getScreenWidth(), height);
  if (bufferSize > 0) {
    originalPreviewBuffer_.resize(bufferSize);
    if (!renderer.copyRegionToBuffer(0, top, renderer.getScreenWidth(), height, originalPreviewBuffer_.data(),
                                     originalPreviewBuffer_.size())) {
      originalPreviewBuffer_.clear();
    }
  }
  originalPreviewTop_ = top;
  originalPreviewHeight_ = height;

  SETTINGS.fontFamily = savedFontFamily;
  SETTINGS.fontSize = savedFontSize;
  SETTINGS.sdFontPointSize = savedSdFontPointSize;
  strncpy(SETTINGS.sdFontFamilyName, savedSdFontFamilyName, sizeof(SETTINGS.sdFontFamilyName) - 1);
  SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
  sdFontSystem.ensureLoaded(renderer);

  return !originalPreviewBuffer_.empty();
}

void FontSelectionActivity::renderPreviewPane(const int top, const int height, const int fontId, const char* fontName) {
  if (height <= 0 || fontId == 0) return;

  const int lineHeight = std::max(originalFontLineHeight_, renderer.getTextHeight(fontId));
  const int labelHeight = renderer.getTextHeight(UI_10_FONT_ID);
  const int styleHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int comparisonTextLines =
      std::clamp(static_cast<int>(fontPreviewLines(renderer, fontId,
                                                   renderer.getScreenWidth() - (metrics_.previewPadding * 2), fontName)
                                      .size()),
                 1, FONT_PREVIEW_MAX_TEXT_LINES);
  const int referenceRequired = labelHeight + FONT_PREVIEW_SECTION_GAP + (originalPreviewTextLines_ * (lineHeight + 1));
  const int comparisonRequired = labelHeight + FONT_PREVIEW_SECTION_GAP + styleHeight + 1 + lineHeight + 1 +
                                 (comparisonTextLines * (lineHeight + 1));
  const int maxReferenceHeight = std::max(referenceRequired, height - comparisonRequired - FONT_PREVIEW_PANE_GAP);
  const int referenceHeight = std::min(std::max(referenceRequired, height * 40 / 100), maxReferenceHeight);
  const int comparisonTop = top + referenceHeight + FONT_PREVIEW_PANE_GAP;
  const int comparisonHeight = std::max(0, top + height - comparisonTop);

  if (cacheOriginalPreview(top, referenceHeight)) {
    renderer.copyBufferToRegion(0, top, renderer.getScreenWidth(), referenceHeight, originalPreviewBuffer_.data(),
                                originalPreviewBuffer_.size());
  }
  renderer.drawLine(metrics_.previewPadding, top + referenceHeight + 1,
                    renderer.getScreenWidth() - metrics_.previewPadding, top + referenceHeight + 1);

  const uint8_t previewPointSize = currentFontPointSize(registry_);
  renderSampleBlock(comparisonTop, comparisonHeight, fontId, "B  Preview", fontName, previewPointSize, true);
}

void FontSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_FONT_FAMILY));

  const int previewFontId = SETTINGS.getReaderFontId();
  const char* previewFontName = (previewFontIndex_ >= 0 && previewFontIndex_ < static_cast<int>(fonts_.size()))
                                    ? fonts_[previewFontIndex_].name.c_str()
                                    : nullptr;
  const int lineHeight = std::max(originalFontLineHeight_, renderer.getTextHeight(previewFontId));
  const int labelHeight = renderer.getTextHeight(UI_10_FONT_ID);
  const int styleHeight = renderer.getTextHeight(SMALL_FONT_ID);
  const int comparisonTextLines = std::clamp(
      static_cast<int>(fontPreviewLines(renderer, previewFontId,
                                        renderer.getScreenWidth() - (metrics_.previewPadding * 2), previewFontName)
                           .size()),
      1, FONT_PREVIEW_MAX_TEXT_LINES);
  const int requiredPreviewHeight = labelHeight + FONT_PREVIEW_SECTION_GAP +
                                    (originalPreviewTextLines_ * (lineHeight + 1)) + FONT_PREVIEW_PANE_GAP +
                                    labelHeight + FONT_PREVIEW_SECTION_GAP + styleHeight + 1 + lineHeight + 1 +
                                    (comparisonTextLines * (lineHeight + 1));
  const int baselinePreviewHeight =
      std::max(usableHeight * metrics_.previewHeightPercent / 100, usableHeight * FONT_PREVIEW_MIN_PERCENT / 100);
  // Very wide faces may need a third preview line. Preserve three list rows
  // instead of clipping the type specimen just to keep five rows visible.
  const int minimumListHeight = metrics_.listRowHeight * 3;
  const int maximumPreviewHeight =
      std::max(baselinePreviewHeight, usableHeight - metrics_.verticalSpacing - minimumListHeight);
  previewHeight = std::min(std::max(baselinePreviewHeight, requiredPreviewHeight), maximumPreviewHeight);

  const int previewTop = afterHeader;
  const int listTop = previewTop + previewHeight + metrics_.verticalSpacing;
  const int listHeight = usableHeight - previewHeight - metrics_.verticalSpacing;

  renderPreviewPane(previewTop, previewHeight, previewFontId, previewFontName);

  renderer.drawLine(0, listTop - metrics_.verticalSpacing / 2, pageWidth, listTop - metrics_.verticalSpacing / 2);

  const int currentFontIndex = findCurrentFontRow(originalSdFontFamilyName_, originalFontFamily_);
  GUI.drawList(
      renderer, Rect{0, listTop, pageWidth, listHeight}, static_cast<int>(fonts_.size()), selectedIndex_,
      [this](int index) { return fonts_[index].name; }, nullptr, nullptr,
      [this, currentFontIndex](int index) -> std::string {
        if (fonts_[index].isHeader) return "";
        if (index == previewFontIndex_ && index != currentFontIndex) return tr(STR_PREVIEW);
        if (index == currentFontIndex) return tr(STR_SELECTED);
        return "";
      },
      true, nullptr, [this](int index) { return fonts_[index].isHeader; });

  const bool onPreviewed = selectedIndex_ == previewFontIndex_;
  const char* confirmLabel = onPreviewed ? tr(STR_SELECT) : tr(STR_PREVIEW);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
