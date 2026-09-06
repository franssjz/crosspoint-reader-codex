#include "LanguageRegistry.h"

#include <algorithm>

#include "HyphenationCommon.h"
#include "generated/hyph-en.trie.h"
#include "generated/hyph-es.trie.h"
#include "generated/hyph-fi.trie.h"
#include "generated/hyph-fr.trie.h"
#include "generated/hyph-it.trie.h"
#include "generated/hyph-pl.trie.h"
#include "generated/hyph-ru.trie.h"
#include "generated/hyph-sv.trie.h"
#include "generated/hyph-tr.trie.h"
#include "generated/hyph-uk.trie.h"

#if CPR_ENABLE_GERMAN_HYPHENATION
#include "generated/hyph-de.trie.h"
#endif

namespace {

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator frenchHyphenator(fr_patterns, isLatinLetter, toLowerLatin);
#if CPR_ENABLE_GERMAN_HYPHENATION
LanguageHyphenator germanHyphenator(de_patterns, isLatinLetter, toLowerLatin);
#endif
LanguageHyphenator russianHyphenator(ru_patterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator spanishHyphenator(es_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator italianHyphenator(it_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator swedishHyphenator(sv_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator turkishHyphenator(tr_patterns, isLatinLetter, toLowerTurkish);
LanguageHyphenator ukrainianHyphenator(uk_patterns, isCyrillicLetter, toLowerCyrillic);
LanguageHyphenator polishHyphenator(pl_patterns, isLatinLetter, toLowerLatin);
LanguageHyphenator finnishHyphenator(fi_patterns, isLatinLetter, toLowerLatin);

static const LanguageEntry kEntries[] = {
    {"english", "en", &englishHyphenator},     {"french", "fr", &frenchHyphenator},
#if CPR_ENABLE_GERMAN_HYPHENATION
    {"german", "de", &germanHyphenator},
#endif
    {"russian", "ru", &russianHyphenator},     {"spanish", "es", &spanishHyphenator},
    {"italian", "it", &italianHyphenator},     {"polish", "pl", &polishHyphenator},
    {"swedish", "sv", &swedishHyphenator},     {"turkish", "tr", &turkishHyphenator},
    {"ukrainian", "uk", &ukrainianHyphenator}, {"finnish", "fi", &finnishHyphenator},
};

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto it = std::find_if(std::begin(kEntries), std::end(kEntries),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != std::end(kEntries)) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() { return LanguageEntryView{kEntries, sizeof(kEntries) / sizeof(kEntries[0])}; }
