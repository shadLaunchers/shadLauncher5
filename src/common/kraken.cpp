// SPDX-FileCopyrightText: Copyright 2026 shadLauncher5 Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "kraken.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace Common::Kraken {

namespace {

constexpr size_t kBlockSize = 0x40000;
constexpr size_t kChunkSize = 0x20000;
constexpr u32 kCodecKraken = 6;

struct Corrupt {
    const char* what;
};

[[noreturn]] void Fail(const char* what) {
    throw Corrupt{what};
}

void Require(bool condition, const char* what) {
    if (!condition) {
        Fail(what);
    }
}

u32 Be16(const u8* p) {
    return (u32{p[0]} << 8) | p[1];
}
u32 Be24(const u8* p) {
    return (u32{p[0]} << 16) | (u32{p[1]} << 8) | p[2];
}
u32 Be32(const u8* p) {
    return (u32{p[0]} << 24) | Be24(p + 1);
}
u32 Le16(const u8* p) {
    return u32{p[0]} | (u32{p[1]} << 8);
}
u32 Le24(const u8* p) {
    return Le16(p) | (u32{p[2]} << 16);
}
u64 Le64(const u8* p) {
    u64 v = 0;
    for (int i = 7; i >= 0; i--) {
        v = (v << 8) | p[i];
    }
    return v;
}
u64 Be64(const u8* p) {
    u64 v = 0;
    for (int i = 0; i < 8; i++) {
        v = (v << 8) | p[i];
    }
    return v;
}

// Maps -1 -> 1, 1 -> 2, -2 -> 3 back: 0, 1, 2, 3 -> 0, -1, 1, -2.
int Unzigzag(u32 v) {
    return static_cast<int>(v >> 1) ^ -static_cast<int>(v & 1);
}

template <bool Backward>
class LsbReader {
public:
    LsbReader(const u8* data, size_t size) : data_(data), size_(size) {
        Refill();
    }
    void Refill() {
        if (count_ > 56) {
            return;
        }
        if (loaded_ + 8 <= size_) {
            const u64 v = Backward ? Be64(data_ + size_ - loaded_ - 8) : Le64(data_ + loaded_);
            buf_ |= v << count_;
            loaded_ += (63 - count_) >> 3;
            count_ |= 56;
            return;
        }
        while (count_ <= 56) {
            const u64 b =
                loaded_ < size_ ? (Backward ? data_[size_ - 1 - loaded_] : data_[loaded_]) : 0;
            buf_ |= b << count_;
            loaded_++;
            count_ += 8;
        }
    }
    // n <= 32, and at most 56 bits since the last Refill.
    [[nodiscard]] u32 Peek(int n) const {
        return static_cast<u32>(buf_ & ((u64{1} << n) - 1));
    }
    void Skip(int n) {
        buf_ >>= n;
        count_ -= n;
    }
    u32 Read(int n) {
        if (n == 0) {
            return 0;
        }
        Refill();
        const u32 v = Peek(n);
        Skip(n);
        return v;
    }
    [[nodiscard]] size_t ConsumedBits() const {
        return loaded_ * 8 - static_cast<size_t>(count_);
    }
    [[nodiscard]] size_t ConsumedBytes() const {
        return (ConsumedBits() + 7) / 8;
    }

private:
    const u8* data_;
    size_t size_;
    size_t loaded_ = 0;
    u64 buf_ = 0;
    int count_ = 0;
};

template <bool Backward>
class MsbReader {
public:
    MsbReader(const u8* data, size_t size) : data_(data), size_(size) {
        Refill();
    }
    void Refill() {
        if (count_ > 56) {
            return;
        }
        if (loaded_ + 8 <= size_) {
            const u64 v = Backward ? Le64(data_ + size_ - loaded_ - 8) : Be64(data_ + loaded_);
            buf_ |= v >> count_;
            loaded_ += (63 - count_) >> 3;
            count_ |= 56;
            return;
        }
        while (count_ <= 56) {
            const u64 b =
                loaded_ < size_ ? (Backward ? data_[size_ - 1 - loaded_] : data_[loaded_]) : 0;
            buf_ |= b << (56 - count_);
            loaded_++;
            count_ += 8;
        }
    }
    // 1 <= n <= 32.
    [[nodiscard]] u32 Peek(int n) {
        Refill();
        return static_cast<u32>(buf_ >> (64 - n));
    }
    void Skip(int n) {
        buf_ <<= n;
        count_ -= n;
    }
    u32 Read(int n) {
        if (n == 0) {
            return 0;
        }
        const u32 v = Peek(n);
        Skip(n);
        return v;
    }
    bool Bit() {
        return Read(1) != 0;
    }
    // Zero bits before the next one bit (64 when none are buffered).
    int LeadingZeros() {
        Refill();
        return std::countl_zero(buf_);
    }
    u32 Unary() {
        u32 zeros = 0;
        for (;;) {
            const int lz = LeadingZeros();
            if (lz < 32) {
                Skip(lz + 1);
                return zeros + static_cast<u32>(lz);
            }
            Skip(32);
            zeros += 32;
            Require(ConsumedBits() <= size_ * 8, "Unary value runs past the data.");
        }
    }
    // A value in [0, 2 * min(257 - n, n)) in truncated binary.
    u32 Truncated(u32 symbols) {
        if (symbols == 256) {
            return 0;
        }
        const u32 range = 2 * std::min(257 - symbols, symbols);
        const int width = std::bit_width(range - 1);
        const u32 v = Peek(width);
        const u32 short_codes = (1u << width) - range;
        if ((v >> 1) >= short_codes) {
            Skip(width);
            return v - short_codes;
        }
        Skip(width - 1);
        return v >> 1;
    }
    [[nodiscard]] size_t ConsumedBits() const {
        return loaded_ * 8 - static_cast<size_t>(count_);
    }
    [[nodiscard]] size_t ConsumedBytes() const {
        return (ConsumedBits() + 7) / 8;
    }

private:
    const u8* data_;
    size_t size_;
    size_t loaded_ = 0;
    u64 buf_ = 0;
    int count_ = 0;
};

using BitsF = MsbReader<false>;

// ---------------------------------------------------------------------------
// Entropy-coded arrays

struct Cursor {
    const u8* p;
    const u8* end;
    [[nodiscard]] size_t Left() const {
        return static_cast<size_t>(end - p);
    }
};

size_t DecodeArray(Cursor& in, u8* out, size_t capacity);

std::vector<u8> DecodeArrayToVector(Cursor& in, size_t capacity) {
    std::vector<u8> v(capacity);
    v.resize(DecodeArray(in, v.data(), capacity));
    return v;
}

// A run of consecutive symbols, used by the newer table formats.
struct SymbolRange {
    u32 first;
    u32 count;
};

std::vector<SymbolRange> ReadRanges(BitsF& bits, u32 symbols, u32 extra_count, const u8* extra) {
    std::vector<SymbolRange> ranges;
    u32 next = 0;
    if (extra_count & 1) {
        const u32 w = *extra++;
        Require(w < 8, "Bad symbol range.");
        next = bits.Read(static_cast<int>(w + 1)) + (1u << (w + 1)) - 1;
    }
    u32 used = 0;
    for (u32 i = 0; i < extra_count / 2; i++) {
        const u32 count_bits = extra[0];
        const u32 gap_bits = extra[1];
        extra += 2;
        Require(count_bits < 9 && gap_bits < 8, "Bad symbol range.");
        const u32 count = bits.Read(static_cast<int>(count_bits)) + (1u << count_bits);
        const u32 gap = bits.Read(static_cast<int>(gap_bits + 1)) + (1u << (gap_bits + 1)) - 1;
        ranges.push_back({next, count});
        used += count;
        next += count + gap;
    }
    Require(next < 256 && used < symbols && next + symbols - used <= 256, "Bad symbol range.");
    ranges.push_back({next, symbols - used});
    return ranges;
}

// --- Huffman ---------------------------------------------------------------

constexpr int kHuffMaxBits = 11;
constexpr size_t kHuffSlots = size_t{1} << kHuffMaxBits;

struct CodeLengths {
    // Symbols per code length, in the order the table lists them.
    std::array<std::array<u8, 256>, kHuffMaxBits + 1> syms{};
    std::array<u32, kHuffMaxBits + 1> count{};
    u32 total = 0;

