// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "elf_info.h"

#include <cstdint>
#include <cstring>
#include <sstream>

namespace Loader::ElfInfo {

namespace {

// SCE's SELF/SPRX magic, shared across PS3/PS4/PS5 signed binaries.
constexpr u8 kSelfMagic[4] = {0x4F, 0x15, 0x3D, 0x1D};
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
    default:
        return "unknown (" + Hex(v) + ")";
    }
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

    std::optional<u64> strtab_vaddr;
    std::optional<u64> strtab_size;
    for (const auto& e : info.dynamic.entries) {
        if (static_cast<u64>(e.d_tag) == 5 /* DT_STRTAB */) {
            strtab_vaddr = e.d_val;
        } else if (static_cast<u64>(e.d_tag) == 10 /* DT_STRSZ */) {
            strtab_size = e.d_val;
        }
    }

    info.dynamic.entry_strings.assign(info.dynamic.entries.size(), std::string());

    if (strtab_vaddr && strtab_size && *strtab_size > 0) {
        if (auto strtab_offset = VaddrToFileOffset(info, *strtab_vaddr)) {
            if (auto strtab_bytes = ReadFileRange(data, info, *strtab_offset, *strtab_size)) {
                for (size_t i = 0; i < info.dynamic.entries.size(); i++) {
                    const u64 tag = info.dynamic.entries[i].d_tag;
                    if (tag == 1 /* DT_NEEDED */ || tag == 14 /* DT_SONAME */ ||
                        tag == 15 /* DT_RPATH */ || tag == 29 /* DT_RUNPATH */) {
                        info.dynamic.entry_strings[i] =
                            ExtractCString(*strtab_bytes, info.dynamic.entries[i].d_val);
                    }
                }
            }
        }
    }
}

} // namespace

std::optional<ParsedInfo> Parse(const std::vector<u8>& data) {
    if (data.size() < 16) {
        return std::nullopt;
    }

    ParsedInfo info;

    if (HasMagic(data, 0, kSelfMagic, sizeof(kSelfMagic))) {
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

    bool has_procparam = false;
    for (const auto& p : info.phdrs) {
        if (static_cast<u32>(p.p_type) == 0x61000001 /* PT_SCE_PROCPARAM */) {
            has_procparam = true;
            break;
        }
    }

    if (has_soname) {
        result.kind = ModuleKind::SharedModule;
        result.so_name = so_name;
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
        return "Unknown (neither DT_SONAME nor PT_SCE_PROCPARAM found)";
    }
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

    return out.str();
}

} // namespace Loader::ElfInfo
