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
#include <vector>

namespace Core::Input::TextJson {

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
    std::string leading; // whitespace/comments/comma immediately before this element, verbatim
    std::string text;    // the element's own value text, verbatim
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
