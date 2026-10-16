// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "crypto.h"

#include <cstring>

namespace Common::Crypto {

namespace {

constexpr u32 Rotr32(u32 v, int n) {
    return (v >> n) | (v << (32 - n));
}
constexpr u64 Rotl64(u64 v, int n) {
    return n == 0 ? v : (v << n) | (v >> (64 - n));
}

// ---------------------------------------------------------------- SHA-256

constexpr u32 kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

class Sha256Ctx {
public:
    void Update(std::span<const u8> data) {
        for (const u8 b : data) {
            buf_[len_++ % 64] = b;
            if (len_ % 64 == 0) {
                Compress(buf_);
            }
        }
    }

    Digest256 Final() {
        const u64 bits = len_ * 8;
        const u8 pad = 0x80;
        Update({&pad, 1});
        const u8 zero = 0;
        while (len_ % 64 != 56) {
            Update({&zero, 1});
        }
        u8 len_be[8];
        for (int i = 0; i < 8; i++) {
            len_be[i] = static_cast<u8>(bits >> (56 - 8 * i));
        }
        Update(len_be);
        Digest256 out{};
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 4; j++) {
                out[i * 4 + j] = static_cast<u8>(h_[i] >> (24 - 8 * j));
            }
        }
        return out;
    }

private:
    void Compress(const u8* block) {
        u32 w[64];
        for (int i = 0; i < 16; i++) {
            w[i] = (u32{block[i * 4]} << 24) | (u32{block[i * 4 + 1]} << 16) |
                   (u32{block[i * 4 + 2]} << 8) | u32{block[i * 4 + 3]};
        }
        for (int i = 16; i < 64; i++) {
            const u32 s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const u32 s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        u32 a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; i++) {
            const u32 t1 = h + (Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25)) +
                           ((e & f) ^ (~e & g)) + kSha256K[i] + w[i];
            const u32 t2 =
                (Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            h = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    u32 h_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    u8 buf_[64]{};
    u64 len_ = 0;
};

// ---------------------------------------------------------------- SHA3-256

struct KeccakTables {
    u64 rc[24];
    int rot[25]; // indexed x + 5y
    int pi[25];  // destination lane of the pi step
    int prev[5], next[5], next2[5];
    KeccakTables() {
        for (int x = 0; x < 5; x++) {
            prev[x] = (x + 4) % 5;
            next[x] = (x + 1) % 5;
            next2[x] = (x + 2) % 5;
            for (int y = 0; y < 5; y++) {
                pi[x + 5 * y] = y + 5 * ((2 * x + 3 * y) % 5);
            }
        }
        // Round constants from the degree-8 LFSR in FIPS 202.
        u8 lfsr = 1;
        for (int round = 0; round < 24; round++) {
            u64 c = 0;
            for (int j = 0; j < 7; j++) {
                if (lfsr & 1) {
                    c |= u64{1} << ((1 << j) - 1);
                }
                lfsr = static_cast<u8>((lfsr & 0x80) ? (lfsr << 1) ^ 0x71 : lfsr << 1);
            }
            rc[round] = c;
        }
        // Rotation offsets.
        rot[0] = 0;
        int x = 1, y = 0;
        for (int t = 0; t < 24; t++) {
            rot[x + 5 * y] = ((t + 1) * (t + 2) / 2) % 64;
            const int nx = y;
            const int ny = (2 * x + 3 * y) % 5;
            x = nx;
            y = ny;
        }
    }
};

const KeccakTables& Keccak() {
    static const KeccakTables tables;
    return tables;
}

void KeccakF(u64 a[25]) {
    const auto& t = Keccak();
    for (int round = 0; round < 24; round++) {
        u64 c[5], b[25];
        for (int x = 0; x < 5; x++) {
            c[x] = a[x] ^ a[x + 5] ^ a[x + 10] ^ a[x + 15] ^ a[x + 20];
        }
        for (int x = 0; x < 5; x++) {
            const u64 d = c[t.prev[x]] ^ Rotl64(c[t.next[x]], 1);
            for (int y = 0; y < 25; y += 5) {
                a[x + y] ^= d;
            }
        }
        for (int i = 0; i < 25; i++) {
            b[t.pi[i]] = Rotl64(a[i], t.rot[i]);
        }
        for (int y = 0; y < 25; y += 5) {
            for (int x = 0; x < 5; x++) {
                a[x + y] = b[x + y] ^ (~b[t.next[x] + y] & b[t.next2[x] + y]);
            }
        }
        a[0] ^= t.rc[round];
    }
}

// ---------------------------------------------------------------- AES-128

constexpr u8 XTime(u8 v) {
    return static_cast<u8>((v << 1) ^ ((v & 0x80) ? 0x1B : 0));
}
constexpr u8 GfMul(u8 a, u8 b) {
    u8 r = 0;
    while (b) {
        if (b & 1) {
            r ^= a;
        }
        a = XTime(a);
        b >>= 1;
    }
    return r;
}

struct AesTables {
    u8 sbox[256];
    u8 inv_sbox[256];
    u32 te[4][256];
    u32 td[4][256];
    AesTables() {
        // S-box: multiplicative inverse in GF(2^8) followed by the affine map.
        for (int i = 0; i < 256; i++) {
            u8 inv = 0;
            if (i != 0) {
                for (int j = 1; j < 256; j++) {
                    if (GfMul(static_cast<u8>(i), static_cast<u8>(j)) == 1) {
                        inv = static_cast<u8>(j);
                        break;
                    }
                }
            }
            u8 s = inv;
            for (int k = 1; k <= 4; k++) {
                s ^= static_cast<u8>((inv << k) | (inv >> (8 - k)));
            }
            s ^= 0x63;
            sbox[i] = s;
            inv_sbox[s] = static_cast<u8>(i);
        }
        for (int i = 0; i < 256; i++) {
            const u8 s = sbox[i];
            const u32 e = (u32{GfMul(s, 2)} << 24) | (u32{s} << 16) | (u32{s} << 8) | GfMul(s, 3);
            const u8 is = inv_sbox[i];
            const u32 d = (u32{GfMul(is, 14)} << 24) | (u32{GfMul(is, 9)} << 16) |
                          (u32{GfMul(is, 13)} << 8) | GfMul(is, 11);
            for (int r = 0; r < 4; r++) {
                te[r][i] = r == 0 ? e : Rotr32(e, 8 * r);
                td[r][i] = r == 0 ? d : Rotr32(d, 8 * r);
            }
        }
    }
};

const AesTables& Aes() {
    static const AesTables tables;
    return tables;
}

u32 LoadBe32(const u8* p) {
    return (u32{p[0]} << 24) | (u32{p[1]} << 16) | (u32{p[2]} << 8) | u32{p[3]};
}
void StoreBe32(u8* p, u32 v) {
    p[0] = static_cast<u8>(v >> 24);
    p[1] = static_cast<u8>(v >> 16);
    p[2] = static_cast<u8>(v >> 8);
    p[3] = static_cast<u8>(v);
}

} // namespace

Digest256 Sha256(std::span<const u8> data) {
    Sha256Ctx ctx;
    ctx.Update(data);
    return ctx.Final();
}

Digest256 Sha3_256(std::span<const u8> data) {
    constexpr size_t kRate = 136;
    u64 a[25] = {};
    auto absorb = [&](const u8* block) {
        for (size_t i = 0; i < kRate / 8; i++) {
            u64 lane = 0;
            for (int b = 0; b < 8; b++) {
                lane |= u64{block[i * 8 + b]} << (8 * b);
            }
            a[i] ^= lane;
        }
        KeccakF(a);
    };
    size_t pos = 0;
    while (data.size() - pos >= kRate) {
        absorb(data.data() + pos);
        pos += kRate;
    }
    u8 last[kRate] = {};
    std::memcpy(last, data.data() + pos, data.size() - pos);
    last[data.size() - pos] ^= 0x06;
    last[kRate - 1] ^= 0x80;
    absorb(last);
    Digest256 out{};
    for (size_t i = 0; i < out.size(); i++) {
        out[i] = static_cast<u8>(a[i / 8] >> (8 * (i % 8)));
    }
    return out;
}

Digest256 HmacSha256(std::span<const u8> key, std::span<const u8> data) {
    u8 k[64] = {};
    if (key.size() > 64) {
        const auto hk = Sha256(key);
        std::memcpy(k, hk.data(), hk.size());
    } else if (!key.empty()) {
        std::memcpy(k, key.data(), key.size());
    }
    u8 ipad[64], opad[64];
    for (int i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5C;
    }
    Sha256Ctx inner;
    inner.Update(ipad);
    inner.Update(data);
    const auto ih = inner.Final();
    Sha256Ctx outer;
    outer.Update(opad);
    outer.Update(ih);
    return outer.Final();
}

Aes128::Aes128(std::span<const u8, 16> key) {
    const auto& t = Aes();
    static constexpr u8 kRcon[10] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};
    for (int i = 0; i < 4; i++) {
        enc_[i] = LoadBe32(key.data() + 4 * i);
    }
    for (int i = 4; i < 44; i++) {
        u32 temp = enc_[i - 1];
        if (i % 4 == 0) {
            temp = (temp << 8) | (temp >> 24);
            temp = (u32{t.sbox[temp >> 24]} << 24) | (u32{t.sbox[(temp >> 16) & 0xFF]} << 16) |
                   (u32{t.sbox[(temp >> 8) & 0xFF]} << 8) | u32{t.sbox[temp & 0xFF]};
            temp ^= u32{kRcon[i / 4 - 1]} << 24;
        }
        enc_[i] = enc_[i - 4] ^ temp;
    }
    // Decryption schedule: round keys in reverse, with InvMixColumns applied
    // to the inner rounds (equivalent inverse cipher).
    for (int round = 0; round <= 10; round++) {
        for (int c = 0; c < 4; c++) {
            u32 w = enc_[(10 - round) * 4 + c];
            if (round != 0 && round != 10) {
                w = t.td[0][t.sbox[w >> 24]] ^ t.td[1][t.sbox[(w >> 16) & 0xFF]] ^
                    t.td[2][t.sbox[(w >> 8) & 0xFF]] ^ t.td[3][t.sbox[w & 0xFF]];
            }
            dec_[round * 4 + c] = w;
        }
    }
}

