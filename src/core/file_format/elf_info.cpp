// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "elf_info.h"

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

    return info;
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
        out << "(Segment payload bytes are not decoded here - SELF's segment "
               "compression is a proprietary, undocumented Sony format. This "
               "shows only what the segment directory itself states.)\n";
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
    out << "Machine:      " << ElfMachineName(e.e_machine) << "\n";
    out << "Entry point:  " << Hex(e.e_entry) << "\n";
    out << "Flags:        " << Hex(e.e_flags) << "\n";
    out << "Program headers: " << static_cast<u16>(e.e_phnum) << " entries, "
        << static_cast<u16>(e.e_phentsize) << " bytes each, at file offset "
        << Hex(info.ehdr_file_offset + static_cast<u64>(e.e_phoff)) << "\n";
    out << "Section headers: " << static_cast<u16>(e.e_shnum) << " entries"
        << (info.is_self ? " (not read for SELF files - see note above)" : "") << "\n";

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

    return out.str();
}

} // namespace Loader::ElfInfo
