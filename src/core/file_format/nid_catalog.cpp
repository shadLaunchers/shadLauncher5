// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "nid_catalog.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>

namespace Loader::ElfInfo {

namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

u32 RotL32(u32 x, int n) {
    return (x << n) | (x >> (32 - n));
}

std::array<u8, 20> Sha1(const std::vector<u8>& data) {
    u32 h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;

    std::vector<u8> msg(data.begin(), data.end());
    const u64 bit_len = static_cast<u64>(data.size()) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) {
        msg.push_back(0);
    }
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<u8>((bit_len >> (i * 8)) & 0xFF));
    }

    for (size_t chunk = 0; chunk < msg.size(); chunk += 64) {
        std::array<u32, 80> w{};
        for (int i = 0; i < 16; ++i) {
            w[static_cast<size_t>(i)] = (u32(msg[chunk + static_cast<size_t>(i) * 4]) << 24) |
                                        (u32(msg[chunk + static_cast<size_t>(i) * 4 + 1]) << 16) |
                                        (u32(msg[chunk + static_cast<size_t>(i) * 4 + 2]) << 8) |
                                        u32(msg[chunk + static_cast<size_t>(i) * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[static_cast<size_t>(i)] =
                RotL32(w[static_cast<size_t>(i - 3)] ^ w[static_cast<size_t>(i - 8)] ^
                           w[static_cast<size_t>(i - 14)] ^ w[static_cast<size_t>(i - 16)],
                       1);
        }

        u32 a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            u32 f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            const u32 temp = RotL32(a, 5) + f + e + k + w[static_cast<size_t>(i)];
            e = d;
            d = c;
            c = RotL32(b, 30);
            b = a;
            a = temp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<u8, 20> out{};
    const u32 hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[static_cast<size_t>(i) * 4 + 0] = static_cast<u8>((hs[i] >> 24) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 1] = static_cast<u8>((hs[i] >> 16) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 2] = static_cast<u8>((hs[i] >> 8) & 0xFF);
        out[static_cast<size_t>(i) * 4 + 3] = static_cast<u8>(hs[i] & 0xFF);
    }
    return out;
}

constexpr std::array<u8, 16> kSalt{0x51, 0x8D, 0x64, 0xA6, 0x35, 0xDE, 0xD8, 0xC1,
                                   0xE6, 0xB0, 0x39, 0xB1, 0xC3, 0xE5, 0x52, 0x30};

constexpr std::string_view kB64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

std::string EncodeNid(const std::array<u8, 8>& bytes) {
    std::string nid;
    nid.reserve(11);

    for (size_t chunk_start = 0; chunk_start < bytes.size(); chunk_start += 3) {
        const size_t chunk_len = std::min<size_t>(3, bytes.size() - chunk_start);
        const u32 b0 = bytes[chunk_start];
        const u32 b1 = chunk_len > 1 ? bytes[chunk_start + 1] : 0;
        const u32 b2 = chunk_len > 2 ? bytes[chunk_start + 2] : 0;
        const u32 triple = (b0 << 16) | (b1 << 8) | b2;

        nid.push_back(kB64Alphabet[(triple >> 18) & 63]);
        nid.push_back(kB64Alphabet[(triple >> 12) & 63]);
        if (chunk_len > 1) {
            nid.push_back(kB64Alphabet[(triple >> 6) & 63]);
        }
        if (chunk_len > 2) {
            nid.push_back(kB64Alphabet[triple & 63]);
        }
    }

    if (nid.size() > 11) {
        nid.resize(11);
    }
    return nid;
}

} // namespace

std::string ComputeNid(std::string_view name) {
    std::vector<u8> input(name.begin(), name.end());
    input.insert(input.end(), kSalt.begin(), kSalt.end());
    const auto digest = Sha1(input);

    std::array<u8, 8> reversed{};
    for (int i = 0; i < 8; ++i) {
        reversed[static_cast<size_t>(i)] = digest[7 - static_cast<size_t>(i)];
    }

    return EncodeNid(reversed);
}

void NidCatalog::Add(std::string_view name) {
    by_nid_.insert_or_assign(ComputeNid(name), std::string(name));
}

std::optional<std::string> NidCatalog::Resolve(const std::string& nid) const {
    const auto it = by_nid_.find(nid);
    if (it == by_nid_.end()) {
        return std::nullopt;
    }
    return it->second;
}

namespace {

std::string_view Trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start &&
           (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
        --end;
    }
    return s.substr(start, end - start);
}

