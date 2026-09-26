// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include "common/endian.h"
#include "common/types.h"

namespace Loader::ElfInfo {

#pragma pack(push, 1)
struct SelfHeader {
    u8 ident[12]; // SELF magic + version/mode bytes
    u16_le size1;
    u16_le size2;
    u64_le file_size;
    u16_le segments_num;
    u16_le unknown;
    u32_le pad;
};
static_assert(sizeof(SelfHeader) == 32);

struct SelfSegmentEntry {
    u64_le type;
    u64_le offset;
    u64_le compressed_size;
    u64_le decompressed_size;
};
static_assert(sizeof(SelfSegmentEntry) == 32);

struct Elf64Ehdr {
    u8 e_ident[16];
    u16_le e_type;
    u16_le e_machine;
    u32_le e_version;
    u64_le e_entry;
    u64_le e_phoff;
    u64_le e_shoff;
    u32_le e_flags;
    u16_le e_ehsize;
    u16_le e_phentsize;
    u16_le e_phnum;
    u16_le e_shentsize;
    u16_le e_shnum;
    u16_le e_shstrndx;
};
static_assert(sizeof(Elf64Ehdr) == 64);

struct Elf64Phdr {
    u32_le p_type;
    u32_le p_flags;
    u64_le p_offset;
    u64_le p_vaddr;
    u64_le p_paddr;
    u64_le p_filesz;
    u64_le p_memsz;
    u64_le p_align;
};
static_assert(sizeof(Elf64Phdr) == 56);

struct Elf64Shdr {
    u32_le sh_name; // byte offset into the section-header string table
    u32_le sh_type;
    u64_le sh_flags;
    u64_le sh_addr;
    u64_le sh_offset;
    u64_le sh_size;
    u32_le sh_link;
    u32_le sh_info;
    u64_le sh_addralign;
    u64_le sh_entsize;
};
static_assert(sizeof(Elf64Shdr) == 64);

struct Elf64Dyn {
    u64_le d_tag;
    u64_le d_val;
};
static_assert(sizeof(Elf64Dyn) == 16);
#pragma pack(pop)

struct DynamicInfo {
    bool present = false;           // a PT_DYNAMIC program header exists
    bool readable = false;          // and its entries could actually be read
    std::string unavailable_reason; // set when present && !readable

    std::vector<Elf64Dyn> entries;
    std::vector<std::string> entry_strings;
};

struct ParsedInfo {
    bool is_self = false;
    SelfHeader self_header{};
    std::vector<SelfSegmentEntry> self_segments;

    bool is_valid_elf = false; // false => couldn't even read a plausible Ehdr
    u64 ehdr_file_offset = 0;  // physical file offset the Ehdr was read from
    Elf64Ehdr ehdr{};
    std::vector<Elf64Phdr> phdrs;

    std::vector<Elf64Shdr> shdrs;
    std::vector<std::string> section_names;

    DynamicInfo dynamic;
};

enum class ModuleKind {
    Unknown,      // neither signal found; can't tell
    Executable,   // PT_SCE_PROCPARAM present, no DT_SONAME
    SharedModule, // DT_SONAME present (PRX/SPRX)
};

struct ModuleClassification {
    ModuleKind kind = ModuleKind::Unknown;
    std::string so_name; // set when kind == SharedModule and DT_SONAME resolved
};

ModuleClassification ClassifyModule(const ParsedInfo& info);
std::string ModuleKindName(ModuleKind kind);
std::optional<ParsedInfo> Parse(const std::vector<u8>& data);
std::string ToText(const ParsedInfo& info);
std::string Hex(u64 value, int width = 0);
std::string ElfClassName(u8 v);
std::string ElfDataName(u8 v);
std::string ElfOsAbiName(u8 v);
std::string ElfTypeName(u16 v);
std::string ElfMachineName(u16 v);
std::string PhdrTypeName(u32 v);
std::string PhdrFlagsName(u32 v);
std::string ShdrTypeName(u32 v);
std::string DynTagName(u64 v);

} // namespace Loader::ElfInfo
