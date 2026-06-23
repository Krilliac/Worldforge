#include "crypto.hpp"
#include <stdexcept>

namespace wf {

// ======================= SHA-1 =======================
namespace {
inline uint32_t rol(uint32_t v, int c) { return (v << c) | (v >> (32 - c)); }
}

std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE,
             h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // Pre-processing: append 0x80, pad to 56 mod 64, then 64-bit big-endian length.
    std::vector<uint8_t> msg(data, data + len);
    uint64_t bitLen = static_cast<uint64_t>(len) * 8;
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0x00);
    for (int i = 7; i >= 0; --i) msg.push_back(static_cast<uint8_t>((bitLen >> (i * 8)) & 0xFF));

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; ++i)
            w[i] = (msg[off+i*4] << 24) | (msg[off+i*4+1] << 16) |
                   (msg[off+i*4+2] << 8) | msg[off+i*4+3];
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);            k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                     k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);   k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                     k = 0xCA62C1D6; }
            uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<uint8_t, 20> out;
    uint32_t hs[5] = { h0, h1, h2, h3, h4 };
    for (int i = 0; i < 5; ++i) {
        out[i*4+0] = (hs[i] >> 24) & 0xFF; out[i*4+1] = (hs[i] >> 16) & 0xFF;
        out[i*4+2] = (hs[i] >> 8)  & 0xFF; out[i*4+3] =  hs[i]        & 0xFF;
    }
    return out;
}

// ======================= BigUInt =======================
void BigUInt::trim() { while (!limbs_.empty() && limbs_.back() == 0) limbs_.pop_back(); }

bool BigUInt::bit(size_t i) const {
    size_t limb = i / 32, off = i % 32;
    return limb < limbs_.size() && ((limbs_[limb] >> off) & 1);
}

size_t BigUInt::bitLength() const {
    if (limbs_.empty()) return 0;
    size_t hi = limbs_.size() - 1;
    uint32_t top = limbs_[hi];
    size_t bits = 0;
    while (top) { ++bits; top >>= 1; }
    return hi * 32 + bits;
}

BigUInt BigUInt::fromBytesBE(const uint8_t* b, size_t n) {
    BigUInt r;
    r.limbs_.assign((n + 3) / 4, 0);
    // b[0] is most significant.
    for (size_t i = 0; i < n; ++i) {
        size_t bytePos = n - 1 - i;             // little-endian byte index
        r.limbs_[bytePos / 4] |= static_cast<uint32_t>(b[i]) << ((bytePos % 4) * 8);
    }
    r.trim();
    return r;
}

BigUInt BigUInt::fromBytesLE(const uint8_t* b, size_t n) {
    BigUInt r;
    r.limbs_.assign((n + 3) / 4, 0);
    for (size_t i = 0; i < n; ++i)
        r.limbs_[i / 4] |= static_cast<uint32_t>(b[i]) << ((i % 4) * 8);
    r.trim();
    return r;
}

BigUInt BigUInt::fromHexBE(const std::string& hex) {
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("bad hex");
    };
    std::string h = hex;
    if (h.size() % 2) h = "0" + h;
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < h.size(); i += 2)
        bytes.push_back(static_cast<uint8_t>((nib(h[i]) << 4) | nib(h[i+1])));
    return fromBytesBE(bytes.data(), bytes.size());
}

std::vector<uint8_t> BigUInt::toBytesLE(size_t width) const {
    std::vector<uint8_t> out;
    for (uint32_t limb : limbs_)
        for (int i = 0; i < 4; ++i) out.push_back((limb >> (i * 8)) & 0xFF);
    while (!out.empty() && out.back() == 0) out.pop_back();
    if (width) { if (out.size() > width) out.resize(width); else out.resize(width, 0); }
    return out;
}

std::vector<uint8_t> BigUInt::toBytesBE(size_t width) const {
    std::vector<uint8_t> le = toBytesLE(width ? width : 0);
    return std::vector<uint8_t>(le.rbegin(), le.rend());
}

