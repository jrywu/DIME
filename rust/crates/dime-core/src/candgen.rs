//! Candidate generation plan: port of the decisions in
//! `CCompositionProcessorEngine::GetCandidateList`.
//!
//! The dictionary engines and the candidate list are still C++ objects, so
//! this returns the sequence of operations `GetCandidateList` performs -- which
//! engine to search with which key and how, when to sort, when to stop -- and
//! the C++ side executes it.  The order of the steps is the C++ order.

use crate::composition::vp;
use crate::keyclass::{ime_mode, ARRAY40_BIG5};

/// Dictionary engines of `CCompositionProcessorEngine`.
pub mod engine {
    /// `_pTableDictionaryEngine[imeMode]`.
    pub const TABLE: u32 = 0;
    /// `_pBig5TableDictionaryEngine[imeMode]`.
    pub const BIG5: u32 = 1;
    /// `_pCustomTableDictionaryEngine[imeMode]`.
    pub const CUSTOM: u32 = 2;
    pub const ARRAY_SHORT_CODE: u32 = 3;
    pub const ARRAY_PHRASE: u32 = 4;
    pub const ARRAY_EXT_B: u32 = 5;
    pub const ARRAY_EXT_CD: u32 = 6;
    pub const ARRAY_EXT_E: u32 = 7;
}

/// Search keys.
pub mod key {
    /// `_keystrokeBuffer`.
    pub const BUFFER: u32 = 0;
    /// The buffer with `*` appended unless it already has a wildcard.
    pub const INCREMENTAL: u32 = 1;
    /// Phonetic "any tone": the syllable's keys without the tone.
    pub const NO_TONE: u32 = 2;
    /// Phonetic custom phrase: the buffer without its leading `\`, plus `*`.
    pub const CUSTOM_PHRASE: u32 = 3;
}

/// `SEARCH_SECTION`.
pub mod section {
    pub const TEXT: u32 = 0;
    pub const SYMBOL: u32 = 1;
    pub const PHRASE_FROM_KEYSTROKE: u32 = 3;
}

/// `ARRAY_SCOPE` values.
pub mod scope {
    pub const EXT_A: u32 = 0;
    pub const EXT_AB: u32 = 1;
    pub const EXT_ABCD: u32 = 2;
    pub const EXT_A_TO_J: u32 = 3;
    pub const ARRAY30_BIG5: u32 = 5;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Step {
    /// The buffer is a single wildcard: one item showing it; then stop.
    SingleWildcardItem,
    /// `_pActiveCandidateListIndexRange = &_candidateListIndexRange`.
    ResetActiveRange,
    SetSection {
        engine: u32,
        section: u32,
    },
    Collect {
        engine: u32,
        key: u32,
    },
    /// `CollectWordForWildcard`; `with_freq` passes the TC frequency engine.
    CollectWildcard {
        engine: u32,
        key: u32,
        with_freq: bool,
    },
    /// Stop (without the later steps) when no candidates were found.
    ReturnIfEmpty,
    SortByFindKeyCode {
        engine: u32,
    },
    /// Drop the typed prefix from every item's `_FindKeyCode`.
    TrimFindKeyCodes,
    /// `SetupTCFreqTable()`.
    SetupFreqTable,
    /// `SortListItemByWordFrequency` on `engine`, if the frequency table is loaded.
    SortByWordFrequency {
        engine: u32,
    },
    /// Recompute `_candidateWndWidth`.
    UpdateWidth,
    /// `_hasWildcardIncludedInKeystrokeBuffer = FALSE`.
    ClearWildcardFlag,
}

/// Everything `GetCandidateList` decides on.
#[derive(Debug, Clone, Default)]
pub struct SearchInput<'a> {
    /// `Global::imeMode`.
    pub ime_mode: u32,
    /// `IsDictionaryAvailable(Global::imeMode)`.
    pub dictionary_available: bool,
    pub buffer: &'a [u16],
    pub buffer_null: bool,
    pub incremental: bool,
    pub wildcard_search: bool,
    pub array_phrase_ending: bool,
    pub array_scope: u32,
    pub big5_filter: bool,
    pub custom_table_priority: bool,
    pub array_single_quote_custom_phrase: bool,
    /// `IsEscapeInput()`.
    pub escape_input: bool,
    /// The main engine is a TTS dictionary (`GetDictionaryType()`), for the
    /// table engine and the Big5 engine.
    pub table_is_tts: bool,
    pub big5_is_tts: bool,
    pub has_big5: bool,
    pub has_custom: bool,
    pub has_array_short_code: bool,
    pub has_array_phrase: bool,
    pub has_ext_b: bool,
    pub has_ext_cd: bool,
    pub has_ext_e: bool,
    pub has_freq_table: bool,
    pub keystroke_sort: bool,
    pub wildcard_freq_sort: bool,
    pub phonetic_syllable: u32,
}