void Aes128::EncryptBlock(const u8 in[16], u8 out[16]) const {
    const auto& t = Aes();
    u32 s0 = LoadBe32(in) ^ enc_[0], s1 = LoadBe32(in + 4) ^ enc_[1];
    u32 s2 = LoadBe32(in + 8) ^ enc_[2], s3 = LoadBe32(in + 12) ^ enc_[3];
    for (int round = 1; round < 10; round++) {
        const u32* rk = &enc_[round * 4];
        const u32 t0 = t.te[0][s0 >> 24] ^ t.te[1][(s1 >> 16) & 0xFF] ^ t.te[2][(s2 >> 8) & 0xFF] ^
                       t.te[3][s3 & 0xFF] ^ rk[0];
        const u32 t1 = t.te[0][s1 >> 24] ^ t.te[1][(s2 >> 16) & 0xFF] ^ t.te[2][(s3 >> 8) & 0xFF] ^
                       t.te[3][s0 & 0xFF] ^ rk[1];
        const u32 t2 = t.te[0][s2 >> 24] ^ t.te[1][(s3 >> 16) & 0xFF] ^ t.te[2][(s0 >> 8) & 0xFF] ^
                       t.te[3][s1 & 0xFF] ^ rk[2];
        const u32 t3 = t.te[0][s3 >> 24] ^ t.te[1][(s0 >> 16) & 0xFF] ^ t.te[2][(s1 >> 8) & 0xFF] ^
                       t.te[3][s2 & 0xFF] ^ rk[3];
        s0 = t0;
        s1 = t1;
        s2 = t2;
        s3 = t3;
    }
    const u32* rk = &enc_[40];
    auto last = [&](u32 a, u32 b, u32 c, u32 d, u32 k) {
        return ((u32{t.sbox[a >> 24]} << 24) | (u32{t.sbox[(b >> 16) & 0xFF]} << 16) |
                (u32{t.sbox[(c >> 8) & 0xFF]} << 8) | u32{t.sbox[d & 0xFF]}) ^
               k;
    };
    StoreBe32(out, last(s0, s1, s2, s3, rk[0]));
    StoreBe32(out + 4, last(s1, s2, s3, s0, rk[1]));
    StoreBe32(out + 8, last(s2, s3, s0, s1, rk[2]));
    StoreBe32(out + 12, last(s3, s0, s1, s2, rk[3]));
}

