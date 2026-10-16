// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <span>

#include "common/types.h"

// Small, dependency-free crypto primitives for reading PS5 packages:
// SHA-256, SHA3-256, HMAC-SHA256 and AES-128 (with XTS decryption).
// Portable C++, no CPU-specific code.
namespace Common::Crypto {

using Digest256 = std::array<u8, 32>;

[[nodiscard]] Digest256 Sha256(std::span<const u8> data);
[[nodiscard]] Digest256 Sha3_256(std::span<const u8> data);
[[nodiscard]] Digest256 HmacSha256(std::span<const u8> key, std::span<const u8> data);

class Aes128 {
public:
    explicit Aes128(std::span<const u8, 16> key);

    void EncryptBlock(const u8 in[16], u8 out[16]) const;
    void DecryptBlock(const u8 in[16], u8 out[16]) const;

private:
    std::array<u32, 44> enc_{};
    std::array<u32, 44> dec_{};
};

// AES-128-XTS decryption of one data unit, in place. `data.size()` must be a
// multiple of 16; the whole buffer is one unit whose tweak is the
// little-endian `sector` number.
void XtsDecrypt(const Aes128& data_key, const Aes128& tweak_key, u64 sector, std::span<u8> data);

// AES-128-CBC decryption, in place, without padding. `data.size()` must be a
// multiple of 16.
void CbcDecrypt(const Aes128& key, std::span<const u8, 16> iv, std::span<u8> data);

} // namespace Common::Crypto
