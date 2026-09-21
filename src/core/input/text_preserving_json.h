// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace Core::Input::TextJson {

template <typename Json, typename = void>
struct AcceptsTrailingCommas : std::false_type {};

template <typename Json>
struct AcceptsTrailingCommas<Json,
                             std::void_t<decltype(Json::parse(std::declval<const std::string&>(),
                                                              nullptr, true, true, true))>>
    : std::true_type {};

[[nodiscard]] std::string StripTrailingCommas(const std::string& text);

template <typename Json>
[[nodiscard]] Json ParseTolerant(const std::string& text) {
    if constexpr (AcceptsTrailingCommas<Json>::value) {
        return Json::parse(text, /*cb=*/nullptr, /*allow_exceptions=*/true,
                           /*ignore_comments=*/true, /*ignore_trailing_commas=*/true);
    } else {
        return Json::parse(StripTrailingCommas(text), /*cb=*/nullptr, /*allow_exceptions=*/true,
                           /*ignore_comments=*/true);
    }
}

struct TopLevelEntry {
    std::string key;
    size_t value_start = 0;
    size_t value_end = 0;
};

struct RootScan {
    bool ok = false;
    size_t open_brace = 0;
    size_t close_brace = 0;
    std::vector<TopLevelEntry> entries;
};

[[nodiscard]] RootScan ScanRoot(const std::string& text);

struct ArrayElement {
    std::string leading;
    std::string text;
};

struct ArrayContent {
    bool ok = false;
    std::vector<ArrayElement> elements;
    std::string trailing;
};

[[nodiscard]] ArrayContent SplitArray(const std::string& inner_text);
[[nodiscard]] std::string ReplaceOrInsertTopLevelValue(const std::string& text,
                                                       const std::string& key,
                                                       const std::string& new_value_text);

} // namespace Core::Input::TextJson