void Aes128::DecryptBlock(const u8 in[16], u8 out[16]) const {
    const auto& t = Aes();
    u32 s0 = LoadBe32(in) ^ dec_[0], s1 = LoadBe32(in + 4) ^ dec_[1];
    u32 s2 = LoadBe32(in + 8) ^ dec_[2], s3 = LoadBe32(in + 12) ^ dec_[3];
    for (int round = 1; round < 10; round++) {
        const u32* rk = &dec_[round * 4];
        const u32 t0 = t.td[0][s0 >> 24] ^ t.td[1][(s3 >> 16) & 0xFF] ^ t.td[2][(s2 >> 8) & 0xFF] ^
                       t.td[3][s1 & 0xFF] ^ rk[0];
        const u32 t1 = t.td[0][s1 >> 24] ^ t.td[1][(s0 >> 16) & 0xFF] ^ t.td[2][(s3 >> 8) & 0xFF] ^
                       t.td[3][s2 & 0xFF] ^ rk[1];
        const u32 t2 = t.td[0][s2 >> 24] ^ t.td[1][(s1 >> 16) & 0xFF] ^ t.td[2][(s0 >> 8) & 0xFF] ^
                       t.td[3][s3 & 0xFF] ^ rk[2];
        const u32 t3 = t.td[0][s3 >> 24] ^ t.td[1][(s2 >> 16) & 0xFF] ^ t.td[2][(s1 >> 8) & 0xFF] ^
                       t.td[3][s0 & 0xFF] ^ rk[3];
        s0 = t0;
        s1 = t1;
        s2 = t2;
        s3 = t3;
    }
    const u32* rk = &dec_[40];
    auto last = [&](u32 a, u32 b, u32 c, u32 d, u32 k) {
        return ((u32{t.inv_sbox[a >> 24]} << 24) | (u32{t.inv_sbox[(b >> 16) & 0xFF]} << 16) |
                (u32{t.inv_sbox[(c >> 8) & 0xFF]} << 8) | u32{t.inv_sbox[d & 0xFF]}) ^
               k;
    };
    StoreBe32(out, last(s0, s3, s2, s1, rk[0]));
    StoreBe32(out + 4, last(s1, s0, s3, s2, rk[1]));
    StoreBe32(out + 8, last(s2, s1, s0, s3, rk[2]));
    StoreBe32(out + 12, last(s3, s2, s1, s0, rk[3]));
}