std::vector<std::string_view> SplitLines(std::string_view content) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start <= content.size()) {
        size_t pos = content.find('\n', start);
        if (pos == std::string_view::npos) {
            lines.push_back(content.substr(start));
            break;
        }
        lines.push_back(content.substr(start, pos - start));
        start = pos + 1;
    }
    return lines;
}

std::vector<std::string_view> SplitN(std::string_view s, char sep, size_t max_fields) {
    std::vector<std::string_view> fields;
    size_t start = 0;
    while (fields.size() + 1 < max_fields) {
        size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            break;
        }
        fields.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    fields.push_back(s.substr(start));
    return fields;
}

std::string_view Field(const std::vector<std::string_view>& fields, size_t index) {
    return index < fields.size() ? fields[index] : std::string_view();
}

std::string ReadWholeFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

} // namespace

size_t NidCatalog::LoadNamesFile(const std::filesystem::path& path) {
    const std::string content = ReadWholeFile(path);
    if (content.empty()) {
        return 0;
    }

    size_t count = 0;
    for (auto line : SplitLines(content)) {
        const auto trimmed = Trim(line);
        if (!trimmed.empty() && trimmed.front() != '#') {
            Add(trimmed);
            ++count;
        }
    }
    return count;
}

size_t NidCatalog::LoadNidsCsv(std::string_view content) {
    std::optional<std::string_view> first_non_empty;
    for (auto line : SplitLines(content)) {
        const auto t = Trim(line);
        if (!t.empty() && t.front() != '#') {
            first_non_empty = line;
            break;
        }
    }

    if (first_non_empty && first_non_empty->find(',') != std::string_view::npos) {
        return LoadNidsCsvRich(*first_non_empty, content);
    }
    return LoadNidsCsvLegacy(content);
}

size_t NidCatalog::LoadNidsCsvFile(const std::filesystem::path& path) {
    const std::string content = ReadWholeFile(path);
    if (content.empty()) {
        return 0;
    }
    return LoadNidsCsv(content);
}

