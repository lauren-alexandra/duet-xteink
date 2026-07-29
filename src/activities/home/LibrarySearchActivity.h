#pragma once

#include <string>

#include "LibraryBookInfo.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class LibrarySearchActivity final : public Activity {
 public:
  explicit LibrarySearchActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                 std::string initialQuery = {})
      : Activity("LibrarySearch", renderer, mappedInput), query(std::move(initialQuery)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  static constexpr size_t MAX_RESULTS = 80;

  ButtonNavigator buttonNavigator;
  std::string query;
  LibraryBookSearchResponse response;
  size_t selectorIndex = 0;
  bool keyboardOpen = false;
  bool longPressFired = false;

  void launchKeyboard();
  void runSearch();
  void cancel();
};
