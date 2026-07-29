#include "FavoritesActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <string>
#include <vector>

#include "MappedInputManager.h"
#include "activities/home/BookInfoActivity.h"
#include "activities/home/FileBrowserActionActivity.h"
#include "components/CompactHeader.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long LONG_PRESS_MS = 1000;
}

void FavoritesActivity::reload() {
  books = FAVORITES.getBooks();
  selectedIndex = books.empty() ? 0 : std::clamp(selectedIndex, 0, static_cast<int>(books.size()) - 1);
}

void FavoritesActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void FavoritesActivity::onExit() {
  books.clear();
  Activity::onExit();
}

void FavoritesActivity::showActions(const bool ignoreInitialConfirmRelease) {
  if (books.empty() || selectedIndex < 0 || selectedIndex >= static_cast<int>(books.size())) return;
  const FavoriteBook book = books[selectedIndex];
  std::vector<FileBrowserActionActivity::MenuItem> items;
  items.reserve(4);
  items.push_back({FileBrowserAction::MoreInfo, StrId::STR_MORE_INFO});
  if (selectedIndex > 0) items.push_back({FileBrowserAction::MoveFavoriteUp, StrId::STR_MOVE_UP});
  if (selectedIndex + 1 < static_cast<int>(books.size())) {
    items.push_back({FileBrowserAction::MoveFavoriteDown, StrId::STR_MOVE_DOWN});
  }
  items.push_back({FileBrowserAction::RemoveBookFavorite, StrId::STR_REMOVE_FROM_FAVORITES});

  startActivityForResult(
      std::make_unique<FileBrowserActionActivity>(renderer, mappedInput, book.title, std::move(items),
                                                  ignoreInitialConfirmRelease),
      [this, book](const ActivityResult& result) {
        if (result.isCancelled) {
          requestUpdate();
          return;
        }
        const auto* actionResult = std::get_if<FileBrowserActionResult>(&result.data);
        if (!actionResult) return;
        switch (static_cast<FileBrowserAction>(actionResult->action)) {
          case FileBrowserAction::MoreInfo:
            startActivityForResult(
                std::make_unique<BookInfoActivity>(renderer, mappedInput, book.path, book.title, book.coverBmpPath),
                [this](const ActivityResult&) { requestUpdate(); });
            return;
          case FileBrowserAction::MoveFavoriteUp:
            if (FAVORITES.moveBook(selectedIndex, selectedIndex - 1)) --selectedIndex;
            break;
          case FileBrowserAction::MoveFavoriteDown:
            if (FAVORITES.moveBook(selectedIndex, selectedIndex + 1)) ++selectedIndex;
            break;
          case FileBrowserAction::RemoveBookFavorite:
            FAVORITES.removeBook(book.path);
            break;
          default:
            break;
        }
        reload();
        requestUpdate(true);
      });
}

void FavoritesActivity::loop() {
  if (longPressFired) {
    if (!mappedInput.isPressed(MappedInputManager::Button::Confirm)) longPressFired = false;
    return;
  }

  if (!books.empty() && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= LONG_PRESS_MS) {
    longPressFired = true;
    showActions(true);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!books.empty()) onSelectBook(books[selectedIndex].path);
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (books.empty()) return;
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(books.size()));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (books.empty()) return;
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(books.size()));
    requestUpdate();
  });
}

void FavoritesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = CompactHeader::contentTop(metrics) + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const std::string heading = std::string(tr(STR_FAVORITES)) + " (" + std::to_string(books.size()) + ")";
  CompactHeader::drawTitle(renderer, heading.c_str(), true);

  if (books.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, contentTop + 36, tr(STR_NO_FAVORITES));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, renderer.getScreenWidth(), contentHeight}, static_cast<int>(books.size()),
        selectedIndex, [this](const int index) { return books[index].title; },
        [this](const int index) { return books[index].author.empty() ? books[index].path : books[index].author; },
        [](const int) { return BookmarkIcon; }, nullptr, false,
        [this](const int index) { return !Storage.exists(books[index].path.c_str()); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