    void Add(u32 sym, u32 len) {
        Require(len >= 1 && len <= kHuffMaxBits && sym < 256, "Bad Huffman code length.");
        Require(count[len] < 256, "Too many Huffman codes.");
        syms[len][count[len]++] = static_cast<u8>(sym);
        total++;
    }
};

void ReadLengthsGamma(BitsF& bits, CodeLengths& lengths) {
    const u32 forced = bits.Read(2);
    const int max_zeros = 20 >> forced;
    int average_x4 = 32;
    u32 sym = 0;
    auto run_length = [&] {
        const int lz = bits.LeadingZeros();
        Require(lz <= 7, "Bad Huffman table.");
        return bits.Read(2 * lz + 2) - 1;
    };
    bool unused_first = !bits.Bit();
    for (;;) {
        if (unused_first) {
            sym += run_length();
            if (sym >= 256) {
                break;
            }
        }
        unused_first = true;
        const u32 used = run_length();
        Require(sym + used <= 256, "Bad Huffman table.");
        for (u32 i = 0; i < used; i++) {
            const int lz = bits.LeadingZeros();
            Require(lz <= max_zeros, "Bad Huffman table.");
            bits.Skip(lz + 1);
            const u32 v = (static_cast<u32>(lz) << forced) | bits.Read(static_cast<int>(forced));
            const int len = Unzigzag(v) + ((average_x4 + 2) >> 2);
            Require(len >= 1 && len <= kHuffMaxBits, "Bad Huffman code length.");
            average_x4 = len + ((3 * average_x4 + 2) >> 2);
            lengths.Add(sym++, static_cast<u32>(len));
        }
        if (sym == 256) {
            break;
        }
    }
    Require(sym == 256 && lengths.total >= 2, "Bad Huffman table.");
}

void ReadLengthsList(BitsF& bits, CodeLengths& lengths) {
    const u32 n = bits.Read(8);
    Require(n != 0, "Empty Huffman table.");
    if (n == 1) {
        lengths.Add(bits.Read(8), 1);
        return;
    }
    const u32 len_bits = bits.Read(3);
    Require(len_bits <= 4, "Bad Huffman table.");
    for (u32 i = 0; i < n; i++) {
        const u32 sym = bits.Read(8);
        lengths.Add(sym, bits.Read(static_cast<int>(len_bits)) + 1);
    }
}

void ReadLengthsRice(BitsF& bits, CodeLengths& lengths) {
    const u32 forced = bits.Read(2);
    const u32 n = bits.Read(8) + 1;
    const u32 extra = bits.Truncated(n);
    std::vector<u8> values(n + extra);
    for (auto& v : values) {
        const u32 q = bits.Unary();
        Require(q < 256, "Bad Huffman table.");
        v = static_cast<u8>(q);
    }
    std::vector<u32> lens(n);
    for (u32 i = 0; i < n; i++) {
        lens[i] = (u32{values[i]} << forced) | bits.Read(static_cast<int>(forced));
    }
    int running = 0x1E;
    for (auto& len : lens) {
        const int delta = Unzigzag(len);
        const int l = delta + (running >> 2) + 1;
        Require(l >= 1 && l <= kHuffMaxBits, "Bad Huffman code length.");
        len = static_cast<u32>(l);
        running += delta;
    }
    const auto ranges = ReadRanges(bits, n, extra, values.data() + n);
    u32 k = 0;
    for (const auto& r : ranges) {
        for (u32 s = r.first; s < r.first + r.count; s++) {
            lengths.Add(s, lens[k++]);
        }
    }
}

struct HuffTable {
    // Indexed by the next 11 stream bits, least significant first.
    std::array<u8, kHuffSlots> sym;
    std::array<u8, kHuffSlots> len;
};

constexpr std::array<u16, kHuffSlots> MakeReverse11() {
    std::array<u16, kHuffSlots> r{};
    for (u32 i = 0; i < kHuffSlots; i++) {
        u32 v = 0;
        for (int b = 0; b < kHuffMaxBits; b++) {
            v |= ((i >> b) & 1) << (kHuffMaxBits - 1 - b);
        }
        r[i] = static_cast<u16>(v);
    }
    return r;
}
constexpr auto kReverse11 = MakeReverse11();

void BuildHuffTable(const CodeLengths& lengths, HuffTable& table) {
    u32 slot = 0;
    for (u32 len = 1; len <= kHuffMaxBits; len++) {
        const u32 span = 1u << (kHuffMaxBits - len);
        for (u32 i = 0; i < lengths.count[len]; i++) {
            Require(slot + span <= kHuffSlots, "Huffman code is oversubscribed.");
            for (u32 k = 0; k < span; k++) {
                const u16 at = kReverse11[slot + k];
                table.sym[at] = lengths.syms[len][i];
                table.len[at] = static_cast<u8>(len);
            }
            slot += span;
        }
    }
    Require(slot == kHuffSlots, "Huffman code is incomplete.");
}

void DecodeHuffmanStreams(const HuffTable& t, const u8* src, size_t size, size_t split, u8* out,
                          size_t n) {
    Require(split <= size, "Bad Huffman stream split.");
    LsbReader<false> a(src, split);
    LsbReader<true> b(src + split, size - split);
    LsbReader<false> c(src + split, size - split);
    size_t i = 0;
    auto next = [&](auto& r) {
        const u32 k = r.Peek(kHuffMaxBits);
        out[i++] = t.sym[k];
        r.Skip(t.len[k]);
    };
    while (n - i >= 15) {
        a.Refill();
        b.Refill();
        c.Refill();
        for (int k = 0; k < 5; k++) {
            next(a);
            next(b);
            next(c);
        }
    }
    while (i < n) {
        a.Refill();
        next(a);
        if (i < n) {
            b.Refill();
            next(b);
        }
        if (i < n) {
            c.Refill();
            next(c);
        }
    }
    Require(a.ConsumedBytes() == split, "Huffman stream A has the wrong length.");
    Require(b.ConsumedBytes() + c.ConsumedBytes() == size - split,
            "Huffman streams B and C have the wrong length.");
}

void DecodeHuffman(const u8* src, size_t size, u8* out, size_t n, bool six_streams) {
    BitsF bits(src, size);
    CodeLengths lengths;
    if (!bits.Bit()) {
        if (bits.Bit()) {
            ReadLengthsGamma(bits, lengths);
        } else {
            ReadLengthsList(bits, lengths);
        }
    } else {
        Require(!bits.Bit(), "Unknown Huffman table format.");
        ReadLengthsRice(bits, lengths);
    }
    const size_t table_bytes = bits.ConsumedBytes();
    Require(table_bytes <= size, "Huffman table runs past the data.");
    if (lengths.total == 1) {
        u8 only = 0;
        for (u32 len = 1; len <= kHuffMaxBits; len++) {
            if (lengths.count[len] != 0) {
                only = lengths.syms[len][0];
            }
        }
        std::memset(out, only, n);
        return;
    }
    auto table = std::make_unique<HuffTable>();
    BuildHuffTable(lengths, *table);

    const u8* p = src + table_bytes;
    const u8* end = src + size;
    if (!six_streams) {
        Require(end - p >= 3, "Huffman data is truncated.");
        const size_t split = Le16(p);
        DecodeHuffmanStreams(*table, p + 2, static_cast<size_t>(end - p - 2), split, out, n);
        return;
    }
    Require(end - p >= 6, "Huffman data is truncated.");
    const size_t half_split = Le24(p);
    p += 3;
    Require(half_split <= static_cast<size_t>(end - p), "Bad Huffman stream split.");
    const u8* mid = p + half_split;
    const size_t left_split = Le16(p);
    p += 2;
    Require(mid >= p && end - mid >= 2, "Bad Huffman stream split.");
    const size_t right_split = Le16(mid);
    const size_t half = (n + 1) / 2;
    DecodeHuffmanStreams(*table, p, static_cast<size_t>(mid - p), left_split, out, half);
    DecodeHuffmanStreams(*table, mid + 2, static_cast<size_t>(end - mid - 2), right_split,
                         out + half, n - half);
}

// --- tANS ------------------------------------------------------------------

struct TansEntry {
    u8 symbol;
    u8 bits;
    u16 base; // next state = base + the next `bits` stream bits
};

struct TansWeights {
    std::vector<u8> singles;                  // weight 1, by symbol
    std::vector<std::pair<u8, u32>> weighted; // weight >= 2, by symbol
};

TansWeights ReadTansWeights(BitsF& bits, u32 log2_states) {
    const u32 states = 1u << log2_states;
    TansWeights w;
    if (bits.Bit()) {
        // Rice-coded weights over symbol ranges, relative to a running average.
        const u32 q = bits.Read(3);
        const u32 n = bits.Read(8) + 1;
        Require(n >= 2, "Bad tANS table.");
        const u32 extra = bits.Truncated(n);
        std::vector<u8> values(n + extra);
        for (auto& v : values) {
            const u32 x = bits.Unary();
            Require(x < 256, "Bad tANS table.");
            v = static_cast<u8>(x);
        }
        const auto ranges = ReadRanges(bits, n, extra, values.data() + n);
        int average = 6;
        u32 sum = 0;
        u32 k = 0;
        for (const auto& r : ranges) {
            for (u32 s = r.first; s < r.first + r.count; s++) {
                const u32 nbits = q + values[k++];
                Require(nbits <= 15, "Bad tANS table.");
                int v = static_cast<int>(bits.Read(static_cast<int>(nbits)) + (1u << nbits) -
                                         (1u << q));
                const int quarter = average >> 2;
                int limit = 2 * quarter;
                if (v <= limit) {
                    v = quarter + Unzigzag(static_cast<u32>(v));
                }
                limit = std::min(limit, v);
                v += 1;
                average += limit - quarter;
                Require(v >= 1, "Bad tANS weight.");
                if (v == 1) {
                    w.singles.push_back(static_cast<u8>(s));
                } else {
                    w.weighted.emplace_back(static_cast<u8>(s), static_cast<u32>(v));
                }
                sum += static_cast<u32>(v);
            }
        }
        Require(sum == states, "tANS weights do not add up.");
        return w;
    }
    // A short list of (symbol, weight delta); the last symbol takes the rest.
    std::array<bool, 256> seen{};
    const u32 n = bits.Read(3) + 1;
    const u32 delta_bits = bits.Read(std::bit_width(log2_states));
    Require(delta_bits != 0 && delta_bits <= log2_states, "Bad tANS table.");
    u32 weight = 0;
    u32 total = 0;
    auto add = [&](u32 sym, u32 wt) {
        Require(!seen[sym], "Repeated tANS symbol.");
        seen[sym] = true;
        if (wt == 1) {
            w.singles.push_back(static_cast<u8>(sym));
        } else {
            w.weighted.emplace_back(static_cast<u8>(sym), wt);
        }
    };
    for (u32 i = 0; i < n; i++) {
        const u32 sym = bits.Read(8);
        weight += bits.Read(static_cast<int>(delta_bits));
        Require(weight != 0, "Bad tANS weight.");
        add(sym, weight);
        total += weight;
    }
    const u32 last = bits.Read(8);
    Require(total < states && states - total >= weight && states - total > 1, "Bad tANS table.");
    add(last, states - total);
    std::sort(w.singles.begin(), w.singles.end());
    std::sort(w.weighted.begin(), w.weighted.end());
    return w;
}

std::vector<TansEntry> BuildTansTable(const TansWeights& w, u32 log2_states) {
    const u32 states = 1u << log2_states;
    std::vector<TansEntry> table(states);
    auto entry = [&](u8 symbol, u32 k) {
        // The k-th state of a symbol (k counts up from its weight).
        const u32 bits = log2_states - static_cast<u32>(std::bit_width(k) - 1);
        return TansEntry{symbol, static_cast<u8>(bits),
                         static_cast<u16>((k << bits) & (states - 1))};
    };
    // Weight-1 symbols take the last slots.
    const u32 shared = states - static_cast<u32>(w.singles.size());
    for (size_t i = 0; i < w.singles.size(); i++) {
        table[shared + i] = entry(w.singles[i], 1);
    }
    std::array<u32, 4> lane{};
    const u32 quarter = shared >> 2;
    const u32 rest = shared & 3;
    lane[0] = 0;
    lane[1] = lane[0] + quarter + (rest > 0);
    lane[2] = lane[1] + quarter + (rest > 1);
    lane[3] = lane[2] + quarter + (rest > 2);
    const std::array<u32, 4> lane_end{lane[1], lane[2], lane[3], shared};
    u32 dealt = 0;
    for (const auto& [symbol, weight] : w.weighted) {
        u32 k = weight;
        for (u32 j = 0; j < 4; j++) {
            const u32 take = (weight + ((dealt - j - 1) & 3)) >> 2;
            for (u32 t = 0; t < take; t++) {
                Require(lane[j] < lane_end[j], "tANS table overflow.");
                table[lane[j]++] = entry(symbol, k++);
            }
        }
        dealt += weight;
    }
    return table;
}

void DecodeTans(const u8* src, size_t size, u8* out, size_t n) {
    Require(size >= 8 && n >= 5, "tANS block is too small.");
    BitsF bits(src, size);
    Require(!bits.Bit(), "Reserved tANS bit set.");
    const u32 log2_states = bits.Read(2) + 8;
    const auto weights = ReadTansWeights(bits, log2_states);
    const size_t table_bytes = bits.ConsumedBytes();
    Require(table_bytes < size, "tANS table runs past the data.");
    const auto table = BuildTansTable(weights, log2_states);

    const u8* data = src + table_bytes;
    const size_t data_size = size - table_bytes;
    LsbReader<false> f(data, data_size);
    LsbReader<true> b(data, data_size);
    const int lb = static_cast<int>(log2_states);
    std::array<u32, 5> state{};
    state[0] = f.Read(lb);
    state[1] = b.Read(lb);
    state[2] = f.Read(lb);
    state[3] = b.Read(lb);
    state[4] = f.Read(lb);

    struct Step {
        bool forward;
        u8 state;
    };
    static constexpr std::array<Step, 10> kRound{{{true, 0},
                                                  {true, 1},
                                                  {true, 2},
                                                  {true, 3},
                                                  {true, 4},
                                                  {false, 0},
                                                  {false, 1},
                                                  {false, 2},
                                                  {false, 3},
                                                  {false, 4}}};
    const size_t symbols = n - 5;
    size_t i = 0;
    while (i < symbols) {
        for (const Step& step : kRound) {
            u32& s = state[step.state];
            const TansEntry& e = table[s];
            out[i++] = e.symbol;
            s = e.base + (step.forward ? f.Read(e.bits) : b.Read(e.bits));
            if (i == symbols) {
                break;
            }
        }
    }
    Require(f.ConsumedBytes() + b.ConsumedBytes() == data_size,
            "tANS streams have the wrong length.");
    for (int k = 0; k < 5; k++) {
        Require(state[k] < 256, "Bad final tANS state.");
        out[symbols + k] = static_cast<u8>(state[k]);
    }
}

// --- RLE -------------------------------------------------------------------

void DecodeRle(const u8* src, size_t size, u8* out, size_t n) {
    if (size <= 1) {
        Require(size == 1, "Empty RLE block.");
        std::memset(out, src[0], n);
        return;
    }
    std::vector<u8> unpacked;
    const u8* lit = src + 1;
    const u8* cmd = src + size;
    if (src[0] != 0) {
        Cursor in{src, src + size};
        unpacked = DecodeArrayToVector(in, 2 * n + 64);
        unpacked.insert(unpacked.end(), in.p, src + size);
        lit = unpacked.data();
        cmd = unpacked.data() + unpacked.size();
    }
    u8 fill = 0;
    size_t o = 0;
    while (lit < cmd) {
        const u32 c = cmd[-1];
        size_t copy = 0;
        size_t run = 0;
        if (c == 0 || c >= 0x30) {
            copy = (~c) & 0xF;
            run = c >> 4;
            cmd -= 1;
        } else if (c == 1) {
            fill = *lit++;
            cmd -= 1;
            continue;
        } else {
            Require(cmd - lit >= 2, "Truncated RLE command.");
            const u32 word = (c << 8) | cmd[-2];
            cmd -= 2;
            if (c >= 0x10) {
                const u32 d = word - 0x1000;
                copy = d & 0x3F;
                run = d >> 6;
            } else if (c >= 9) {
                run = size_t{word - 0x8FF} * 128;
            } else {
                copy = size_t{word - 0x1FF} * 64;
            }
        }
        Require(lit <= cmd && copy <= static_cast<size_t>(cmd - lit) && copy + run <= n - o,
                "RLE command out of range.");
        std::memcpy(out + o, lit, copy);
        lit += copy;
        o += copy;
        std::memset(out + o, fill, run);
        o += run;
    }
    Require(lit == cmd && o == n, "RLE data has the wrong length.");
}

// --- Split and multi-array -------------------------------------------------

size_t DecodeMultiArray(const u8* src, size_t size, u8* out, size_t capacity) {
    Cursor in{src, src + size};
    Require(in.Left() >= 4, "Multi-array block is too small.");
    const u32 head = *in.p++;
    Require((head & 0x80) != 0, "Bad multi-array header.");
    const u32 sources = head & 0x3F;
    if (sources == 0) {
        return DecodeArray(in, out, capacity);
    }
    std::vector<std::vector<u8>> parts(sources);
    std::vector<size_t> taken(sources, 0);
    size_t total = 0;
    for (auto& part : parts) {
        part = DecodeArrayToVector(in, capacity);
        total += part.size();
        Require(total <= capacity, "Multi-array output is too large.");
    }
    Require(in.Left() >= 3, "Multi-array block is truncated.");
    const u32 q = Le16(in.p);
    in.p += 2;
    const bool packed = (q & 0x8000) != 0;
    std::vector<u8> index = DecodeArrayToVector(in, kBlockSize);
    const size_t num_index = index.size();
    Require(num_index >= 2, "Bad multi-array index.");
    std::vector<u8> width;
    size_t num_lengths;
    if (packed) {
        width.resize(num_index);
        for (size_t i = 0; i < num_index; i++) {
            width[i] = index[i] >> 4;
            index[i] &= 0xF;
        }
        num_lengths = num_index;
    } else {
        num_lengths = num_index - 1;
        width = DecodeArrayToVector(in, num_lengths);
        Require(width.size() == num_lengths, "Bad multi-array lengths.");
    }
    for (size_t i = 0; i < num_lengths; i++) {
        Require(width[i] <= 16, "Bad multi-array length width.");
    }
    const size_t bits_size = q & 0x3FFF;
    Require(in.Left() >= bits_size, "Multi-array bits are truncated.");
    MsbReader<false> fwd(in.p, bits_size);
    MsbReader<true> bwd(in.p, bits_size);
    std::vector<u32> lengths(num_lengths);
    for (size_t i = 0; i < num_lengths; i++) {
        const int w = width[i];
        lengths[i] = (1u << w) | ((i & 1) ? bwd.Read(w) : fwd.Read(w));
    }
    Require(fwd.ConsumedBits() + bwd.ConsumedBits() <= bits_size * 8, "Multi-array bits overrun.");
    Require(index[num_index - 1] == 0, "Bad multi-array index.");

    size_t o = 0;
    size_t ii = 0;
    size_t li = 0;
    u32 s;
    while ((s = index[ii++]) != 0) {
        Require(s <= sources && li < num_lengths, "Bad multi-array index.");
        const size_t len = lengths[li++];
        auto& part = parts[s - 1];
        Require(len <= part.size() - taken[s - 1] && len <= capacity - o,
                "Multi-array piece out of range.");
        std::memcpy(out + o, part.data() + taken[s - 1], len);
        taken[s - 1] += len;
        o += len;
    }
    li += packed ? 1 : 0;
    Require(ii == num_index && li == num_lengths, "Multi-array index has the wrong length.");
    for (size_t k = 0; k < sources; k++) {
        Require(taken[k] == parts[k].size(), "Multi-array data left over.");
    }
    Require(in.p + bits_size == src + size, "Multi-array block has the wrong size.");
    return o;
}

void DecodeSplit(const u8* src, size_t size, u8* out, size_t n) {
    Require(size >= 6, "Split block is too small.");
    const u32 count = src[0] & 0x7F;
    Require(count >= 2, "Bad split block.");
    if (src[0] & 0x80) {
        Require(DecodeMultiArray(src, size, out, n) == n, "Multi-array output has the wrong size.");
        return;
    }
    Cursor in{src + 1, src + size};
    size_t o = 0;
    for (u32 i = 0; i < count; i++) {
        o += DecodeArray(in, out + o, n - o);
    }
    Require(o == n && in.p == in.end, "Split block has the wrong size.");
}

size_t DecodeArray(Cursor& in, u8* out, size_t capacity) {
    Require(in.Left() >= 2, "Truncated array header.");
    const u8* p = in.p;
    const u32 type = (p[0] >> 4) & 7;
    if (type == 0) {
        size_t n;
        if (p[0] >= 0x80) {
            n = Be16(p) & 0xFFF;
            p += 2;
        } else {
            Require(in.Left() >= 3, "Truncated array header.");
            n = Be24(p);
            Require(n <= 0x3FFFF, "Bad stored array size.");
            p += 3;
        }
        Require(n <= static_cast<size_t>(in.end - p) && n <= capacity,
                "Stored array out of range.");
        if (n != 0) {
            std::memcpy(out, p, n);
        }
        in.p = p + n;
        return n;
    }
    Require(type <= 5, "Unknown array coder.");
    size_t packed;
    size_t n;
    if (p[0] >= 0x80) {
        Require(in.Left() >= 3, "Truncated array header.");
        const u32 v = Be24(p);
        packed = v & 0x3FF;
        n = packed + ((v >> 10) & 0x3FF) + 1;
        p += 3;
    } else {
        Require(in.Left() >= 5, "Truncated array header.");
        const u32 v = Be32(p + 1);
        packed = v & 0x3FFFF;
        n = (((v >> 18) | (u32{p[0]} << 14)) & 0x3FFFF) + 1;
        Require(packed < n, "Bad array sizes.");
        p += 5;
    }
    Require(packed <= static_cast<size_t>(in.end - p), "Array data is truncated.");
    Require(n <= capacity, "Array is larger than its destination.");
    switch (type) {
    case 1:
        DecodeTans(p, packed, out, n);
        break;
    case 2:
        DecodeHuffman(p, packed, out, n, false);
        break;
    case 3:
        DecodeRle(p, packed, out, n);
        break;
    case 4:
        DecodeHuffman(p, packed, out, n, true);
        break;
    case 5:
        DecodeSplit(p, packed, out, n);
        break;
    }
    in.p = p + packed;
    return n;
}

// ---------------------------------------------------------------------------
// LZ

struct LzTables {
    std::vector<u8> literals;
    std::vector<u8> commands;
    std::vector<s32> offsets; // negative distances
    std::vector<u32> lengths; // long literal and match lengths, already + 3
};

u32 ReadDistance(auto& bits, u32 code) {
    if (code < 0xF0) {
        const int n = static_cast<int>(code >> 4) + 4;
        const u32 v = (1u << n) | bits.Read(n);
        return (v << 4) + (code & 0xF) - 248;
    }
    const int n = static_cast<int>(code - 0xF0) + 4;
    const u64 v = (u64{1} << n) | bits.Read(n);
    const u64 d = 8322816 + (v << 12) + bits.Read(12);
    Require(d < 0x80000000u, "LZ distance out of range.");
    return static_cast<u32>(d);
}

u32 ReadLongLength(auto& bits) {
    const int lz = bits.LeadingZeros();
    Require(lz <= 12, "Bad LZ length.");
    bits.Skip(lz);
    return bits.Read(lz + 7) - 64;
}

void ReadLzTables(u32 mode, const u8* src, const u8* end, u8* dst, size_t dst_size, size_t position,
                  LzTables& t) {
    Require(mode <= 1, "Unknown LZ literal mode.");
    Require(end - src >= 13, "LZ chunk is too small.");
    if (position == 0) {
        // The very first 8 bytes of a stream are stored raw.
        std::memcpy(dst, src, 8);
        src += 8;
    }
    size_t excess = 0;
    bool has_excess = false;
    if (*src & 0x80) {
        const u8 flag = *src++;
        Require((flag & 0xC0) == 0x80, "Bad LZ excess flag.");
        has_excess = true;
        excess = flag & 0x3F;
        if (excess > 0x1F) {
            Require(src < end, "LZ chunk is truncated.");
            excess += size_t{*src++} * 0x20;
        }
        Require(excess <= static_cast<size_t>(end - src), "LZ excess runs past the chunk.");
        end -= excess;
    }
    Cursor in{src, end};
    t.literals = DecodeArrayToVector(in, dst_size);
    t.commands = DecodeArrayToVector(in, dst_size);
    Require(in.Left() >= 3, "LZ chunk is truncated.");
    int scale = 0;
    std::vector<u8> offset_codes;
    std::vector<u8> low_bits;
    if (in.p[0] & 0x80) {
        scale = in.p[0] - 127;
        in.p++;
        offset_codes = DecodeArrayToVector(in, t.commands.size());
        if (scale != 1) {
            low_bits = DecodeArrayToVector(in, offset_codes.size());
            Require(low_bits.size() == offset_codes.size(), "Bad LZ offset arrays.");
        }
    } else {
        offset_codes = DecodeArrayToVector(in, t.commands.size());
    }
    const std::vector<u8> length_codes = DecodeArrayToVector(in, dst_size / 4);

    // The rest is two bitstreams, from the front and from the back.
    const size_t size = in.Left();
    MsbReader<false> a(in.p, size);
    MsbReader<true> b(in.p, size);
    u32 long_count;
    if (has_excess) {
        long_count = static_cast<u32>(std::count(length_codes.begin(), length_codes.end(), 255));
    } else {
        const int lz = b.LeadingZeros();
        Require(lz <= 18, "Bad LZ length count.");
        b.Skip(lz);
        long_count = b.Read(lz + 1) - 1;
    }
    Require(long_count <= 512, "Too many long LZ lengths.");

    t.offsets.resize(offset_codes.size());
    for (size_t i = 0; i < offset_codes.size(); i++) {
        const u32 code = offset_codes[i];
        s32 offset;
        if (scale == 0) {
            offset = -static_cast<s32>((i & 1) ? ReadDistance(b, code) : ReadDistance(a, code));
        } else {
            const int k = static_cast<int>(code >> 3);
            Require(k <= 26, "Bad LZ offset code.");
            const u32 extra = (i & 1) ? b.Read(k) : a.Read(k);
            offset = 8 - static_cast<s32>(((8 + (code & 7)) << k) | extra);
        }
        t.offsets[i] = offset;
    }
    if (scale > 1) {
        for (size_t i = 0; i < t.offsets.size(); i++) {
            const s64 v = s64{scale} * t.offsets[i] - low_bits[i];
            Require(v >= -0x7FFFFFFF, "LZ offset out of range.");
            t.offsets[i] = static_cast<s32>(v);
        }
    }
    std::vector<u32> long_lengths(long_count);
    if (has_excess) {
        MsbReader<false> ea(end, excess);
        MsbReader<true> eb(end, excess);
        for (u32 i = 0; i < long_count; i++) {
            long_lengths[i] = (i & 1) ? ReadLongLength(eb) : ReadLongLength(ea);
        }
        Require(ea.ConsumedBytes() + eb.ConsumedBytes() == excess,
                "LZ excess lengths have the wrong length.");
    } else {
        for (u32 i = 0; i < long_count; i++) {
            long_lengths[i] = (i & 1) ? ReadLongLength(b) : ReadLongLength(a);
        }
    }
    Require(a.ConsumedBytes() + b.ConsumedBytes() == size, "LZ bitstreams have the wrong length.");

    t.lengths.resize(length_codes.size());
    u32 used = 0;
    for (size_t i = 0; i < length_codes.size(); i++) {
        u32 v = length_codes[i];
        if (v == 255) {
            Require(used < long_count, "Missing long LZ length.");
            v += long_lengths[used++];
        }
        t.lengths[i] = v + 3;
    }
    Require(used == long_count, "Unused long LZ lengths.");
}

void RunLz(u32 mode, const LzTables& t, u8* window, u8* dst, size_t dst_size, size_t position) {
    u8* out = dst + (position == 0 ? 8 : 0);
    u8* const out_end = dst + dst_size;
    const u8* lit = t.literals.data();
    const u8* const lit_end = lit + t.literals.size();
    size_t next_offset = 0;
    size_t next_length = 0;
    std::array<s32, 3> recent{-8, -8, -8};
    s32 last = -8;

    auto literals = [&](size_t n) {
        Require(n <= static_cast<size_t>(lit_end - lit) && n <= static_cast<size_t>(out_end - out),
                "LZ literals out of range.");
        if (mode == 0) {
            // Delta literals: added to the byte at the last match distance.
            Require(static_cast<size_t>(-last) <= static_cast<size_t>(out - window),
                    "LZ delta literal before the window.");
            for (size_t i = 0; i < n; i++) {
                out[i] = static_cast<u8>(lit[i] + out[static_cast<ptrdiff_t>(i) + last]);
            }
        } else {
            std::memcpy(out, lit, n);
        }
        out += n;
        lit += n;
    };

    for (const u8 c : t.commands) {
        size_t lit_len = c & 3;
        if (lit_len == 3) {
            Require(next_length < t.lengths.size(), "Missing LZ length.");
            lit_len = t.lengths[next_length++];
        }
        literals(lit_len);

        const u32 which = c >> 6;
        s32 offset;
        if (which == 3) {
            Require(next_offset < t.offsets.size(), "Missing LZ offset.");
            offset = t.offsets[next_offset++];
            recent = {offset, recent[0], recent[1]};
        } else {
            offset = recent[which];
            for (u32 k = which; k > 0; k--) {
                recent[k] = recent[k - 1];
            }
            recent[0] = offset;
        }
        last = offset;

        size_t match_len = ((c >> 2) & 0xF) + 2;
        if (match_len == 17) {
            Require(next_length < t.lengths.size(), "Missing LZ length.");
            match_len = 14 + t.lengths[next_length++];
        }
        Require(offset < 0 && static_cast<size_t>(-static_cast<s64>(offset)) <=
                                  static_cast<size_t>(out - window),
                "LZ match before the window.");
        Require(match_len <= static_cast<size_t>(out_end - out), "LZ match runs past the chunk.");
        const u8* from = out + offset;
        for (size_t i = 0; i < match_len; i++) {
            out[i] = from[i];
        }
        out += match_len;
    }
    Require(next_offset == t.offsets.size() && next_length == t.lengths.size(),
            "LZ tables left over.");
    const size_t tail = static_cast<size_t>(out_end - out);
    Require(tail == static_cast<size_t>(lit_end - lit), "LZ literal count mismatch.");
    literals(tail);
}

// ---------------------------------------------------------------------------

void DecodeChunk(u8* window, size_t position, size_t size, const u8* src, const u8* end, bool lz,
                 u32 mode) {
    Require(size > 0 && size <= kChunkSize, "Bad chunk size.");
    u8* dst = window + position;
    const size_t used = static_cast<size_t>(end - src);
    if (used == size) {
        std::memcpy(dst, src, size);
        return;
    }
    Require(used < size, "Chunk is larger than its output.");
    if (!lz) {
        Cursor in{src, end};
        Require(DecodeArray(in, dst, size) == size && in.p == end, "Chunk has the wrong size.");
        return;
    }
    LzTables tables;
    ReadLzTables(mode, src, end, dst, size, position, tables);
    RunLz(mode, tables, window, dst, size, position);
}

void DecodeQuantum(u8* window, size_t position, size_t size, const u8* src, const u8* end) {
    size_t done = 0;
    while (done < size) {
        const size_t n = std::min(kChunkSize, size - done);
        u8* dst = window + position + done;
        Require(end - src >= 4, "Truncated chunk header.");
        const u32 head = Be24(src);
        if (!(head & 0x800000)) {
            Cursor in{src, end};
            Require(DecodeArray(in, dst, n) == n, "Chunk has the wrong size.");
            src = in.p;
        } else {
            src += 3;
            const size_t used = head & 0x7FFFF;
            const u32 mode = (head >> 19) & 0xF;
            Require(used <= static_cast<size_t>(end - src), "Chunk data is truncated.");
            Require(used != n || mode == 0, "Bad stored chunk.");
            DecodeChunk(window, position + done, n, src, src + used, true, mode);
            src += used;
        }
        done += n;
    }
    Require(src == end, "Quantum has the wrong size.");
}

} // namespace

