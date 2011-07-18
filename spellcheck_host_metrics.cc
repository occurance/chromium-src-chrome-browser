// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/spellcheck_host_metrics.h"

#include "base/md5.h"
#include "base/metrics/histogram.h"

SpellCheckHostMetrics::SpellCheckHostMetrics()
    : misspelled_word_count_(0),
      spellchecked_word_count_(0),
      suggestion_show_count_(0),
      replaced_word_count_(0),
      start_time_(base::Time::Now()) {
  const uint64 kHistogramTimerDurationInMinutes = 30;
  recording_timer_.Start(
      base::TimeDelta::FromMinutes(kHistogramTimerDurationInMinutes),
      this, &SpellCheckHostMetrics::OnHistogramTimerExpired);
  RecordWordCounts();
}

SpellCheckHostMetrics::~SpellCheckHostMetrics() {
}

void SpellCheckHostMetrics::RecordCustomWordCountStats(size_t count) {
  UMA_HISTOGRAM_COUNTS("SpellCheck.CustomWords", count);
}

void SpellCheckHostMetrics::RecordEnabledStats(bool enabled) {
  UMA_HISTOGRAM_BOOLEAN("SpellCheck.Enabled", enabled);
  // Because SpellCheckHost is instantiated lazily, the size of
  // custom dictionary is unknown at this time. We mark it as -1 and
  // record actual value later. See SpellCheckHost for more detail.
  if (enabled)
    RecordCustomWordCountStats(-1);
}

void SpellCheckHostMetrics::RecordCheckedWordStats(const string16& word,
                                                   bool misspell) {
  spellchecked_word_count_++;
  if (misspell) {
    misspelled_word_count_++;
    // If an user misspelled, that user should be counted as a part of
    // the population.  So we ensure to instantiate the histogram
    // entries here at the first time.
    if (misspelled_word_count_ == 1)
      RecordReplacedWordStats(0);
  }

  int percentage = (100 * misspelled_word_count_) / spellchecked_word_count_;
  UMA_HISTOGRAM_PERCENTAGE("SpellCheck.MisspellRatio", percentage);

  // Collects actual number of checked words, excluding duplication.
  base::MD5Digest digest;
  base::MD5Sum(reinterpret_cast<const unsigned char*>(word.c_str()),
         word.size() * sizeof(char16), &digest);
  checked_word_hashes_.insert(base::MD5DigestToBase16(digest));

  RecordWordCounts();
}

void SpellCheckHostMetrics::OnHistogramTimerExpired() {
  if (0 < spellchecked_word_count_) {
    // Collects word checking rate, which is represented
    // as a word count per hour.
    base::TimeDelta since_start = base::Time::Now() - start_time_;
    size_t checked_words_per_hour = spellchecked_word_count_ *
        base::TimeDelta::FromHours(1).InSeconds() / since_start.InSeconds();
    UMA_HISTOGRAM_COUNTS("SpellCheck.CheckedWordsPerHour",
                         checked_words_per_hour);
  }
}

void SpellCheckHostMetrics::RecordDictionaryCorruptionStats(bool corrupted) {
  UMA_HISTOGRAM_BOOLEAN("SpellCheck.DictionaryCorrupted", corrupted);
}

void SpellCheckHostMetrics::RecordSuggestionStats(int delta) {
  suggestion_show_count_ += delta;
  // RecordReplacedWordStats() Calls RecordWordCounts() eventually.
  RecordReplacedWordStats(0);
}

void SpellCheckHostMetrics::RecordReplacedWordStats(int delta) {
  replaced_word_count_ += delta;

  if (misspelled_word_count_) {
    // zero |misspelled_word_count_| is possible when an extension
    // gives the misspelling, which is not recorded as a part of this
    // metrics.
    int percentage = (100 * replaced_word_count_) / misspelled_word_count_;
    UMA_HISTOGRAM_PERCENTAGE("SpellCheck.ReplaceRatio", percentage);
  }

  if (suggestion_show_count_) {
    int percentage = (100 * replaced_word_count_) / suggestion_show_count_;
    UMA_HISTOGRAM_PERCENTAGE("SpellCheck.SuggestionHitRatio", percentage);
  }

  RecordWordCounts();
}

void SpellCheckHostMetrics::RecordWordCounts() {
  UMA_HISTOGRAM_COUNTS("SpellCheck.CheckedWords", spellchecked_word_count_);
  UMA_HISTOGRAM_COUNTS("SpellCheck.MisspelledWords", misspelled_word_count_);
  UMA_HISTOGRAM_COUNTS("SpellCheck.ReplacedWords", replaced_word_count_);
  UMA_HISTOGRAM_COUNTS("SpellCheck.UniqueWords", checked_word_hashes_.size());
  UMA_HISTOGRAM_COUNTS("SpellCheck.ShownSuggestions", suggestion_show_count_);
}
