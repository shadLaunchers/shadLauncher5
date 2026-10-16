// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dds.h"

#include <algorithm>
#include <cstring>

#define BCDEC_IMPLEMENTATION
#define BCDEC_STATIC
#include "common/bcdec.h"

namespace Dds {

namespace {

constexpr u32 kMagic = 0x20534444; // "DDS "
constexpr size_t kHeaderSize = 4 + 124;
constexpr size_t kDx10Size = 20;
constexpr u32 kPfFourCc = 0x4;
constexpr u32 kPfRgb = 0x40;
constexpr u32 kMaxDimension = 16384;

u32 Le32(const u8* p) {
    return static_cast<u32>(p[0]) | (static_cast<u32>(p[1]) << 8) | (static_cast<u32>(p[2]) << 16) |
           (static_cast<u32>(p[3]) << 24);
}

constexpr u32 FourCc(char a, char b, char c, char d) {
    return static_cast<u32>(static_cast<u8>(a)) | (static_cast<u32>(static_cast<u8>(b)) << 8) |
           (static_cast<u32>(static_cast<u8>(c)) << 16) |
           (static_cast<u32>(static_cast<u8>(d)) << 24);
}

enum class Format { Unknown, BC1, BC2, BC3, BC4, BC5, BC7, RGBA8, BGRA8, BGRX8 };

const char* FormatName(Format f) {
    switch (f) {
    case Format::BC1:
        return "BC1";
    case Format::BC2:
        return "BC2";
    case Format::BC3:
        return "BC3";
    case Format::BC4:
        return "BC4";
    case Format::BC5:
        return "BC5";
    case Format::BC7:
        return "BC7";
    case Format::RGBA8:
        return "R8G8B8A8";
    case Format::BGRA8:
        return "B8G8R8A8";
    case Format::BGRX8:
        return "B8G8R8X8";
    default:
        return "unknown";
    }
}

Format FromDxgi(u32 dxgi) {
    switch (dxgi) {
    case 28: // R8G8B8A8_UNORM
    case 29: // _SRGB
        return Format::RGBA8;
    case 71: // BC1_UNORM
    case 72:
        return Format::BC1;
    case 74:
    case 75:
        return Format::BC2;
    case 77:
    case 78:
        return Format::BC3;
    case 80:
        return Format::BC4;
    case 83:
        return Format::BC5;
    case 87: // B8G8R8A8_UNORM
    case 91:
        return Format::BGRA8;
    case 88: // B8G8R8X8_UNORM
    case 93:
        return Format::BGRX8;
    case 98: // BC7_UNORM
    case 99:
        return Format::BC7;
    default:
        return Format::Unknown;
    }
}

size_t BlockBytes(Format f) {
    switch (f) {
    case Format::BC1:
    case Format::BC4:
        return 8;
    case Format::BC2:
    case Format::BC3:
    case Format::BC5:
    case Format::BC7:
        return 16;
    default:
        return 0;
    }
}

} // namespace

bool IsDds(std::span<const u8> data) {
    return data.size() >= kHeaderSize && Le32(data.data()) == kMagic;
}

std::optional<Image> Decode(std::span<const u8> data, std::string* problem) {
    auto fail = [&](const char* why) -> std::optional<Image> {
        if (problem != nullptr) {
            *problem = why;
        }
        return std::nullopt;
    };
    if (!IsDds(data)) {
        return fail("Not a DDS file.");
    }
    const u8* h = data.data() + 4;
    const u32 height = Le32(h + 8);
    const u32 width = Le32(h + 12);
    const u32 pf_flags = Le32(h + 76);
    const u32 four_cc = Le32(h + 80);
    const u32 bit_count = Le32(h + 84);
    const u32 r_mask = Le32(h + 88);
    const u32 b_mask = Le32(h + 96);
    if (width == 0 || height == 0 || width > kMaxDimension || height > kMaxDimension) {
        return fail("The texture has invalid dimensions.");
    }

    Format format = Format::Unknown;
    size_t offset = kHeaderSize;
    if ((pf_flags & kPfFourCc) != 0) {
        switch (four_cc) {
        case FourCc('D', 'X', '1', '0'):
            if (data.size() < kHeaderSize + kDx10Size) {
                return fail("The DX10 header is truncated.");
            }
            format = FromDxgi(Le32(data.data() + kHeaderSize));
            offset += kDx10Size;
            break;
        case FourCc('D', 'X', 'T', '1'):
            format = Format::BC1;
            break;
        case FourCc('D', 'X', 'T', '2'):
        case FourCc('D', 'X', 'T', '3'):
            format = Format::BC2;
            break;
        case FourCc('D', 'X', 'T', '4'):
        case FourCc('D', 'X', 'T', '5'):
            format = Format::BC3;
            break;
        case FourCc('A', 'T', 'I', '1'):
        case FourCc('B', 'C', '4', 'U'):
            format = Format::BC4;
            break;
        case FourCc('A', 'T', 'I', '2'):
        case FourCc('B', 'C', '5', 'U'):
            format = Format::BC5;
            break;
        default:
            break;
        }
    } else if ((pf_flags & kPfRgb) != 0 && bit_count == 32) {
        format = r_mask == 0x000000FF && b_mask == 0x00FF0000 ? Format::RGBA8 : Format::BGRA8;
    }
    if (format == Format::Unknown) {
        return fail("The texture format is not supported.");
    }

    Image img;
    img.width = width;
    img.height = height;
    img.format = FormatName(format);
    img.rgba.assign(size_t{width} * height * 4, 0);
    const u8* src = data.data() + offset;
    const size_t avail = data.size() - offset;

    if (const size_t bb = BlockBytes(format); bb != 0) {
        const u32 bw = (width + 3) / 4;
        const u32 bh = (height + 3) / 4;
        if (avail < size_t{bw} * bh * bb) {
            return fail("The texture data is truncated.");
        }
        u8 block[4 * 4 * 4];
        u8 small[4 * 4 * 2];
        for (u32 by = 0; by < bh; by++) {
            for (u32 bx = 0; bx < bw; bx++) {
                const u8* in = src + (size_t{by} * bw + bx) * bb;
                switch (format) {
                case Format::BC1:
                    bcdec_bc1(in, block, 16);
                    break;
                case Format::BC2:
                    bcdec_bc2(in, block, 16);
                    break;
                case Format::BC3:
                    bcdec_bc3(in, block, 16);
                    break;
                case Format::BC7:
                    bcdec_bc7(in, block, 16);
                    break;
                case Format::BC4:
                    bcdec_bc4(in, small, 4);
                    for (int i = 0; i < 16; i++) {
                        block[i * 4 + 0] = block[i * 4 + 1] = block[i * 4 + 2] = small[i];
                        block[i * 4 + 3] = 255;
                    }
                    break;
                case Format::BC5:
                    bcdec_bc5(in, small, 8);
                    for (int i = 0; i < 16; i++) {
                        block[i * 4 + 0] = small[i * 2 + 0];
                        block[i * 4 + 1] = small[i * 2 + 1];
                        block[i * 4 + 2] = 0;
                        block[i * 4 + 3] = 255;
                    }
                    break;
                default:
                    break;
                }
                // Copy the 4x4 block, clipping at the right/bottom edges.
                for (u32 y = 0; y < 4 && by * 4 + y < height; y++) {
                    const u32 cols = std::min<u32>(4, width - bx * 4);
                    std::memcpy(&img.rgba[(size_t{by * 4 + y} * width + bx * 4) * 4],
                                &block[y * 16], size_t{cols} * 4);
                }
            }
        }
        return img;
    }

    const size_t pixels = size_t{width} * height;
    if (avail < pixels * 4) {
        return fail("The texture data is truncated.");
    }
    for (size_t i = 0; i < pixels; i++) {
        const u8* p = src + i * 4;
        u8* o = &img.rgba[i * 4];
        if (format == Format::RGBA8) {
            std::memcpy(o, p, 4);
        } else {
            o[0] = p[2];
            o[1] = p[1];
            o[2] = p[0];
            o[3] = format == Format::BGRX8 ? 255 : p[3];
        }
    }
    return img;
}

} // namespace Dds