Result DecompressChunk(std::span<const u8> src, std::span<u8> window, size_t position, size_t size,
                       bool lz, u32 literal_mode) {
    Result result;
    try {
        Require(position <= window.size() && size <= window.size() - position,
                "Chunk lies outside the window.");
        DecodeChunk(window.data(), position, size, src.data(), src.data() + src.size(), lz,
                    literal_mode);
        result.ok = true;
        result.consumed = src.size();
    } catch (const Corrupt& c) {
        result.error = c.what;
    } catch (const std::bad_alloc&) {
        result.error = "Out of memory.";
    }
    return result;
}

Result Decompress(std::span<const u8> src, std::span<u8> dst) {
    Result result;
    size_t sp = 0;
    size_t pos = 0;
    try {
        while (pos < dst.size()) {
            Require(src.size() - sp >= 2, "Truncated block header.");
            const u8 b0 = src[sp];
            const u8 b1 = src[sp + 1];
            Require((b0 & 0x0F) == 0x0C && (b0 & 0x30) == 0, "Not an Oodle block header.");
            const u32 codec = b1 & 0x7F;
            if (codec != kCodecKraken) {
                Fail(codec == 10   ? "Mermaid/Selkie streams are not supported."
                     : codec == 12 ? "Leviathan streams are not supported."
                                   : "Unsupported Oodle codec.");
            }
            const bool stored = (b0 & 0x40) != 0;
            const bool checksums = (b1 & 0x80) != 0;
            sp += 2;
            const size_t n = std::min(kBlockSize, dst.size() - pos);
            if (stored) {
                Require(src.size() - sp >= n, "Stored block is truncated.");
                std::memcpy(dst.data() + pos, src.data() + sp, n);
                sp += n;
                pos += n;
                continue;
            }
            Require(src.size() - sp >= 3, "Truncated quantum header.");
            const u32 v = Be24(src.data() + sp);
            if ((v & 0x3FFFF) == 0x3FFFF) {
                Require((v >> 18) == 1 && src.size() - sp >= 4, "Bad quantum header.");
                std::memset(dst.data() + pos, src[sp + 3], n);
                sp += 4;
                pos += n;
                continue;
            }
            const size_t packed = (v & 0x3FFFF) + 1;
            sp += checksums ? 6 : 3;
            Require(sp <= src.size() && packed <= src.size() - sp, "Quantum data is truncated.");
            Require(packed <= n, "Quantum is larger than its output.");
            if (packed == n) {
                std::memcpy(dst.data() + pos, src.data() + sp, n);
            } else {
                DecodeQuantum(dst.data(), pos, n, src.data() + sp, src.data() + sp + packed);
            }
            sp += packed;
            pos += n;
        }
        result.ok = true;
    } catch (const Corrupt& c) {
        result.error = c.what;
    } catch (const std::bad_alloc&) {
        result.error = "Out of memory.";
    }
    result.consumed = sp;
    return result;
}

} // namespace Common::Kraken