fn is_wildcard(c: u16) -> bool {
    c == u16::from(b'?') || c == u16::from(b'*')
}

/// The steps `GetCandidateList` performs.
pub fn plan(i: &SearchInput<'_>) -> Vec<Step> {
    use Step::*;
    let mut steps = Vec::new();
    if !i.dictionary_available || i.buffer_null {
        return steps;
    }
    let ime = i.ime_mode;
    let is_array = ime == ime_mode::ARRAY;
    let array_not_big5 = i.array_scope != ARRAY40_BIG5;

    let need_big5 = (is_array && i.array_scope == scope::ARRAY30_BIG5) || (!is_array && i.big5_filter);
    let (main, main_is_tts) = if need_big5 && i.has_big5 {
        (engine::BIG5, i.big5_is_tts)
    } else {
        (engine::TABLE, i.table_is_tts)
    };

    let len = i.buffer.len();
    if len > 0 {
        if len == 1 && is_wildcard(i.buffer[0]) {
            steps.extend([SingleWildcardItem, ClearWildcardFlag]);
            return steps;
        }
        if i.buffer.iter().all(|&c| is_wildcard(c)) {
            steps.push(ClearWildcardFlag);
            return steps;
        }
    }

    steps.push(ResetActiveRange);
    let has_wildcard = i.buffer.iter().any(|&c| is_wildcard(c));
    // IsArrayShortCode()
    let array_short_code = is_array && array_not_big5 && len < 3;
    let quote_matches = i.array_single_quote_custom_phrase == i.array_phrase_ending;

    if i.incremental && array_short_code && !has_wildcard {
        if !i.has_array_short_code {
            steps.push(SetSection {
                engine: main,
                section: section::PHRASE_FROM_KEYSTROKE,
            });
            steps.push(Collect {
                engine: main,
                key: key::BUFFER,
            });
        } else {
            steps.push(Collect {
                engine: engine::ARRAY_SHORT_CODE,
                key: key::BUFFER,
            });
        }
    } else if i.incremental && !(is_array && array_not_big5) {
        let custom = CollectWildcard {
            engine: engine::CUSTOM,
            key: key::INCREMENTAL,
            with_freq: false,
        };
        if i.has_custom && i.custom_table_priority {
            steps.push(custom);
        }
        steps.push(SetSection {
            engine: main,
            section: section::TEXT,
        });
        steps.push(CollectWildcard {
            engine: main,
            key: key::INCREMENTAL,
            with_freq: false,
        });
        if i.has_custom && !i.custom_table_priority {
            steps.push(custom);
        }
        steps.push(ReturnIfEmpty);
        if i.keystroke_sort {
            steps.push(SortByFindKeyCode { engine: main });
        }
        steps.push(TrimFindKeyCodes);
    } else if i.escape_input && (ime == ime_mode::DAYI || is_array) {
        if main_is_tts {
            steps.push(SetSection {
                engine: main,
                section: section::SYMBOL,
            });
        }
        steps.push(Collect {
            engine: main,
            key: key::BUFFER,
        });
    } else if i.escape_input && ime == ime_mode::PHONETIC {
        if i.has_custom {
            steps.push(CollectWildcard {
                engine: engine::CUSTOM,
                key: key::CUSTOM_PHRASE,
                with_freq: false,
            });
        }
    } else if i.wildcard_search {
        if i.wildcard_freq_sort && !i.has_freq_table {
            steps.push(SetupFreqTable);
        }
        let any_tone = ime == ime_mode::PHONETIC && (i.phonetic_syllable & vp::TONE_MASK) == vp::ANY_TONE;
        let custom = |key| CollectWildcard {
            engine: engine::CUSTOM,
            key,
            with_freq: false,
        };

        if i.has_custom && i.custom_table_priority {
            if any_tone {
                steps.push(custom(key::NO_TONE));
            }
            if !is_array || quote_matches {
                steps.push(custom(key::BUFFER));
            }
        }
        if any_tone {
            steps.push(CollectWildcard {
                engine: main,
                key: key::NO_TONE,
                with_freq: true,
            });
        }
        steps.push(SetSection {
            engine: main,
            section: section::TEXT,
        });
        if !is_array || !i.array_phrase_ending {
            steps.push(CollectWildcard {
                engine: main,
                key: key::BUFFER,
                with_freq: true,
            });
        } else if array_not_big5 && i.has_array_phrase {
            steps.push(CollectWildcard {
                engine: engine::ARRAY_PHRASE,
                key: key::BUFFER,
                with_freq: false,
            });
        }
        if i.wildcard_freq_sort && !i.array_phrase_ending {
            steps.push(SortByWordFrequency { engine: main });
        }
        if i.has_custom && !i.custom_table_priority {
            if any_tone {
                steps.push(custom(key::NO_TONE));
            } else if !is_array || (array_not_big5 && quote_matches) {
                steps.push(custom(key::BUFFER));
            }
        }
        array_extensions(i, &mut steps, true);
    } else {
        let custom = Collect {
            engine: engine::CUSTOM,
            key: key::BUFFER,
        };
        if i.has_custom && i.custom_table_priority && (!is_array || (array_not_big5 && quote_matches)) {
            steps.push(custom);
        }
        steps.push(SetSection {
            engine: main,
            section: section::TEXT,
        });
        if !is_array || !i.array_phrase_ending {
            steps.push(Collect {
                engine: main,
                key: key::BUFFER,
            });
        } else if array_not_big5 && i.has_array_phrase {
            steps.push(Collect {
                engine: engine::ARRAY_PHRASE,
                key: key::BUFFER,
            });
        }
        if i.has_custom && !i.custom_table_priority && (!is_array || quote_matches) {
            steps.push(custom);
        }
        array_extensions(i, &mut steps, false);
    }

    steps.push(UpdateWidth);
    if !i.incremental {
        steps.push(ClearWildcardFlag);
    }
    steps
}

