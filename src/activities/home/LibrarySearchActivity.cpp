#include "LibrarySearchActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <utility>

#include "MappedInputManager.h"
#include "BookInfoActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "components/icons/book.h"
#include "fontIds.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;

std::string trimQuery(std::string query) {
  const size_t first = query.find_first_not_of(' ');
  if (first == std::string::npos) return {};
  const size_t last = query.find_last_not_of(' ');
  return query.substr(first, last - first + 1);
}

std::vector<KeyboardSuggestion> librarySuggestions(const std::string& text, const size_t maxSuggestions) {
  LibraryBookSuggestionResponse response = LibraryBookInfo::suggest(text, maxSuggestions);
  std::vector<KeyboardSuggestion> suggestions;
  suggestions.reserve(response.suggestions.size());
  for (auto& suggestion : response.suggestions) {
    const char* category = "Title";
    switch (suggestion.kind) {
      case LibraryBookSuggestionKind::Author:
        category = "Author";
        break;
      case LibraryBookSuggestionKind::Series:
        category = "Series";
        break;
      case LibraryBookSuggestionKind::Title:
      default:
        break;
    }
    suggestions.push_back({std::move(suggestion.value), category});
  }
  return suggestions;
}
}  // namespace

void LibrarySearchActivity::onEnter() {
  Activity::onEnter();
  selectorIndex = 0;
  if (query.empty()) {
    if (LibraryBookInfo::hasCatalog()) {
      launchKeyboard();
    } else {
      response.catalogAvailable = false;
      requestUpdate();
    }
  } else {
    runSearch();
  }
}

void LibrarySearchActivity::onExit() {
  Activity::onExit();
  LibraryBookInfo::releaseSuggestionCache();
  response.books.clear();
}

void LibrarySearchActivity::launchKeyboard() {
  if (keyboardOpen) return;
  keyboardOpen = true;
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Search Library", query, 128,
                                              InputType::Search, 2, librarySuggestions),
      [this](const ActivityResult& result) {
        keyboardOpen = false;
        if (result.isCancelled) {
          if (query.empty()) {
            cancel();
          } else {
            requestUpdate();
          }
          return;
        }

        const auto* keyboardResult = std::get_if<KeyboardResult>(&result.data);
        if (!keyboardResult) {
          cancel();
          return;
        }
        query = trimQuery(keyboardResult->text);
        if (query.empty()) {
          cancel();
          return;
        }
        runSearch();
      });
}

void LibrarySearchActivity::runSearch() {
  selectorIndex = 0;
  // The forward-typing autocomplete cache is no longer needed once the
  // keyboard closes; release it before allocating up to MAX_RESULTS rows.
  LibraryBookInfo::releaseSuggestionCache();
  response = LibraryBookInfo::search(query, MAX_RESULTS);
  LOG_INF("BOOKSEARCH", "Query '%s' returned %u of %u result(s)", query.c_str(),
          static_cast<unsigned>(response.books.size()), static_cast<unsigned>(response.totalMatches));
  requestUpdate(true);
}

void LibrarySearchActivity::cancel() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

void LibrarySearchActivity::loop() {
  if (keyboardOpen) return;

  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired = false;
    return;
  }

  if (!response.books.empty() && selectorIndex < response.books.size() &&
      mappedInput.isPressed(MappedInputManager::Button::Confirm) && mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    mappedInput.suppressNextConfirmRelease();
    const auto& book = response.books[selectorIndex];
    startActivityForResult(
        std::make_unique<BookInfoActivity>(renderer, mappedInput, book.path, book.title, std::string{}),
        [this](const ActivityResult&) { requestUpdate(); });
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    cancel();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (response.books.empty()) {
      if (response.catalogAvailable || LibraryBookInfo::hasCatalog()) launchKeyboard();
    } else if (selectorIndex < response.books.size()) {
      onSelectBook(response.books[selectorIndex].path);
    }
    return;
  }

  const int listSize = static_cast<int>(response.books.size());
  if (listSize <= 0) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int pageItems = std::max(1, contentHeight / metrics.listWithSubtitleRowHeight);

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void LibrarySearchActivity::render(RenderLock&&) {
  if (keyboardOpen) return;
  renderer.clearScreen();

  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  CompactHeader::drawTitle(renderer, "Search Library");

  char resultCount[32];
  if (response.truncated) {
    snprintf(resultCount, sizeof(resultCount), "%u of %u books", static_cast<unsigned>(response.books.size()),
             static_cast<unsigned>(response.totalMatches));
  } else {
    snprintf(resultCount, sizeof(resultCount), "%u %s", static_cast<unsigned>(response.totalMatches),
             response.totalMatches == 1 ? "book" : "books");
  }
  const int subHeaderTop = CompactHeader::contentTop(metrics);
  GUI.drawSubHeader(renderer, Rect{0, subHeaderTop, pageWidth, metrics.tabBarHeight}, query.c_str(), resultCount);

  const int contentTop = subHeaderTop + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  if (response.books.empty()) {
    const char* message = response.catalogAvailable ? "No matching books" : "Library search index not found";
    const int messageWidth = renderer.getTextWidth(UI_10_FONT_ID, message);
    renderer.drawText(UI_10_FONT_ID, std::max(metrics.contentSidePadding, (pageWidth - messageWidth) / 2),
                      contentTop + 36, message);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(response.books.size()),
        static_cast<int>(selectorIndex), [this](int index) { return response.books[index].title; },
        [this](int index) {
          std::string subtitle = response.books[index].author;
          if (!response.books[index].series.empty()) {
            if (!subtitle.empty()) subtitle += " | ";
            subtitle += response.books[index].series;
          }
          return subtitle;
        },
        [](int) { return Book; });
  }

  const char* confirmLabel = response.books.empty() ? (response.catalogAvailable ? tr(STR_SEARCH) : "") : tr(STR_OPEN);
  const char* previousLabel = response.books.empty() ? "" : tr(STR_DIR_UP);
  const char* nextLabel = response.books.empty() ? "" : tr(STR_DIR_DOWN);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, previousLabel, nextLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