void XtsDecrypt(const Aes128& data_key, const Aes128& tweak_key, u64 sector, std::span<u8> data) {
    u8 tweak[16] = {};
    for (int i = 0; i < 8; i++) {
        tweak[i] = static_cast<u8>(sector >> (8 * i));
    }
    tweak_key.EncryptBlock(tweak, tweak);
    u8 block[16];
    for (size_t off = 0; off + 16 <= data.size(); off += 16) {
        for (int i = 0; i < 16; i++) {
            block[i] = data[off + i] ^ tweak[i];
        }
        data_key.DecryptBlock(block, block);
        for (int i = 0; i < 16; i++) {
            data[off + i] = block[i] ^ tweak[i];
        }
        // Multiply the tweak by x in GF(2^128), little-endian.
        u8 carry = 0;
        for (int i = 0; i < 16; i++) {
            const u8 next = tweak[i] >> 7;
            tweak[i] = static_cast<u8>((tweak[i] << 1) | carry);
            carry = next;
        }
        if (carry) {
            tweak[0] ^= 0x87;
        }
    }
}

void CbcDecrypt(const Aes128& key, std::span<const u8, 16> iv, std::span<u8> data) {
    u8 prev[16], cur[16];
    std::memcpy(prev, iv.data(), 16);
    for (size_t off = 0; off + 16 <= data.size(); off += 16) {
        std::memcpy(cur, data.data() + off, 16);
        key.DecryptBlock(cur, data.data() + off);
        for (int i = 0; i < 16; i++) {
            data[off + i] ^= prev[i];
        }
        std::memcpy(prev, cur, 16);
    }
}

} // namespace Common::Crypto
