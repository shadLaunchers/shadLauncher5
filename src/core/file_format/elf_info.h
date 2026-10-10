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
    u8 ident[12];
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

    [[nodiscard]] bool is_data() const {
        return (static_cast<u64>(type) & 0x800u) != 0;
    }
    [[nodiscard]] bool is_encrypted() const {
        return (static_cast<u64>(type) & 0x2u) != 0;
    }
    [[nodiscard]] bool is_compressed_flag() const {
        return (static_cast<u64>(type) & 0x8u) != 0;
    }
    [[nodiscard]] u64 phdr_index() const {
        return (static_cast<u64>(type) >> 20u) & 0xFFFu;
    }
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
    u32_le sh_name;
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

struct Elf64Sym {
    u32_le st_name;
    u8 st_info;
    u8 st_other;
    u16_le st_shndx;
    u64_le st_value;
    u64_le st_size;
};
static_assert(sizeof(Elf64Sym) == 24);

struct Elf64Rela {
    u64_le r_offset;
    u64_le r_info;
    u64_le r_addend;
};
static_assert(sizeof(Elf64Rela) == 24);
#pragma pack(pop)

struct ModuleIdInfo {
    std::string id;
    std::string name;
    int version_major = 0;
    int version_minor = 0;
};

struct LibraryIdInfo {
    std::string id;
    std::string name;
    int version = 0;
};

struct SymbolInfo {
    std::string raw_name;
    std::string nid;
    std::string resolved_name;
    std::string library_id;
    std::string module_id;
    std::string library;
    int library_version = 0;
    std::string module;
    int module_version_major = 0;
    int module_version_minor = 0;
    u8 bind = 0; // STB_*
    u8 type = 0; // STT_*
    u64 value = 0;
    u64 size = 0;
    bool nid_form = false;
    bool is_export = false;
};

struct RelocationInfo {
    u64 offset = 0;
    u32 type = 0;
    u32 symbol_index = 0;
    s64 addend = 0;
    bool is_plt = false;
    bool has_symbol = false;
    std::string symbol_nid;
    std::string symbol_library;
    std::string symbol_module;
};

struct DynamicInfo {
    bool present = false;
    bool readable = false;
    std::string unavailable_reason;

    std::vector<Elf64Dyn> entries;
    std::vector<std::string> entry_strings;

    std::vector<ModuleIdInfo> import_modules;
    std::vector<ModuleIdInfo> export_modules;
    std::vector<LibraryIdInfo> import_libs;
    std::vector<LibraryIdInfo> export_libs;

    std::vector<std::pair<std::string, std::string>> summary;
    std::string strings_unavailable_reason;
    std::vector<SymbolInfo> symbols;
    std::string symbols_unavailable_reason;

    std::vector<RelocationInfo> relocations;
    std::string relocations_unavailable_reason;
};

struct LibVersionEntry {
    std::string name;
    u32 version_raw = 0;
    std::vector<u8> raw;

    [[nodiscard]] std::string GuessedVersionString() const;
};

std::vector<LibVersionEntry> ParseLibVersion(const std::vector<u8>& segment);

struct ParsedInfo {
    bool is_self = false;
    SelfHeader self_header{};
    std::vector<SelfSegmentEntry> self_segments;

    bool is_valid_elf = false;
    u64 ehdr_file_offset = 0;
    Elf64Ehdr ehdr{};
    std::vector<Elf64Phdr> phdrs;

    std::vector<Elf64Shdr> shdrs;
    std::vector<std::string> section_names;
    DynamicInfo dynamic;

    bool lib_versions_present = false;
    std::vector<LibVersionEntry> lib_versions;
    std::string lib_versions_unavailable_reason;

    u64 file_size = 0;
    std::vector<u8> raw_header;
    std::vector<u8> raw_at_ehdr_offset;
};

enum class ModuleKind {
    Unknown,
    Executable,
    SharedModule,
};

struct ModuleClassification {
    ModuleKind kind = ModuleKind::Unknown;
    std::string so_name;
};

ModuleClassification ClassifyModule(const ParsedInfo& info);
std::string ModuleKindName(ModuleKind kind);
std::string PlatformName(const ParsedInfo& info);
std::vector<std::string> StrictValidationFailures(const ParsedInfo& info);
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
std::string SymbolBindName(u8 v);
std::string SymbolTypeName(u8 v);
std::string RelocTypeName(u32 v);
struct TlsSummary {
    bool present = false;
    u64 image_vaddr = 0;
    u64 image_size = 0;
    u64 init_size = 0;
    u64 tcb_offset = 0;
    u64 align = 0;
};
TlsSummary GetTlsSummary(const ParsedInfo& info);
std::string HexDump(const std::vector<u8>& bytes, u64 base_offset = 0);

struct ExtractResult {
    std::vector<u8> elf;
    bool was_self = false;
    size_t encrypted_segments = 0;
    size_t compressed_segments = 0;
    size_t copied_load_segments = 0;
    std::string error;
};
ExtractResult ExtractElf(const std::vector<u8>& data, const ParsedInfo& info);

} // namespace Loader::ElfInfo
