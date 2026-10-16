// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "playgo.h"

#include <cstring>

namespace {

constexpr size_t kHeaderSize = 0x100;
constexpr size_t kTableOffset = 0xC0; // eight {u32 offset, u32 size} section records
constexpr size_t kChunkAttrSize = 0x20;
constexpr size_t kRangeSize = 0x10;
constexpr size_t kScenarioAttrSize = 0x20;
constexpr size_t kMaxRecords = 0x10000;

enum Section {
    ChunkAttrs = 0,
    ChunkMchunks = 1,
    ChunkLabels = 2,
    MchunkAttrs = 3,
    ScenarioAttrs = 4,
    ScenarioChunks = 5,
    ScenarioLabels = 6,
};

u16 Le16(const u8* p) {
    return static_cast<u16>(p[0] | (p[1] << 8));
}
u32 Le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}
u64 Le64(const u8* p) {
    return static_cast<u64>(Le32(p)) | (static_cast<u64>(Le32(p + 4)) << 32);
}

struct Blob {
    const u8* data = nullptr;
    size_t size = 0;
};

std::string LabelAt(const Blob& labels, u32 offset) {
    std::string s;
    for (size_t i = offset; i < labels.size && labels.data[i] != 0; i++) {
        s.push_back(static_cast<char>(labels.data[i]));
    }
    return s;
}

} // namespace

bool PlayGoInfo::Parse(std::span<const u8> data) {
    *this = {};
    if (data.size() < kHeaderSize) {
        error = "The file is too small.";
        return false;
    }
    if (std::memcmp(data.data(), "plgx", 4) != 0 && std::memcmp(data.data(), "plgo", 4) != 0) {
        error = "Not a PlayGo chunk file.";
        return false;
    }
    magic.assign(reinterpret_cast<const char*>(data.data()), 4);
    version = Le16(&data[0x04]);
    image_count = Le16(&data[0x08]);
    default_scenario = Le16(&data[0x14]);
    for (size_t i = 0x40; i < 0x40 + 0x30 && data[i] != 0; i++) {
        content_id.push_back(static_cast<char>(data[i]));
    }

    Blob sections[8];
    for (int s = 0; s < 8; s++) {
        const u32 off = Le32(&data[kTableOffset + s * 8]);
        const u32 size = Le32(&data[kTableOffset + s * 8 + 4]);
        if (size == 0) {
            continue;
        }
        if (off > data.size() || size > data.size() - off) {
            error = "A section lies outside the file.";
            return false;
        }
        sections[s] = {data.data() + off, size};
    }

    const Blob& chunk_attrs = sections[ChunkAttrs];
    for (size_t i = 0; i + kChunkAttrSize <= chunk_attrs.size && i / kChunkAttrSize < kMaxRecords;
         i += kChunkAttrSize) {
        const u8* c = chunk_attrs.data + i;
        PlayGoChunk chunk;
        chunk.flag = c[0];
        chunk.disc_layer = c[1];
        chunk.required_locus = c[2];
        chunk.language_mask = Le64(c + 0x10);
        chunk.label = LabelAt(sections[ChunkLabels], Le32(c + 0x1C));
        chunks.push_back(std::move(chunk));
    }

    const Blob& mchunks = sections[MchunkAttrs];
    for (size_t i = 0; i + kRangeSize <= mchunks.size && i / kRangeSize < kMaxRecords;
         i += kRangeSize) {
        ranges.push_back({Le64(mchunks.data + i), Le64(mchunks.data + i + 8)});
    }

    const Blob& scenario_attrs = sections[ScenarioAttrs];
    const Blob& scenario_chunks = sections[ScenarioChunks];
    for (size_t i = 0;
         i + kScenarioAttrSize <= scenario_attrs.size && i / kScenarioAttrSize < kMaxRecords;
         i += kScenarioAttrSize) {
        const u8* s = scenario_attrs.data + i;
        PlayGoScenario scenario;
        scenario.type = s[0];
        scenario.initial_chunk_count = Le16(s + 0x14);
        const u16 count = Le16(s + 0x16);
        const u32 list = Le32(s + 0x18);
        for (u32 k = 0; k < count; k++) {
            const size_t at = size_t{list} + size_t{k} * 2;
            if (at + 2 > scenario_chunks.size) {
                break;
            }
            scenario.chunks.push_back(Le16(scenario_chunks.data + at));
        }
        scenario.label = LabelAt(sections[ScenarioLabels], Le32(s + 0x1C));
        scenarios.push_back(std::move(scenario));
    }
    if (chunks.empty() && scenarios.empty()) {
        error = "The file lists no chunks or scenarios.";
        return false;
    }
    return true;
}
