#pragma once

#include <vector>

#include "FavoritesStore.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FavoritesActivity final : public Activity {
 public:
  FavoritesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Favorites", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<FavoriteBook> books;
  int selectedIndex = 0;
  bool longPressFired = false;
  ButtonNavigator buttonNavigator;

  void reload();
  void showActions(bool ignoreInitialConfirmRelease);
};
