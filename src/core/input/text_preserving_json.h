// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
//
// input-bindings.md section 3: "An editor that round-trips one of these
// files through a JSON parser and writes it back will destroy every comment
// in it." This is a minimal, comment- and string-aware text scanner that
// lets a config class locate one top-level key's value (or one array's
// individual elements) by byte range in the *original file text*, so a save
// can splice in just what changed and leave everything else -- including
// every comment, anywhere in the file -- byte-for-byte untouched.
//
// This is not a general JSON editor: it only understands enough structure
// to find top-level object keys and split one array's elements. It does not
// parse values themselves (that's nlohmann::json's job, applied to one
// element's text in isolation when the caller needs to read it).

#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Input::TextJson {

// Parse the way the emulator parses: comments ignored, and trailing commas
// tolerated. The emulator passes ignore_trailing_commas (see ParseFile in
// src/core/input/input_config.cpp), so a file with one loads there and was
// being reported as unreadable here -- the editor refusing to open something
// the emulator is perfectly happy with.
//
// That argument only exists in newer nlohmann, and the launcher pins a
// different fork of it than the emulator does, so whether it is available is
// not something this file can assume. Detecting it keeps both cases correct
// instead of trading one version's breakage for another's.
template <typename Json, typename = void>
struct AcceptsTrailingCommas : std::false_type {};

template <typename Json>
struct AcceptsTrailingCommas<
    Json, std::void_t<decltype(Json::parse(std::declval<const std::string&>(), nullptr, true, true,
                                            true))>> : std::true_type {};

template <typename Json>
[[nodiscard]] Json ParseTolerant(const std::string& text) {
    if constexpr (AcceptsTrailingCommas<Json>::value) {
        return Json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                           /*ignore_comments=*/true, /*ignore_trailing_commas=*/true);
    } else {
        return Json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                           /*ignore_comments=*/true);
    }
}

struct TopLevelEntry {
    std::string key;
    size_t value_start = 0; // index of the value's first character
    size_t value_end = 0;   // index one past the value's last character
};

struct RootScan {
    bool ok = false;
    size_t open_brace = 0;
    size_t close_brace = 0;
    std::vector<TopLevelEntry> entries;
};

// Walks a JSON object's direct keys (comment- and string-aware; tolerates
// trailing commas). Does not recurse into nested values beyond skipping
// over them, so nested keys named the same as a top-level one are never
// confused for it. ok is false if `text` isn't an object at all.
[[nodiscard]] RootScan ScanRoot(const std::string& text);

struct ArrayElement {
    // Whitespace and comments immediately before this element, verbatim.
    // The separating comma is NOT part of this: it is structure, not
    // trivia, and keeping it here would put a stray comma at the front of
    // the rebuilt array whenever the first element is the one being
    // replaced. A caller reassembling elements emits its own commas.
    std::string leading;
    std::string text; // the element's own value text, verbatim
};

struct ArrayContent {
    bool ok = false;
    std::vector<ArrayElement> elements;
    std::string trailing; // whitespace/comments after the last element, before ']'
};

// Splits the inner text of a "[ ... ]" span -- NOT including the brackets --
// into its elements, each keeping whatever preceded it verbatim.
[[nodiscard]] ArrayContent SplitArray(const std::string& inner_text);

// Replaces (or, if absent, appends before the closing brace) a top-level
// key's value in `text` with `new_value_text`. Every other key, and every
// comment anywhere else in the file, is left exactly as it was. Returns the
// original text unchanged if `text` isn't a JSON object.
[[nodiscard]] std::string ReplaceOrInsertTopLevelValue(const std::string& text,
                                                       const std::string& key,
                                                       const std::string& new_value_text);

} // namespace Core::Input::TextJson
