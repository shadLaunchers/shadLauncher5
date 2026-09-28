// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "elf_info.h"

#include <cstdint>
#include <cstring>
#include <sstream>

namespace Loader::ElfInfo {

namespace {

constexpr u8 kSelfMagic[4] = {0x4F, 0x15, 0x3D, 0x1D};
constexpr u8 kSelfMagic2[4] = {0x54, 0x14, 0xF5, 0xEE};
constexpr u8 kElfMagic[4] = {0x7F, 'E', 'L', 'F'};

bool HasMagic(const std::vector<u8>& data, size_t offset, const u8* magic, size_t magic_len) {
    if (offset + magic_len > data.size()) {
        return false;
    }
    return std::memcmp(data.data() + offset, magic, magic_len) == 0;
}

template <typename T>
bool ReadAt(const std::vector<u8>& data, size_t offset, T& out) {
    if (offset + sizeof(T) > data.size()) {
        return false;
    }
    std::memcpy(&out, data.data() + offset, sizeof(T));
    return true;
}

} // namespace

std::string Hex(u64 value, int width) {
    std::ostringstream ss;
    ss << "0x" << std::hex;
    if (width > 0) {
        ss.width(width);
        ss.fill('0');
    }
    ss << value;
    return ss.str();
}

std::string ElfClassName(u8 v) {
    switch (v) {
    case 1:
        return "ELFCLASS32";
    case 2:
        return "ELFCLASS64";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string ElfDataName(u8 v) {
    switch (v) {
    case 1:
        return "little-endian (LSB)";
    case 2:
        return "big-endian (MSB)";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string ElfOsAbiName(u8 v) {
    switch (v) {
    case 9:
        return "FreeBSD (used by PS4/PS5 executables)";
    case 0:
        return "System V";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string ElfTypeName(u16 v) {
    switch (v) {
    case 1:
        return "ET_REL (relocatable)";
    case 2:
        return "ET_EXEC (executable)";
    case 3:
        return "ET_DYN (shared object)";
    case 0xfe10:
        return "ET_SCE_DYNEXEC (PS4/PS5 executable)";
    case 0xfe18:
        return "ET_SCE_DYNAMIC (PS4/PS5 shared object)";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string ElfMachineName(u16 v) {
    if (v == 62) {
        return "EM_X86_64";
    }
    return "unknown (" + Hex(v) + ")";
}

std::string PhdrTypeName(u32 v) {
    switch (v) {
    case 0:
        return "PT_NULL";
    case 1:
        return "PT_LOAD";
    case 2:
        return "PT_DYNAMIC";
    case 3:
        return "PT_INTERP";
    case 4:
        return "PT_NOTE";
    case 6:
        return "PT_PHDR";
    case 7:
        return "PT_TLS";
    case 0x6474e550:
        return "PT_GNU_EH_FRAME";
    case 0x61000000:
        return "PT_SCE_DYNLIBDATA (PS4/PS5)";
    case 0x61000001:
        return "PT_SCE_PROCPARAM (PS4/PS5)";
    case 0x61000010:
        return "PT_SCE_RELRO (PS4/PS5)";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string PhdrFlagsName(u32 v) {
    std::string s;
    s += (v & 0x4) ? 'R' : '-';
    s += (v & 0x2) ? 'W' : '-';
    s += (v & 0x1) ? 'X' : '-';
    return s;
}

std::string ShdrTypeName(u32 v) {
    switch (v) {
    case 0:
        return "SHT_NULL";
    case 1:
        return "SHT_PROGBITS";
    case 2:
        return "SHT_SYMTAB";
    case 3:
        return "SHT_STRTAB";
    case 4:
        return "SHT_RELA";
    case 5:
        return "SHT_HASH";
    case 6:
        return "SHT_DYNAMIC";
    case 7:
        return "SHT_NOTE";
    case 8:
        return "SHT_NOBITS";
    case 9:
        return "SHT_REL";
    case 11:
        return "SHT_DYNSYM";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string DynTagName(u64 v) {
    switch (v) {
    case 0:
        return "DT_NULL";
    case 1:
        return "DT_NEEDED";
    case 2:
        return "DT_PLTRELSZ";
    case 3:
        return "DT_PLTGOT";
    case 4:
        return "DT_HASH";
    case 5:
        return "DT_STRTAB";
    case 6:
        return "DT_SYMTAB";
    case 7:
        return "DT_RELA";
    case 8:
        return "DT_RELASZ";
    case 9:
        return "DT_RELAENT";
    case 10:
        return "DT_STRSZ";
    case 11:
        return "DT_SYMENT";
    case 12:
        return "DT_INIT";
    case 13:
        return "DT_FINI";
    case 14:
        return "DT_SONAME";
    case 15:
        return "DT_RPATH";
    case 16:
        return "DT_SYMBOLIC";
    case 17:
        return "DT_REL";
    case 18:
        return "DT_RELSZ";
    case 19:
        return "DT_RELENT";
    case 20:
        return "DT_PLTREL";
    case 21:
        return "DT_DEBUG";
    case 22:
        return "DT_TEXTREL";
    case 23:
        return "DT_JMPREL";
    case 24:
        return "DT_BIND_NOW";
    case 25:
        return "DT_INIT_ARRAY";
    case 26:
        return "DT_FINI_ARRAY";
    case 27:
        return "DT_INIT_ARRAYSZ";
    case 28:
        return "DT_FINI_ARRAYSZ";
    case 29:
        return "DT_RUNPATH";
    case 30:
        return "DT_FLAGS";
    case 32:
        return "DT_PREINIT_ARRAY";
    case 33:
        return "DT_PREINIT_ARRAYSZ";
    case 0x6ffffff9:
        return "DT_RELACOUNT";
    case 0x61000007:
        return "DT_OS_FINGERPRINT";
    case 0x61000009:
        return "DT_OS_ORIGINAL_FILENAME";
    case 0x6100000d:
        return "DT_OS_MODULE_INFO";
    case 0x6100000f:
        return "DT_OS_NEEDED_MODULE";
    case 0x61000011:
        return "DT_OS_MODULE_ATTR";
    case 0x61000013:
        return "DT_OS_EXPORT_LIB";
    case 0x61000015:
        return "DT_OS_IMPORT_LIB";
    case 0x61000017:
        return "DT_OS_EXPORT_LIB_ATTR";
    case 0x61000019:
        return "DT_OS_IMPORT_LIB_ATTR";
    case 0x61000025:
        return "DT_OS_HASH";
    case 0x61000027:
        return "DT_OS_PLTGOT";
    case 0x61000029:
        return "DT_OS_JMPREL";
    case 0x6100002b:
        return "DT_OS_PLTREL";
    case 0x6100002d:
        return "DT_OS_PLTRELSZ";
    case 0x6100002f:
        return "DT_OS_RELA";
    case 0x61000031:
        return "DT_OS_RELASZ";
    case 0x61000033:
        return "DT_OS_RELAENT";
    case 0x61000035:
        return "DT_OS_STRTAB";
    case 0x61000037:
        return "DT_OS_STRSZ";
    case 0x61000039:
        return "DT_OS_SYMTAB";
    case 0x6100003b:
        return "DT_OS_SYMENT";
    case 0x6100003d:
        return "DT_OS_HASHSZ";
    case 0x6100003f:
        return "DT_OS_SYMTABSZ";
    case 0x61000041:
        return "DT_OS_ORIGINAL_FILENAME_1";
    case 0x61000043:
        return "DT_OS_MODULE_INFO_1";
    case 0x61000045:
        return "DT_OS_NEEDED_MODULE_1";
    case 0x61000047:
        return "DT_OS_EXPORT_LIB_1";
    case 0x61000049:
        return "DT_OS_IMPORT_LIB_1";
    default:
        return "unknown (" + Hex(v) + ")";
    }
}

std::string HexDump(const std::vector<u8>& bytes, u64 base_offset) {
    std::ostringstream out;
    for (size_t row = 0; row < bytes.size(); row += 16) {
        out << Hex(base_offset + row, 8) << "  ";

        const size_t row_len = std::min<size_t>(16, bytes.size() - row);
        for (size_t col = 0; col < 16; col++) {
            if (col < row_len) {
                out.width(2);
                out.fill('0');
                out << std::hex << static_cast<unsigned>(bytes[row + col]) << std::dec;
            } else {
                out << "  ";
            }
            out << (col == 7 ? "  " : " ");
        }

        out << " ";
        for (size_t col = 0; col < row_len; col++) {
            const u8 c = bytes[row + col];
            out << (c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.');
        }
        out << "\n";
    }
    return out.str();
}

namespace {

std::optional<std::vector<u8>> ReadFileRange(const std::vector<u8>& data, const ParsedInfo& info,
                                             u64 file_offset, u64 size,
                                             bool* was_compressed = nullptr) {
    if (was_compressed != nullptr) {
        *was_compressed = false;
    }

    if (!info.is_self) {
        if (file_offset + size > data.size()) {
            return std::nullopt;
        }
        return std::vector<u8>(data.begin() + static_cast<ptrdiff_t>(file_offset),
                               data.begin() + static_cast<ptrdiff_t>(file_offset + size));
    }

    for (const auto& seg : info.self_segments) {
        const u64 type = seg.type;
        if ((type & 0x800) == 0) {
            continue;
        }
        const auto phdr_id = static_cast<size_t>((type >> 20) & 0xFFF);
        if (phdr_id >= info.phdrs.size()) {
            continue;
        }
        const auto& phdr = info.phdrs[phdr_id];
        if (file_offset < static_cast<u64>(phdr.p_offset) ||
            file_offset >= static_cast<u64>(phdr.p_offset) + static_cast<u64>(phdr.p_filesz)) {
            continue;
        }

        if (static_cast<u64>(seg.compressed_size) != static_cast<u64>(seg.decompressed_size)) {
            if (was_compressed != nullptr) {
                *was_compressed = true;
            }
            return std::nullopt;
        }

        const u64 rel = file_offset - static_cast<u64>(phdr.p_offset);
        const u64 physical = static_cast<u64>(seg.offset) + rel;
        if (rel + size > static_cast<u64>(seg.decompressed_size) || physical + size > data.size()) {
            return std::nullopt;
        }
        return std::vector<u8>(data.begin() + static_cast<ptrdiff_t>(physical),
                               data.begin() + static_cast<ptrdiff_t>(physical + size));
    }

    return std::nullopt; // no self segment covers this offset
}

std::optional<u64> VaddrToFileOffset(const ParsedInfo& info, u64 vaddr) {
    for (const auto& p : info.phdrs) {
        const u64 v0 = p.p_vaddr;
        const u64 v1 = v0 + static_cast<u64>(p.p_memsz);
        if (vaddr >= v0 && vaddr < v1) {
            return static_cast<u64>(p.p_offset) + (vaddr - v0);
        }
    }
    return std::nullopt;
}

std::string ExtractCString(const std::vector<u8>& blob, u64 offset) {
    if (offset >= blob.size()) {
        return {};
    }
    const auto* start = reinterpret_cast<const char*>(blob.data() + offset);
    const size_t max_len = blob.size() - offset;
    const size_t len = strnlen(start, max_len);
    return std::string(start, len);
}

std::string EncodeId64(u16 in_id) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    std::string out;
    if (in_id < 0x40u) {
        out += kAlphabet[in_id];
    } else if (in_id < 0x1000u) {
        out += kAlphabet[(in_id >> 6u) & 0x3fu];
        out += kAlphabet[in_id & 0x3fu];
    } else {
        out += kAlphabet[(in_id >> 12u) & 0x3fu];
        out += kAlphabet[(in_id >> 6u) & 0x3fu];
        out += kAlphabet[in_id & 0x3fu];
    }
    return out;
}

std::optional<size_t> FindPhdrByType(const ParsedInfo& info, u32 type) {
    for (size_t i = 0; i < info.phdrs.size(); i++) {
        if (static_cast<u32>(info.phdrs[i].p_type) == type) {
            return i;
        }
    }
    return std::nullopt;
}

void DecodeModuleIds(const ParsedInfo& info, const std::vector<u8>& strtab, u64 tag,
                     std::vector<ModuleIdInfo>& out) {
    for (const auto& e : info.dynamic.entries) {
        if (static_cast<u64>(e.d_tag) != tag) {
            continue;
        }
        const u64 need = e.d_val;
        ModuleIdInfo m;
        m.id = EncodeId64(static_cast<u16>((need >> 48u) & 0xffffu));
        m.version_major = static_cast<int>((need >> 40u) & 0xffu);
        m.version_minor = static_cast<int>((need >> 32u) & 0xffu);
        m.name = ExtractCString(strtab, need & 0xffffffffu);
        out.push_back(std::move(m));
    }
}

void DecodeLibraryIds(const ParsedInfo& info, const std::vector<u8>& strtab, u64 tag,
                      std::vector<LibraryIdInfo>& out) {
    for (const auto& e : info.dynamic.entries) {
        if (static_cast<u64>(e.d_tag) != tag) {
            continue;
        }
        const u64 need = e.d_val;
        LibraryIdInfo l;
        l.id = EncodeId64(static_cast<u16>((need >> 48u) & 0xffffu));
        l.version = static_cast<int>((need >> 32u) & 0xffffu);
        l.name = ExtractCString(strtab, need & 0xffffffffu);
        out.push_back(std::move(l));
    }
}

void ParseDynamic(const std::vector<u8>& data, ParsedInfo& info) {
    size_t dynamic_phdr_index = SIZE_MAX;
    for (size_t i = 0; i < info.phdrs.size(); i++) {
        if (static_cast<u32>(info.phdrs[i].p_type) == 2 /* PT_DYNAMIC */) {
            dynamic_phdr_index = i;
            break;
        }
    }

    if (dynamic_phdr_index == SIZE_MAX) {
        info.dynamic.present = false;
        return;
    }
    info.dynamic.present = true;

    const auto& phdr = info.phdrs[dynamic_phdr_index];
    if (static_cast<u64>(phdr.p_filesz) % sizeof(Elf64Dyn) != 0) {
        info.dynamic.readable = false;
        info.dynamic.unavailable_reason = "PT_DYNAMIC size isn't a multiple of 16 bytes";
        return;
    }

    bool was_compressed = false;
    auto bytes = ReadFileRange(data, info, phdr.p_offset, phdr.p_filesz, &was_compressed);
    if (!bytes) {
        info.dynamic.readable = false;
        info.dynamic.unavailable_reason =
            was_compressed
                ? "the segment backing PT_DYNAMIC is SELF-compressed and can't be decoded here"
                : "couldn't locate PT_DYNAMIC's bytes in the file";
        return;
    }
    info.dynamic.readable = true;

    const size_t count = bytes->size() / sizeof(Elf64Dyn);
    info.dynamic.entries.reserve(count);
    for (size_t i = 0; i < count; i++) {
        Elf64Dyn entry{};
        std::memcpy(&entry, bytes->data() + i * sizeof(Elf64Dyn), sizeof(Elf64Dyn));
        info.dynamic.entries.push_back(entry);
        if (static_cast<u64>(entry.d_tag) == 0 /* DT_NULL */) {
            break;
        }
    }

    info.dynamic.entry_strings.assign(info.dynamic.entries.size(), std::string());

    auto find_tag = [&](u64 os_tag, u64 std_tag) -> std::optional<u64> {
        for (const auto& e : info.dynamic.entries) {
            // 0 means "no such variant" here (it would otherwise match the
            // DT_NULL terminator, whose tag is also 0).
            const u64 tag = static_cast<u64>(e.d_tag);
            if ((os_tag != 0 && tag == os_tag) || (std_tag != 0 && tag == std_tag)) {
                return static_cast<u64>(e.d_val);
            }
        }
        return std::nullopt;
    };
    auto add = [&](const std::string& label, const std::string& value) {
        info.dynamic.summary.emplace_back(label, value);
    };
    auto add_addr = [&](const char* label, std::optional<u64> v) {
        if (v)
            add(label, Hex(*v));
    };
    add_addr("Init function (DT_INIT)", find_tag(0, 12));
    add_addr("Fini function (DT_FINI)", find_tag(0, 13));
    if (auto v = find_tag(0, 25)) {
        auto sz = find_tag(0, 27);
        add("Init array", Hex(*v) + (sz ? ", " + std::to_string(*sz / 8) + " entries" : ""));
    }
    if (auto v = find_tag(0, 26)) {
        auto sz = find_tag(0, 28);
        add("Fini array", Hex(*v) + (sz ? ", " + std::to_string(*sz / 8) + " entries" : ""));
    }
    if (auto v = find_tag(0, 32)) {
        auto sz = find_tag(0, 33);
        add("Preinit array", Hex(*v) + (sz ? ", " + std::to_string(*sz / 8) + " entries" : ""));
    }
    add_addr("PLT GOT", find_tag(0x61000027, 3));
    if (auto sz = find_tag(0x6100002d, 2)) {
        std::string v =
            std::to_string(*sz / sizeof(u64) / 3) + " entries (" + std::to_string(*sz) + " bytes)";
        if (auto t = find_tag(0x6100002b, 20)) {
            v += *t == 7 ? ", RELA" : ", type " + Hex(*t);
        }
        add("PLT relocations (JMPREL)", v);
    }
    if (auto sz = find_tag(0x61000031, 8)) {
        u64 ent = find_tag(0x61000033, 9).value_or(24);
        add("RELA relocations",
            std::to_string(ent ? *sz / ent : 0) + " entries (" + std::to_string(*sz) + " bytes)");
    }
    if (auto v = find_tag(0, 0x6ffffff9)) {
        add("Relative relocations (DT_RELACOUNT)", std::to_string(*v));
    }
    if (auto sz = find_tag(0x6100003f, 0)) {
        u64 ent = find_tag(0x6100003b, 11).value_or(24);
        add("Symbol table",
            std::to_string(ent ? *sz / ent : 0) + " symbols (" + std::to_string(*sz) + " bytes)");
    }
    if (auto v = find_tag(0x6100003d, 0)) {
        add("Hash table size", std::to_string(*v) + " bytes");
    }
    if (auto v = find_tag(0x61000037, 10)) {
        add("String table size", std::to_string(*v) + " bytes");
    }
    add_addr("Flags (DT_FLAGS)", find_tag(0, 30));
    add_addr("Debug (DT_DEBUG)", find_tag(0, 21));
    add_addr("Text relocations (DT_TEXTREL)", find_tag(0, 22));

    std::optional<std::vector<u8>> strtab_bytes;

    std::optional<u64> os_strtab_offset;
    std::optional<u64> os_strtab_size;
    std::optional<u64> strtab_vaddr;
    std::optional<u64> strtab_size;
    for (const auto& e : info.dynamic.entries) {
        const u64 tag = e.d_tag;
        if (tag == 0x61000035 /* DT_OS_STRTAB */) {
            os_strtab_offset = e.d_val;
        } else if (tag == 0x61000037 /* DT_OS_STRSZ */) {
            os_strtab_size = e.d_val;
        } else if (tag == 5 /* DT_STRTAB */) {
            strtab_vaddr = e.d_val;
        } else if (tag == 10 /* DT_STRSZ */) {
            strtab_size = e.d_val;
        }
    }

    if (os_strtab_offset && os_strtab_size && *os_strtab_size > 0) {
        if (!FindPhdrByType(info, 0x61000000)) {
            info.dynamic.strings_unavailable_reason =
                "DT_OS_STRTAB is set but there is no PT_SCE_DYNLIBDATA segment to read it from";
        }
        if (auto dynlibdata_idx = FindPhdrByType(info, 0x61000000 /* PT_SCE_DYNLIBDATA */)) {
            const auto& dyn_phdr = info.phdrs[*dynlibdata_idx];
            bool strtab_compressed = false;
            strtab_bytes =
                ReadFileRange(data, info, static_cast<u64>(dyn_phdr.p_offset) + *os_strtab_offset,
                              *os_strtab_size, &strtab_compressed);
            if (!strtab_bytes) {
                info.dynamic.strings_unavailable_reason =
                    strtab_compressed
                        ? "PT_SCE_DYNLIBDATA is SELF-compressed and can't be decoded here"
                        : "couldn't locate the string table's bytes inside PT_SCE_DYNLIBDATA";
            }
        }
    }
    if (!strtab_bytes && strtab_vaddr && strtab_size && *strtab_size > 0) {
        if (auto strtab_offset = VaddrToFileOffset(info, *strtab_vaddr)) {
            strtab_bytes = ReadFileRange(data, info, *strtab_offset, *strtab_size);
        }
    }

    if (!strtab_bytes) {
        if (info.dynamic.strings_unavailable_reason.empty()) {
            info.dynamic.strings_unavailable_reason =
                "no DT_OS_STRTAB/DT_STRTAB (with a size) found, or it couldn't be located in the "
                "file";
        }
        return; // no string table resolvable - nothing further to decode
    }
    info.dynamic.strings_unavailable_reason.clear();

    for (size_t i = 0; i < info.dynamic.entries.size(); i++) {
        const u64 tag = info.dynamic.entries[i].d_tag;
        if (tag == 1 /* DT_NEEDED */ || tag == 14 /* DT_SONAME */ || tag == 15 /* DT_RPATH */ ||
            tag == 29 /* DT_RUNPATH */) {
            info.dynamic.entry_strings[i] =
                ExtractCString(*strtab_bytes, info.dynamic.entries[i].d_val);
        }
    }

    DecodeModuleIds(info, *strtab_bytes, 0x6100000f /* DT_OS_NEEDED_MODULE */,
                    info.dynamic.import_modules);
    DecodeModuleIds(info, *strtab_bytes, 0x61000045 /* DT_OS_NEEDED_MODULE_1 */,
                    info.dynamic.import_modules);
    DecodeModuleIds(info, *strtab_bytes, 0x6100000d /* DT_OS_MODULE_INFO */,
                    info.dynamic.export_modules);
    DecodeModuleIds(info, *strtab_bytes, 0x61000043 /* DT_OS_MODULE_INFO_1 */,
                    info.dynamic.export_modules);
    DecodeLibraryIds(info, *strtab_bytes, 0x61000015 /* DT_OS_IMPORT_LIB */,
                     info.dynamic.import_libs);
    DecodeLibraryIds(info, *strtab_bytes, 0x61000049 /* DT_OS_IMPORT_LIB_1 */,
                     info.dynamic.import_libs);
    DecodeLibraryIds(info, *strtab_bytes, 0x61000013 /* DT_OS_EXPORT_LIB */,
                     info.dynamic.export_libs);
    DecodeLibraryIds(info, *strtab_bytes, 0x61000047 /* DT_OS_EXPORT_LIB_1 */,
                     info.dynamic.export_libs);
}

} // namespace

std::optional<ParsedInfo> Parse(const std::vector<u8>& data) {
    if (data.size() < 16) {
        return std::nullopt;
    }

    ParsedInfo info;
    info.file_size = data.size();

    constexpr size_t kRawDumpMaxBytes = 256;
    info.raw_header.assign(data.begin(), data.begin() + std::min(data.size(), kRawDumpMaxBytes));

    if (HasMagic(data, 0, kSelfMagic, sizeof(kSelfMagic)) ||
        HasMagic(data, 0, kSelfMagic2, sizeof(kSelfMagic2))) {
        info.is_self = true;

        if (!ReadAt(data, 0, info.self_header)) {
            info.is_self = false; // magic matched but header itself is truncated
        } else {
            const size_t seg_table_offset = sizeof(SelfHeader);
            for (u16 i = 0; i < info.self_header.segments_num; i++) {
                SelfSegmentEntry seg{};
                if (!ReadAt(data, seg_table_offset + i * sizeof(SelfSegmentEntry), seg)) {
                    break; // segment directory itself is truncated; report what we got
                }
                info.self_segments.push_back(seg);
            }
        }
    }

    info.ehdr_file_offset =
        info.is_self ? sizeof(SelfHeader) + static_cast<u64>(info.self_header.segments_num) *
                                                sizeof(SelfSegmentEntry)
                     : 0;

    if (!HasMagic(data, info.ehdr_file_offset, kElfMagic, sizeof(kElfMagic)) ||
        !ReadAt(data, info.ehdr_file_offset, info.ehdr)) {
        info.is_valid_elf = false;
        if (info.is_self && info.ehdr_file_offset < data.size()) {
            const size_t dump_len = std::min(kRawDumpMaxBytes, data.size() - info.ehdr_file_offset);
            info.raw_at_ehdr_offset.assign(
                data.begin() + static_cast<ptrdiff_t>(info.ehdr_file_offset),
                data.begin() + static_cast<ptrdiff_t>(info.ehdr_file_offset + dump_len));
        }
        return info;
    }
    info.is_valid_elf = true;

    if (static_cast<u16>(info.ehdr.e_phentsize) == sizeof(Elf64Phdr)) {
        const u64 phdr_base = info.ehdr_file_offset + static_cast<u64>(info.ehdr.e_phoff);
        for (u16 i = 0; i < static_cast<u16>(info.ehdr.e_phnum); i++) {
            Elf64Phdr phdr{};
            if (!ReadAt(data, phdr_base + static_cast<u64>(i) * sizeof(Elf64Phdr), phdr)) {
                break; // phdr table truncated; report what we got
            }
            info.phdrs.push_back(phdr);
        }
    }

    if (static_cast<u16>(info.ehdr.e_shentsize) == sizeof(Elf64Shdr)) {
        const u64 shdr_base = info.ehdr_file_offset + static_cast<u64>(info.ehdr.e_shoff);
        for (u16 i = 0; i < static_cast<u16>(info.ehdr.e_shnum); i++) {
            Elf64Shdr shdr{};
            if (!ReadAt(data, shdr_base + static_cast<u64>(i) * sizeof(Elf64Shdr), shdr)) {
                break; // shdr table truncated; report what we got
            }
            info.shdrs.push_back(shdr);
        }
    }

    info.section_names.assign(info.shdrs.size(), std::string());
    const u16 shstrndx = info.ehdr.e_shstrndx;
    if (shstrndx < info.shdrs.size()) {
        const auto& shstrtab = info.shdrs[shstrndx];
        if (auto blob = ReadFileRange(data, info, shstrtab.sh_offset, shstrtab.sh_size)) {
            for (size_t i = 0; i < info.shdrs.size(); i++) {
                info.section_names[i] = ExtractCString(*blob, info.shdrs[i].sh_name);
            }
        }
    }

    ParseDynamic(data, info);

    return info;
}

ModuleClassification ClassifyModule(const ParsedInfo& info) {
    ModuleClassification result;

    std::string so_name;
    bool has_soname = false;
    if (info.dynamic.readable) {
        for (size_t i = 0; i < info.dynamic.entries.size(); i++) {
            if (static_cast<u64>(info.dynamic.entries[i].d_tag) == 14 /* DT_SONAME */) {
                has_soname = true;
                so_name = info.dynamic.entry_strings[i];
                break;
            }
        }
    }

    const bool has_exports =
        !info.dynamic.export_modules.empty() || !info.dynamic.export_libs.empty();

    bool has_procparam = false;
    for (const auto& p : info.phdrs) {
        if (static_cast<u32>(p.p_type) == 0x61000001 /* PT_SCE_PROCPARAM */) {
            has_procparam = true;
            break;
        }
    }

    if (has_soname || has_exports) {
        result.kind = ModuleKind::SharedModule;
        if (has_soname) {
            result.so_name = so_name;
        } else if (!info.dynamic.export_modules.empty()) {
            result.so_name = info.dynamic.export_modules.front().name;
        } else {
            result.so_name = info.dynamic.export_libs.front().name;
        }
    } else if (has_procparam) {
        result.kind = ModuleKind::Executable;
    } else {
        result.kind = ModuleKind::Unknown;
    }

    return result;
}

std::string ModuleKindName(ModuleKind kind) {
    switch (kind) {
    case ModuleKind::Executable:
        return "Main executable";
    case ModuleKind::SharedModule:
        return "Shared module (PRX/SPRX)";
    case ModuleKind::Unknown:
    default:
        return "Unknown (no shared-module or executable signal found)";
    }
}

std::string PlatformName(const ParsedInfo& info) {
    if (!info.is_valid_elf) {
        return "Unknown";
    }
    return info.ehdr.e_ident[8] == 2 ? "PS5" : "PS4";
}

std::vector<std::string> StrictValidationFailures(const ParsedInfo& info) {
    std::vector<std::string> f;
    if (!info.is_valid_elf) {
        f.push_back("no ELF header found");
        return f;
    }
    const auto& e = info.ehdr;
    if (e.e_ident[4] != 2)
        f.push_back("EI_CLASS is not ELFCLASS64");
    if (e.e_ident[5] != 1)
        f.push_back("EI_DATA is not little-endian");
    if (e.e_ident[6] != 1)
        f.push_back("EI_VERSION is not EV_CURRENT");
    if (e.e_ident[7] != 9)
        f.push_back("EI_OSABI is not FreeBSD (9)");
    if (e.e_ident[8] != 0 && e.e_ident[8] != 2)
        f.push_back("EI_ABIVERSION is not 0 or 2");
    if (static_cast<u16>(e.e_type) != 0xfe10 && static_cast<u16>(e.e_type) != 0xfe18) {
        f.push_back("e_type is not ET_SCE_DYNEXEC (0xfe10) or ET_SCE_DYNAMIC (0xfe18): " +
                    Hex(static_cast<u16>(e.e_type)));
    }
    if (static_cast<u16>(e.e_machine) != 62)
        f.push_back("e_machine is not x86-64");
    if (static_cast<u32>(e.e_version) != 1)
        f.push_back("e_version is not EV_CURRENT");
    if (static_cast<u16>(e.e_phentsize) != sizeof(Elf64Phdr)) {
        f.push_back("e_phentsize is not 56");
    }
    if (static_cast<u16>(e.e_shentsize) > 0 &&
        static_cast<u16>(e.e_shentsize) != sizeof(Elf64Shdr)) {
        f.push_back("e_shentsize is not 0 or 64");
    }
    return f;
}

std::string ToText(const ParsedInfo& info) {
    std::ostringstream out;

    out << "=== SELF wrapper ===\n";
    if (info.is_self) {
        const auto& s = info.self_header;
        out << "Present:         yes\n";
        out << "Declared size:   " << static_cast<u64>(s.file_size) << " bytes\n";
        out << "Segment entries: " << static_cast<u16>(s.segments_num) << "\n";
        out << "\n";
        out << "  [ #] type(raw)           phdr#  has_phdr  offset             "
               "compressed          decompressed         compressed?\n";
        for (size_t i = 0; i < info.self_segments.size(); i++) {
            const auto& seg = info.self_segments[i];
            const u64 type = seg.type;
            const bool has_phdr = (type & 0x800) != 0;
            const u32 phdr_id = static_cast<u32>((type >> 20) & 0xFFF);
            const bool compressed =
                static_cast<u64>(seg.compressed_size) != static_cast<u64>(seg.decompressed_size);
            out << "  [" << i << "] " << Hex(type, 16) << "  ";
            if (has_phdr) {
                out << phdr_id;
            } else {
                out << "-";
            }
            out << "      " << (has_phdr ? "yes" : "no") << "       " << Hex(seg.offset, 16)
                << "   " << static_cast<u64>(seg.compressed_size) << "  "
                << static_cast<u64>(seg.decompressed_size) << "  " << (compressed ? "yes" : "no")
                << "\n";
        }
        out << "\n";
        out << "(Compressed segment payloads are not decoded here - SELF's segment "
               "compression is a proprietary, undocumented Sony format. Segments the "
               "directory marks as stored uncompressed - which in practice usually "
               "includes the dynamic-linking segment - are read directly instead; see "
               "the Dynamic section below.)\n";
    } else {
        out << "Present: no (file is a plain, unwrapped ELF)\n";
    }

    out << "\n=== ELF header ===\n";
    if (!info.is_valid_elf) {
        out << "Not found / not recognized as ELF at offset " << Hex(info.ehdr_file_offset)
            << ".\n";

        if (info.is_self) {
            out << "\nThe SELF wrapper above WAS recognized, but the bytes at that "
                   "offset aren't a valid ELF header - could be a different container "
                   "format nested inside, a build this hasn't been tested against, or "
                   "a corrupted file. Bytes at offset "
                << Hex(info.ehdr_file_offset) << ":\n\n";
            out << HexDump(info.raw_at_ehdr_offset, info.ehdr_file_offset);
        } else {
            out << "\nNeither the SELF wrapper magic nor the ELF magic (\\x7fELF) was "
                   "found anywhere this looked. This may not be a PS4/PS5 executable at "
                   "all - first bytes of the file (size: "
                << info.file_size << " bytes):\n\n";
            out << HexDump(info.raw_header);
        }

        return out.str();
    }

    const auto& e = info.ehdr;
    out << "Found at file offset: " << Hex(info.ehdr_file_offset) << "\n";
    out << "Class:        " << ElfClassName(e.e_ident[4]) << "\n";
    out << "Data:         " << ElfDataName(e.e_ident[5]) << "\n";
    out << "OS/ABI:       " << ElfOsAbiName(e.e_ident[7]) << "\n";
    out << "ABI version:  " << static_cast<int>(e.e_ident[8]) << "\n";
    out << "Type:         " << ElfTypeName(e.e_type) << "\n";
    const auto module_class = ClassifyModule(info);
    out << "Module kind:  " << ModuleKindName(module_class.kind);
    if (module_class.kind == ModuleKind::SharedModule && !module_class.so_name.empty()) {
        out << " - " << module_class.so_name;
    }
    out << "\n";
    out << "Machine:      " << ElfMachineName(e.e_machine) << "\n";
    out << "Platform:     " << PlatformName(info) << "\n";
    {
        const auto failures = StrictValidationFailures(info);
        out << "PS4/PS5 loader validation: " << (failures.empty() ? "passes" : "rejected") << "\n";
        for (const auto& line : failures) {
            out << "  - " << line << "\n";
        }
    }
    out << "Entry point:  " << Hex(e.e_entry) << "\n";
    out << "Flags:        " << Hex(e.e_flags) << "\n";
    out << "Program headers: " << static_cast<u16>(e.e_phnum) << " entries, "
        << static_cast<u16>(e.e_phentsize) << " bytes each, at file offset "
        << Hex(info.ehdr_file_offset + static_cast<u64>(e.e_phoff)) << "\n";
    out << "Section headers: " << static_cast<u16>(e.e_shnum) << " entries"
        << (e.e_shnum > 0 && info.shdrs.empty()
                ? " (present but not read - e_shentsize didn't match, or table truncated)"
                : "")
        << "\n";

    out << "\n=== Program headers ===\n";
    if (info.phdrs.empty()) {
        out << "(none read - either e_phnum is 0, e_phentsize didn't match the "
               "expected 56 bytes, or the table is truncated)\n";
    } else {
        out << "  [ #] type                            flags  offset             "
               "vaddr              filesz             memsz              align\n";
        for (size_t i = 0; i < info.phdrs.size(); i++) {
            const auto& p = info.phdrs[i];
            out << "  [" << i << "] ";
            out.width(32);
            out.setf(std::ios::left);
            out << PhdrTypeName(p.p_type);
            out.unsetf(std::ios::left);
            out << "  " << PhdrFlagsName(p.p_flags) << "  " << Hex(p.p_offset, 16) << "   "
                << Hex(p.p_vaddr, 16) << "   " << Hex(p.p_filesz, 16) << "   " << Hex(p.p_memsz, 16)
                << "   " << Hex(p.p_align) << "\n";
        }
    }

    out << "\n=== Section headers ===\n";
    if (info.shdrs.empty()) {
        out << "(none - see note above)\n";
    } else {
        out << "  [ #] name                             type              flags    "
               "addr               offset             size               link  info\n";
        for (size_t i = 0; i < info.shdrs.size(); i++) {
            const auto& sh = info.shdrs[i];
            const std::string name =
                info.section_names[i].empty() ? "<unnamed>" : info.section_names[i];
            out << "  [" << i << "] ";
            out.width(32);
            out.setf(std::ios::left);
            out << name;
            out.unsetf(std::ios::left);
            out << " ";
            out.width(16);
            out.setf(std::ios::left);
            out << ShdrTypeName(sh.sh_type);
            out.unsetf(std::ios::left);
            out << "  " << Hex(sh.sh_flags, 8) << " " << Hex(sh.sh_addr, 16) << "   "
                << Hex(sh.sh_offset, 16) << "   " << Hex(sh.sh_size, 16) << "   "
                << static_cast<u32>(sh.sh_link) << "     " << static_cast<u32>(sh.sh_info) << "\n";
        }
    }

    out << "\n=== Dynamic section ===\n";
    if (!info.dynamic.present) {
        out << "(no PT_DYNAMIC program header - this binary has no dynamic linking "
               "info, or isn't dynamically linked)\n";
    } else if (!info.dynamic.readable) {
        out << "Present, but not readable: " << info.dynamic.unavailable_reason << "\n";
    } else {
        out << "  [ #] tag                     value/vaddr         string\n";
        for (size_t i = 0; i < info.dynamic.entries.size(); i++) {
            const auto& d = info.dynamic.entries[i];
            out << "  [" << i << "] ";
            out.width(24);
            out.setf(std::ios::left);
            out << DynTagName(d.d_tag);
            out.unsetf(std::ios::left);
            out << " " << Hex(d.d_val, 16);
            if (!info.dynamic.entry_strings[i].empty()) {
                out << "  " << info.dynamic.entry_strings[i];
            }
            out << "\n";
        }
    }

    if (info.dynamic.readable && !info.dynamic.summary.empty()) {
        out << "\n=== Dynamic linking summary ===\n";
        for (const auto& row : info.dynamic.summary) {
            out << "  " << row.first << ": " << row.second << "\n";
        }
    }
    if (info.dynamic.readable && !info.dynamic.strings_unavailable_reason.empty()) {
        out << "\nNote: names/libraries couldn't be resolved - "
            << info.dynamic.strings_unavailable_reason << ".\n";
    }

    auto print_modules = [&out](const char* title, const std::vector<ModuleIdInfo>& list) {
        out << "\n=== " << title << " (" << list.size() << ") ===\n";
        for (size_t i = 0; i < list.size(); i++) {
            const auto& m = list[i];
            out << "  [" << i << "] " << m.name << " (id=" << m.id << ", v" << m.version_major
                << "." << m.version_minor << ")\n";
        }
    };
    auto print_libs = [&out](const char* title, const std::vector<LibraryIdInfo>& list) {
        out << "\n=== " << title << " (" << list.size() << ") ===\n";
        for (size_t i = 0; i < list.size(); i++) {
            const auto& l = list[i];
            out << "  [" << i << "] " << l.name << " (id=" << l.id << ", v" << l.version << ")\n";
        }
    };

    if (!info.dynamic.import_modules.empty()) {
        print_modules("Imported Modules", info.dynamic.import_modules);
    }
    if (!info.dynamic.export_modules.empty()) {
        print_modules("Exported Modules", info.dynamic.export_modules);
    }
    if (!info.dynamic.import_libs.empty()) {
        print_libs("Imported Libraries", info.dynamic.import_libs);
    }
    if (!info.dynamic.export_libs.empty()) {
        print_libs("Exported Libraries", info.dynamic.export_libs);
    }

    return out.str();
}

} // namespace Loader::ElfInfo