/// The Array Unicode extension tables searched for the configured scope.
fn array_extensions(i: &SearchInput<'_>, steps: &mut Vec<Step>, wildcard: bool) {
    if i.ime_mode != ime_mode::ARRAY
        || i.array_phrase_ending
        || i.array_scope == ARRAY40_BIG5
        || i.array_scope == scope::EXT_A
    {
        return;
    }
    let tables: &[(u32, bool)] = match i.array_scope {
        scope::EXT_AB => &[(engine::ARRAY_EXT_B, i.has_ext_b)],
        scope::EXT_ABCD => &[(engine::ARRAY_EXT_B, i.has_ext_b), (engine::ARRAY_EXT_CD, i.has_ext_cd)],
        scope::EXT_A_TO_J => &[
            (engine::ARRAY_EXT_B, i.has_ext_b),
            (engine::ARRAY_EXT_CD, i.has_ext_cd),
            (engine::ARRAY_EXT_E, i.has_ext_e),
        ],
        _ => &[],
    };
    for &(engine, present) in tables {
        if present {
            steps.push(if wildcard {
                Step::CollectWildcard {
                    engine,
                    key: key::BUFFER,
                    with_freq: false,
                }
            } else {
                Step::Collect {
                    engine,
                    key: key::BUFFER,
                }
            });
        }
    }
}

