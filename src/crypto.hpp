#pragma once
// ---------------------------------------------------------------------------
// Minimal crypto primitives for the vanilla logon protocol: SHA-1 and an
// arbitrary-precision unsigned integer with modular exponentiation. Both are
// dependency-free and unit-tested (SHA-1 against the standard vectors, BigUInt
// modpow against Python's pow for the real WoW safe prime).
//
// NOT for production security use -- this exists to speak the 1.12.1 protocol.
// ---------------------------------------------------------------------------
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace wf {

// ---- SHA-1 ----
std::array<uint8_t, 20> sha1(const uint8_t* data, size_t len);
inline std::array<uint8_t, 20> sha1(const std::vector<uint8_t>& v) { return sha1(v.data(), v.size()); }

// ---- big unsigned integer (base 2^32, little-endian limbs) ----
class BigUInt {
public:
    BigUInt() = default;
    explicit BigUInt(uint32_t v) { if (v) limbs_.push_back(v); }

    static BigUInt fromBytesBE(const uint8_t* b, size_t n);
    static BigUInt fromBytesLE(const uint8_t* b, size_t n);
    static BigUInt fromBytesBE(const std::vector<uint8_t>& v) { return fromBytesBE(v.data(), v.size()); }
    static BigUInt fromBytesLE(const std::vector<uint8_t>& v) { return fromBytesLE(v.data(), v.size()); }
    static BigUInt fromHexBE(const std::string& hex);

    std::vector<uint8_t> toBytesLE(size_t width = 0) const;  // 0 = minimal
    std::vector<uint8_t> toBytesBE(size_t width = 0) const;

    bool isZero() const { return limbs_.empty(); }

    BigUInt operator+(const BigUInt& o) const;
    BigUInt operator-(const BigUInt& o) const;   // assumes *this >= o
    BigUInt operator*(const BigUInt& o) const;
    BigUInt operator%(const BigUInt& m) const;

    static int  cmp(const BigUInt& a, const BigUInt& b);
    bool operator<(const BigUInt& o)  const { return cmp(*this, o) < 0; }
    bool operator>=(const BigUInt& o) const { return cmp(*this, o) >= 0; }
    bool operator==(const BigUInt& o) const { return cmp(*this, o) == 0; }

    // (base ^ exp) mod m, square-and-multiply.
    static BigUInt modpow(BigUInt base, BigUInt exp, const BigUInt& m);

private:
    std::vector<uint32_t> limbs_;   // little-endian, normalised (no trailing zeros)
    void trim();
    bool bit(size_t i) const;
    size_t bitLength() const;
};

} // namespace wf