int BigUInt::cmp(const BigUInt& a, const BigUInt& b) {
    if (a.limbs_.size() != b.limbs_.size())
        return a.limbs_.size() < b.limbs_.size() ? -1 : 1;
    for (size_t i = a.limbs_.size(); i-- > 0; )
        if (a.limbs_[i] != b.limbs_[i]) return a.limbs_[i] < b.limbs_[i] ? -1 : 1;
    return 0;
}

BigUInt BigUInt::operator+(const BigUInt& o) const {
    BigUInt r;
    size_t n = std::max(limbs_.size(), o.limbs_.size());
    uint64_t carry = 0;
    for (size_t i = 0; i < n || carry; ++i) {
        uint64_t sum = carry;
        if (i < limbs_.size())   sum += limbs_[i];
        if (i < o.limbs_.size()) sum += o.limbs_[i];
        r.limbs_.push_back(static_cast<uint32_t>(sum & 0xFFFFFFFF));
        carry = sum >> 32;
    }
    r.trim();
    return r;
}

BigUInt BigUInt::operator-(const BigUInt& o) const {
    BigUInt r;
    int64_t borrow = 0;
    for (size_t i = 0; i < limbs_.size(); ++i) {
        int64_t diff = static_cast<int64_t>(limbs_[i]) - borrow -
                       (i < o.limbs_.size() ? o.limbs_[i] : 0);
        if (diff < 0) { diff += (int64_t(1) << 32); borrow = 1; } else borrow = 0;
        r.limbs_.push_back(static_cast<uint32_t>(diff));
    }
    r.trim();
    return r;
}

BigUInt BigUInt::operator*(const BigUInt& o) const {
    if (isZero() || o.isZero()) return BigUInt();
    BigUInt r;
    r.limbs_.assign(limbs_.size() + o.limbs_.size(), 0);
    for (size_t i = 0; i < limbs_.size(); ++i) {
        uint64_t carry = 0;
        for (size_t j = 0; j < o.limbs_.size() || carry; ++j) {
            uint64_t cur = r.limbs_[i + j] + carry;
            if (j < o.limbs_.size())
                cur += static_cast<uint64_t>(limbs_[i]) * o.limbs_[j];
            r.limbs_[i + j] = static_cast<uint32_t>(cur & 0xFFFFFFFF);
            carry = cur >> 32;
        }
    }
    r.trim();
    return r;
}

// Remainder via binary long division (shift-subtract). Correct, not the fastest.
BigUInt BigUInt::operator%(const BigUInt& m) const {
    if (m.isZero()) throw std::runtime_error("mod by zero");
    if (cmp(*this, m) < 0) return *this;
    BigUInt rem;
    for (size_t i = bitLength(); i-- > 0; ) {
        // rem <<= 1
        uint32_t carry = 0;
        for (size_t k = 0; k < rem.limbs_.size(); ++k) {
            uint32_t nc = rem.limbs_[k] >> 31;
            rem.limbs_[k] = (rem.limbs_[k] << 1) | carry;
            carry = nc;
        }
        if (carry) rem.limbs_.push_back(carry);
        // rem |= bit i
        if (bit(i)) {
            if (rem.limbs_.empty()) rem.limbs_.push_back(1);
            else rem.limbs_[0] |= 1;
        }
        if (cmp(rem, m) >= 0) rem = rem - m;
    }
    rem.trim();
    return rem;
}

BigUInt BigUInt::modpow(BigUInt base, BigUInt exp, const BigUInt& m) {
    BigUInt result(1);
    base = base % m;
    size_t bits = exp.bitLength();
    for (size_t i = 0; i < bits; ++i) {
        if (exp.bit(i)) result = (result * base) % m;
        base = (base * base) % m;
    }
    return result;
}

} // namespace wf