/// The key `GetCandidateStringInConverted` looks up associated phrases with,
/// for the character just committed.
///
/// A CIN phrase table (`Phrase.cin`, README format: the key is the character)
/// is matched exactly.  A TTS table is searched in its `[Phrase]` section, with
/// a trailing `*` when no phrase section was seen (the old behaviour).
///
/// Before this port the `*` was also appended for CIN tables unless some
/// dictionary had a TTS `[Phrase]` or CIN `%phrasedef` section, and CIN
/// lookups do not treat `*` as a wildcard, so a README-format `Phrase.cin`
/// never produced associated phrases (and the result depended on what the
/// process had loaded before).
pub fn associated_phrase_key(
    last: &[u16],
    phrase_table_is_tts: bool,
    has_phrase_section: bool,
    has_cin_phrase_section: bool,
) -> Vec<u16> {
    let mut key = last.to_vec();
    if phrase_table_is_tts && !(has_phrase_section || has_cin_phrase_section) {
        key.push(u16::from(b'*'));
    }
    key
}

#[cfg(test)]
mod tests {
    use super::*;
    use Step::*;

    fn s(t: &str) -> Vec<u16> {
        t.encode_utf16().collect()
    }

    #[test]
    fn associated_phrase_key_is_exact_for_cin_tables() {
        let ren = [0x4EBA];
        assert_eq!(associated_phrase_key(&ren, false, false, false), ren);
        assert_eq!(associated_phrase_key(&ren, true, true, false), ren);
        assert_eq!(
            associated_phrase_key(&ren, true, false, false),
            [0x4EBA, u16::from(b'*')]
        );
    }

    #[test]
    fn plain_dayi_lookup() {
        let b = s("ab");
        let i = SearchInput {
            dictionary_available: true,
            buffer: &b,
            ..Default::default()
        };
        assert_eq!(
            plan(&i),
            vec![
                ResetActiveRange,
                SetSection {
                    engine: engine::TABLE,
                    section: section::TEXT
                },
                Collect {
                    engine: engine::TABLE,
                    key: key::BUFFER
                },
                UpdateWidth,
                ClearWildcardFlag,
            ]
        );
    }

    #[test]
    fn single_wildcard_stops_early() {
        let b = s("*");
        let i = SearchInput {
            dictionary_available: true,
            buffer: &b,
            ..Default::default()
        };
        assert_eq!(plan(&i), vec![SingleWildcardItem, ClearWildcardFlag]);
    }

    #[test]
    fn incremental_returns_if_empty_before_trimming() {
        let b = s("a");
        let i = SearchInput {
            dictionary_available: true,
            buffer: &b,
            incremental: true,
            keystroke_sort: true,
            ..Default::default()
        };
        let p = plan(&i);
        let pos = |st: Step| p.iter().position(|&x| x == st).unwrap();
        assert!(pos(ReturnIfEmpty) < pos(SortByFindKeyCode { engine: engine::TABLE }));
        assert_eq!(p.last(), Some(&UpdateWidth));
    }

    #[test]
    fn array_ext_tables_follow_scope() {
        let b = s("ab");
        let i = SearchInput {
            ime_mode: ime_mode::ARRAY,
            dictionary_available: true,
            buffer: &b,
            array_scope: scope::EXT_A_TO_J,
            has_ext_b: true,
            has_ext_e: true,
            ..Default::default()
        };
        let p = plan(&i);
        assert!(p.contains(&Collect {
            engine: engine::ARRAY_EXT_B,
            key: key::BUFFER
        }));
        assert!(!p.iter().any(|st| matches!(
            st,
            Collect {
                engine: engine::ARRAY_EXT_CD,
                ..
            }
        )));
        assert!(p.contains(&Collect {
            engine: engine::ARRAY_EXT_E,
            key: key::BUFFER
        }));
    }
}
