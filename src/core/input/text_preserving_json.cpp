// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cctype>

#include "text_preserving_json.h"

namespace Core::Input::TextJson {

size_t SkipWs(const std::string& s, size_t pos) {
    while (pos < s.size()) {
        const char c = s[pos];
        if (std::isspace(static_cast<unsigned char>(c))) {
            pos++;
            continue;
        }
        if (c == '/' && pos + 1 < s.size() && s[pos + 1] == '/') {
            pos += 2;
            while (pos < s.size() && s[pos] != '\n') {
                pos++;
            }
            continue;
        }
        if (c == '/' && pos + 1 < s.size() && s[pos + 1] == '*') {
            pos += 2;
            while (pos + 1 < s.size() && !(s[pos] == '*' && s[pos + 1] == '/')) {
                pos++;
            }
            pos = pos + 1 < s.size() ? pos + 2 : s.size();
            continue;
        }
        break;
    }
    return pos;
}

// s[pos] must be '"'. Returns the index one past the closing quote.
size_t SkipString(const std::string& s, size_t pos) {
    pos++; // opening quote
    while (pos < s.size()) {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            pos += 2;
            continue;
        }
        if (s[pos] == '"') {
            pos++;
            break;
        }
        pos++;
    }
    return pos;
}

// Returns the index one past one complete JSON value starting at pos (pos
// must point at the value's first non-trivia character). Depth-tracks
// braces/brackets and skips over strings/comments so nested commas, braces
// in strings, etc. never confuse it.
size_t SkipValue(const std::string& s, size_t pos) {
    if (pos >= s.size()) {
        return pos;
    }
    const char c = s[pos];
    if (c == '"') {
        return SkipString(s, pos);
    }
    if (c == '{' || c == '[') {
        const char open = c;
        const char close = (c == '{') ? '}' : ']';
        int depth = 0;
        while (pos < s.size()) {
            const char cur = s[pos];
            if (cur == '"') {
                pos = SkipString(s, pos);
                continue;
            }
            if (cur == '/' && pos + 1 < s.size() && (s[pos + 1] == '/' || s[pos + 1] == '*')) {
                pos = SkipWs(s, pos);
                continue;
            }
            if (cur == open) {
                depth++;
            } else if (cur == close) {
                depth--;
                pos++;
                if (depth == 0) {
                    break;
                }
                continue;
            }
            pos++;
        }
        return pos;
    }
    // number, true/false/null, or an unquoted token -- read to a delimiter.
    while (pos < s.size()) {
        const char cur = s[pos];
        if (cur == ',' || cur == '}' || cur == ']' ||
            std::isspace(static_cast<unsigned char>(cur))) {
            break;
        }
        if (cur == '/' && pos + 1 < s.size() && (s[pos + 1] == '/' || s[pos + 1] == '*')) {
            break;
        }
        pos++;
    }
    return pos;
}

RootScan ScanRoot(const std::string& text) {
    RootScan result;
    size_t pos = SkipWs(text, 0);
    if (pos >= text.size() || text[pos] != '{') {
        return result; // not an object
    }
    result.open_brace = pos;
    pos++;

    while (true) {
        pos = SkipWs(text, pos);
        if (pos >= text.size()) {
            return RootScan{}; // malformed unterminated object
        }
        if (text[pos] == '}') {
            result.close_brace = pos;
            result.ok = true;
            return result;
        }
        if (text[pos] != '"') {
            return RootScan{}; // expected a key
        }
        const size_t key_start = pos;
        pos = SkipString(text, pos);
        // Keys in these files are plain identifiers,no escapes to unescape.
        const std::string key = text.substr(key_start + 1, pos - key_start - 2);

        pos = SkipWs(text, pos);
        if (pos >= text.size() || text[pos] != ':') {
            return RootScan{};
        }
        pos++;
        pos = SkipWs(text, pos);

        const size_t value_start = pos;
        pos = SkipValue(text, pos);
        if (pos == value_start) {
            return RootScan{}; // couldn't read a value
        }
        result.entries.push_back({key, value_start, pos});

        pos = SkipWs(text, pos);
        if (pos < text.size() && text[pos] == ',') {
            pos++;
        }
    }
}

ArrayContent SplitArray(const std::string& inner) {
    ArrayContent result;
    result.ok = true;
    size_t pos = 0;

    while (true) {
        const size_t trivia_start = pos;
        pos = SkipWs(inner, pos);
        if (pos >= inner.size()) {
            result.trailing = inner.substr(trivia_start);
            break;
        }
        if (inner[pos] == ',') {
            pos++;
            continue;
        }
        const size_t value_start = pos;
        pos = SkipValue(inner, pos);
        if (pos == value_start) {
            result.ok = false;
            return result;
        }
        result.elements.push_back({inner.substr(trivia_start, value_start - trivia_start),
                                   inner.substr(value_start, pos - value_start)});

        const size_t after_value = pos;
        pos = SkipWs(inner, pos);
        if (pos < inner.size() && inner[pos] == ',') {
            pos++;
            continue;
        }
        result.trailing = inner.substr(after_value);
        break;
    }
    return result;
}

std::string ReplaceOrInsertTopLevelValue(const std::string& text, const std::string& key,
                                         const std::string& new_value_text) {
    const RootScan scan = ScanRoot(text);
    if (!scan.ok) {
        return text; // can't safely edit something we can't parse the shape of
    }

    for (const auto& entry : scan.entries) {
        if (entry.key == key) {
            return text.substr(0, entry.value_start) + new_value_text +
                   text.substr(entry.value_end);
        }
    }

    // Key not present
    if (scan.entries.empty()) {
        const std::string insertion = "    \"" + key + "\": " + new_value_text + "\n";
        return text.substr(0, scan.close_brace) + insertion + text.substr(scan.close_brace);
    }
    const size_t after_last = scan.entries.back().value_end;
    const std::string insertion = ",\n    \"" + key + "\": " + new_value_text;
    return text.substr(0, after_last) + insertion + text.substr(after_last);
}

std::string StripTrailingCommas(const std::string& text) {
    std::string out;
    out.reserve(text.size());

    size_t pos = 0;
    while (pos < text.size()) {
        const char c = text[pos];

        if (c == '"') {
            const size_t end = SkipString(text, pos);
            out.append(text, pos, end - pos);
            pos = end;
            continue;
        }
        if (c == '/' && pos + 1 < text.size() && (text[pos + 1] == '/' || text[pos + 1] == '*')) {
            const size_t end = SkipWs(text, pos); // consumes this comment and any run after it
            out.append(text, pos, end - pos);
            pos = end;
            continue;
        }
        if (c == ',') {
            const size_t next = SkipWs(text, pos + 1);
            if (next < text.size() && (text[next] == ']' || text[next] == '}')) {
                out.append(text, pos + 1, next - (pos + 1));
                pos = next;
                continue;
            }
        }

        out.push_back(c);
        pos++;
    }
    return out;
}

} // namespace Core::Input::TextJson