size_t NidCatalog::LoadNidsCsvRich(std::string_view first_line, std::string_view content) {
    const std::string_view header = Trim(first_line);
    const auto header_cols = SplitN(header, ',', 2);
    const bool skip_header = Trim(Field(header_cols, 0)) == "nid";
    const bool is_six_col_header = header.find("nid_hex") != std::string_view::npos;

    size_t count = 0;
    for (auto raw_line : SplitLines(content)) {
        const auto line = Trim(raw_line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        if (skip_header && line == header) {
            continue;
        }

        const auto comma_count = static_cast<size_t>(std::count(line.begin(), line.end(), ','));
        const bool is_six_col = is_six_col_header || comma_count == 5;

        if (is_six_col) {
            const auto cols = SplitN(line, ',', 6);
            const auto nid = Trim(Field(cols, 0));
            const auto name = Trim(Field(cols, 2));
            if (!nid.empty() && !name.empty()) {
                by_nid_.insert_or_assign(std::string(nid), std::string(name));
                ++count;
            }
        } else {
            const auto cols = SplitN(line, ',', 5);
            const auto nid = Trim(Field(cols, 0));
            const auto name = Trim(Field(cols, 1));
            if (!nid.empty() && !name.empty()) {
                by_nid_.insert_or_assign(std::string(nid), std::string(name));
                ++count;
            }
        }
    }
    return count;
}

size_t NidCatalog::LoadNidsCsvLegacy(std::string_view content) {
    size_t count = 0;
    for (auto raw_line : SplitLines(content)) {
        const auto line = Trim(raw_line);
        if (line.empty() || line.front() == '#') {
            continue;
        }
        const auto space_pos = line.find(' ');
        if (space_pos == std::string_view::npos) {
            continue;
        }
        const auto nid = line.substr(0, space_pos);
        const auto name = Trim(line.substr(space_pos + 1));
        if (!nid.empty() && !name.empty()) {
            by_nid_.insert_or_assign(std::string(nid), std::string(name));
            ++count;
        }
    }
    return count;
}

void NidCatalog::AddBuiltins() {
    static constexpr std::string_view kNames[] = {
        "memcpy",
        "memmove",
        "memset",
        "memcmp",
        "memchr",
        "strlen",
        "strnlen",
        "strcmp",
        "strncmp",
        "strcpy",
        "strncpy",
        "strcat",
        "strncat",
        "strchr",
        "strrchr",
        "strstr",
        "strtol",
        "strtoul",
        "strtod",
        "strdup",
        "snprintf",
        "vsnprintf",
        "sprintf",
        "printf",
        "fprintf",
        "puts",
        "putchar",
        "malloc",
        "calloc",
        "realloc",
        "free",
        "aligned_alloc",
        "posix_memalign",
        "abort",
        "exit",
        "atexit",
        "__cxa_atexit",
        "__cxa_finalize",
        "qsort",
        "bsearch",
        "getenv",
        "rand",
        "srand",
        "fopen",
        "fclose",
        "fread",
        "fwrite",
        "fseek",
        "ftell",
        "fflush",
        "fgets",
        "fputs",
        "setvbuf",
        "pow",
        "sqrt",
        "sin",
        "cos",
        "tan",
        "atan2",
        "floor",
        "ceil",
        "fmod",
        "log",
        "exp",
        "__stack_chk_fail",
        "__stack_chk_guard",
        "__memcpy_chk",
        "__memset_chk",
        "__cxa_guard_acquire",
        "__cxa_guard_release",
        "__cxa_throw",
        "__cxa_begin_catch",
        "__cxa_end_catch",
        "_Znwm",
        "_Znam",
        "_ZdlPv",
        "_ZdaPv",
        "__gxx_personality_v0",
        "scePthreadCreate",
        "scePthreadJoin",
        "scePthreadExit",
        "scePthreadMutexInit",
        "scePthreadMutexLock",
        "scePthreadMutexUnlock",
        "scePthreadMutexDestroy",
        "scePthreadMutexTrylock",
        "scePthreadCondInit",
        "scePthreadCondWait",
        "scePthreadCondSignal",
        "scePthreadCondBroadcast",
        "scePthreadCondDestroy",
        "scePthreadSelf",
        "scePthreadOnce",
        "scePthreadKeyCreate",
        "scePthreadSetspecific",
        "scePthreadGetspecific",
        "scePthreadKeyDelete",
        "scePthreadEqual",
        "scePthreadYield",
        "scePthreadAttrInit",
        "scePthreadAttrDestroy",
        "scePthreadAttrSetstacksize",
        "scePthreadAttrSetdetachstate",
        "pthread_create",
        "pthread_join",
        "pthread_mutex_lock",
        "pthread_mutex_unlock",
        "pthread_cond_wait",
        "pthread_cond_signal",
        "pthread_self",
        "pthread_once",
        "sceKernelAllocateDirectMemory",
        "sceKernelReleaseDirectMemory",
        "sceKernelMapDirectMemory",
        "sceKernelMapNamedFlexibleMemory",
        "sceKernelMapFlexibleMemory",
        "sceKernelReserveVirtualRange",
        "sceKernelMunmap",
        "sceKernelMmap",
        "sceKernelVirtualQuery",
        "sceKernelSetVirtualRangeName",
        "sceKernelGetProcParam",
        "sceKernelLoadStartModule",
        "sceKernelDlsym",
        "sceKernelGetModuleInfo",
        "sceKernelError",
        "sceKernelUsleep",
        "sceKernelSleep",
        "sceKernelNanosleep",
        "sceKernelGettimeofday",
        "sceKernelClockGettime",
        "sceKernelGetProcessTime",
        "sceKernelGetTscFrequency",
        "sceKernelCreateEqueue",
        "sceKernelWaitEqueue",
        "sceKernelDeleteEqueue",
        "sceKernelCreateEventFlag",
        "sceKernelWaitEventFlag",
        "sceKernelSetEventFlag",
        "sceKernelCreateSema",
        "sceKernelWaitSema",
        "sceKernelSignalSema",
        "sceKernelOpen",
        "sceKernelClose",
        "sceKernelRead",
        "sceKernelWrite",
        "sceKernelLseek",
        "sceKernelStat",
        "sceKernelFstat",
        "open",
        "close",
        "read",
        "write",
        "lseek",
        "stat",
        "fstat",
        "mmap",
        "munmap",
        "clock_gettime",
        "gettimeofday",
        "nanosleep",
        "usleep",
    };

    for (const auto name : kNames) {
        Add(name);
    }
}

NidCatalog::NidCatalog() {
    AddBuiltins();
}

NidCatalog& GetMutableDefaultNidCatalog() {
    static NidCatalog catalog;
    return catalog;
}

const NidCatalog& GetDefaultNidCatalog() {
    return GetMutableDefaultNidCatalog();
}

} // namespace Loader::ElfInfo
