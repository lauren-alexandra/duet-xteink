#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include "HyphenationCommon.h"
// Each trie is a large flash table (de alone is ~206KB). OMIT_HYPH_* flags let
// size-constrained envs drop languages they never render; English is always in.
#include "generated/hyph-en.trie.h"
#if !defined(OMIT_HYPH_DE)
#include "generated/hyph-de.trie.h"
#endif
#if !defined(OMIT_HYPH_ES)
#include "generated/hyph-es.trie.h"
#endif
#if !defined(OMIT_HYPH_FR)
#include "generated/hyph-fr.trie.h"
#endif
#if !defined(OMIT_HYPH_IT)
#include "generated/hyph-it.trie.h"
#endif
#if !defined(OMIT_HYPH_PL)
#include "generated/hyph-pl.trie.h"
#endif
#if !defined(OMIT_HYPH_PT)
#include "generated/hyph-pt.trie.h"
#endif
#if !defined(OMIT_HYPH_RU)
#include "generated/hyph-ru.trie.h"
#endif
#if !defined(OMIT_HYPH_SV)
#include "generated/hyph-sv.trie.h"
#endif
#if !defined(OMIT_HYPH_UK)
#include "generated/hyph-uk.trie.h"
#endif

namespace {

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
#if !defined(OMIT_HYPH_FR)
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_DE)
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_RU)
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if !defined(OMIT_HYPH_ES)
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_IT)
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_SV)
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_UK)
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
#endif
#if !defined(OMIT_HYPH_PL)
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
#endif
#if !defined(OMIT_HYPH_PT)
LanguageHyphenator portugueseHyphenator(pt_patterns, isLatinLetter, toLowerLatin);
#endif

constexpr size_t kEntryCount = 1
#if !defined(OMIT_HYPH_FR)
                               + 1
#endif
#if !defined(OMIT_HYPH_DE)
                               + 1
#endif
#if !defined(OMIT_HYPH_RU)
                               + 1
#endif
#if !defined(OMIT_HYPH_ES)
                               + 1
#endif
#if !defined(OMIT_HYPH_IT)
                               + 1
#endif
#if !defined(OMIT_HYPH_PL)
                               + 1
#endif
#if !defined(OMIT_HYPH_PT)
                               + 1
#endif
#if !defined(OMIT_HYPH_SV)
                               + 1
#endif
#if !defined(OMIT_HYPH_UK)
                               + 1
#endif
    ;

using EntryArray = std::array<LanguageEntry, kEntryCount>;

const EntryArray& entries() {
  static const EntryArray kEntries = {{
      {"english", "en", &englishHyphenator},
#if !defined(OMIT_HYPH_FR)
      {"french", "fr", &frenchHyphenator},
#endif
#if !defined(OMIT_HYPH_DE)
      {"german", "de", &germanHyphenator},
#endif
#if !defined(OMIT_HYPH_RU)
      {"russian", "ru", &russianHyphenator},
#endif
#if !defined(OMIT_HYPH_ES)
      {"spanish", "es", &spanishHyphenator},
#endif
#if !defined(OMIT_HYPH_IT)
      {"italian", "it", &italianHyphenator},
#endif
#if !defined(OMIT_HYPH_PL)
      {"polish", "pl", &polishHyphenator},
#endif
#if !defined(OMIT_HYPH_PT)
      {"portuguese", "pt", &portugueseHyphenator},
#endif
#if !defined(OMIT_HYPH_SV)
      {"swedish", "sv", &swedishHyphenator},
#endif
#if !defined(OMIT_HYPH_UK)
      {"ukrainian", "uk", &ukrainianHyphenator},
#endif
  }};
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
