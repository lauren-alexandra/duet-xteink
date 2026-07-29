#pragma once

#include <I18n.h>

#include "LauncherLayoutStore.h"

inline StrId launcherItemLabel(const LauncherItem item) {
  switch (item) {
    case LauncherItem::BrowseFiles:
      return StrId::STR_BROWSE_FILES;
    case LauncherItem::SearchLibrary:
      return StrId::STR_SEARCH;
    case LauncherItem::RecentBooks:
      return StrId::STR_MENU_RECENT_BOOKS;
    case LauncherItem::ReadingStats:
      return StrId::STR_READING_STATS;
    case LauncherItem::ReadingHeatmap:
      return StrId::STR_STATS_ACTIVITY_HEATMAP;
    case LauncherItem::ReadingProfile:
      return StrId::STR_STATS_READING_PROFILE;
    case LauncherItem::SavedItems:
      return StrId::STR_BOOKMARKS_AND_CLIPPINGS;
    case LauncherItem::Favorites:
      return StrId::STR_FAVORITES;
    case LauncherItem::Achievements:
      return StrId::STR_ACHIEVEMENTS;
    case LauncherItem::Dictionary:
      return StrId::STR_DICTIONARY;
    case LauncherItem::Tetris:
      return StrId::STR_TETRIS;
    case LauncherItem::IfFound:
      return StrId::STR_IF_FOUND_RETURN_ME;
    case LauncherItem::ScreenClean:
      return StrId::STR_SCREEN_CLEAN;
    case LauncherItem::NearbyStatsSync:
      return StrId::STR_NEARBY_STATS_SYNC;
    case LauncherItem::FileTransfer:
      return StrId::STR_FILE_TRANSFER;
    case LauncherItem::OpdsBrowser:
      return StrId::STR_OPDS_BROWSER;
    case LauncherItem::KOReaderSync:
      return StrId::STR_KOREADER_SYNC;
    case LauncherItem::Sleep:
      return StrId::STR_SLEEP;
    case LauncherItem::ReadMe:
      return StrId::STR_READ_ME;
    case LauncherItem::Apps:
      return StrId::STR_APPS;
    case LauncherItem::CustomizeHomeApps:
      return StrId::STR_CUSTOMIZE_HOME_APPS;
    case LauncherItem::Settings:
      return StrId::STR_SETTINGS_TITLE;
    case LauncherItem::Count:
    default:
      return StrId::STR_NONE_OPT;
  }
}

inline StrId launcherItemDescription(const LauncherItem item) {
  switch (item) {
    case LauncherItem::BrowseFiles:
      return StrId::STR_BROWSE_FILES_APP_DESC;
    case LauncherItem::SearchLibrary:
      return StrId::STR_SEARCH_LIBRARY_APP_DESC;
    case LauncherItem::RecentBooks:
      return StrId::STR_RECENT_BOOKS_APP_DESC;
    case LauncherItem::ReadingStats:
      return StrId::STR_READING_STATS_APP_DESC;
    case LauncherItem::ReadingHeatmap:
      return StrId::STR_READING_HEATMAP_APP_DESC;
    case LauncherItem::ReadingProfile:
      return StrId::STR_READING_PROFILE_APP_DESC;
    case LauncherItem::SavedItems:
      return StrId::STR_SAVED_ITEMS_APP_DESC;
    case LauncherItem::Favorites:
      return StrId::STR_FAVORITES_APP_DESC;
    case LauncherItem::Achievements:
      return StrId::STR_ACHIEVEMENTS_APP_DESC;
    case LauncherItem::Dictionary:
      return StrId::STR_DICTIONARY_APP_DESC;
    case LauncherItem::Tetris:
      return StrId::STR_TETRIS_DESC;
    case LauncherItem::IfFound:
      return StrId::STR_IF_FOUND_APP_DESC;
    case LauncherItem::ScreenClean:
      return StrId::STR_SCREEN_CLEAN_APP_DESC;
    case LauncherItem::NearbyStatsSync:
      return StrId::STR_NEARBY_STATS_SYNC_DESC;
    case LauncherItem::FileTransfer:
      return StrId::STR_FILE_TRANSFER_APP_DESC;
    case LauncherItem::OpdsBrowser:
      return StrId::STR_OPDS_BROWSER_APP_DESC;
    case LauncherItem::KOReaderSync:
      return StrId::STR_KOREADER_SYNC_APP_DESC;
    case LauncherItem::Sleep:
      return StrId::STR_SLEEP_APP_DESC;
    case LauncherItem::ReadMe:
      return StrId::STR_READ_ME_APP_DESC;
    case LauncherItem::Apps:
      return StrId::STR_APPS_APP_DESC;
    case LauncherItem::CustomizeHomeApps:
      return StrId::STR_CUSTOMIZE_HOME_APPS_DESC;
    case LauncherItem::Settings:
      return StrId::STR_SETTINGS_APP_DESC;
    case LauncherItem::Count:
    default:
      return StrId::STR_NONE_OPT;
  }
}
