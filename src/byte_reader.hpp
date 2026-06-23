#pragma once
// ---------------------------------------------------------------------------
// ByteReader: a bounds-checked, little-endian cursor over an immutable span.
//
// All multi-byte values in WoW client files are little-endian. We decode them
// byte-by-byte rather than reinterpret_cast'ing structs, which makes the code:
//   * portable to big-endian hosts,
//   * immune to struct padding/alignment differences between MSVC and GCC,
//   * safe against unaligned reads (UB on some targets).
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace wf {

class ByteReader {
public:
    ByteReader(const uint8_t* data, size_t size) noexcept
        : data_(data), size_(size), pos_(0) {}

    explicit ByteReader(const std::vector<uint8_t>& v) noexcept
        : data_(v.data()), size_(v.size()), pos_(0) {}

    size_t pos()       const noexcept { return pos_; }
    size_t size()      const noexcept { return size_; }
    size_t remaining() const noexcept { return size_ - pos_; }
    bool   eof()       const noexcept { return pos_ >= size_; }
    const uint8_t* ptr() const noexcept { return data_ + pos_; }

    void seek(size_t p) {
        if (p > size_) throw std::out_of_range("ByteReader::seek past end");
        pos_ = p;
    }
    void skip(size_t n) {
        require(n);
        pos_ += n;
    }

    uint8_t u8() {
        require(1);
        return data_[pos_++];
    }
    uint16_t u16() {
        require(2);
        uint16_t v = static_cast<uint16_t>(data_[pos_]) |
                     static_cast<uint16_t>(static_cast<uint16_t>(data_[pos_ + 1]) << 8);
        pos_ += 2;
        return v;
    }
    uint32_t u32() {
        require(4);
        uint32_t v = static_cast<uint32_t>(data_[pos_])             |
                     (static_cast<uint32_t>(data_[pos_ + 1]) << 8)  |
                     (static_cast<uint32_t>(data_[pos_ + 2]) << 16) |
                     (static_cast<uint32_t>(data_[pos_ + 3]) << 24);
        pos_ += 4;
        return v;
    }
    int32_t i32() { return static_cast<int32_t>(u32()); }

    uint64_t u64() {
        require(8);
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<uint64_t>(data_[pos_ + i]) << (8 * i);
        pos_ += 8;
        return v;
    }

    float f32() {
        uint32_t bits = u32();
        float f;
        std::memcpy(&f, &bits, sizeof(f)); // well-defined type-pun
        return f;
    }

    // Read a 4-byte chunk magic and return it human-readable.
    // On disk WoW stores chunk FourCCs as a little-endian uint32, so the bytes
    // 'R','E','V','M' represent the chunk "MVER". We reverse to read normally.
    std::string fourccReversed() {
        require(4);
        const char c[4] = {
            static_cast<char>(data_[pos_ + 3]),
            static_cast<char>(data_[pos_ + 2]),
            static_cast<char>(data_[pos_ + 1]),
            static_cast<char>(data_[pos_ + 0]),
        };
        pos_ += 4;
        return std::string(c, 4);
    }

    // Read 4 raw bytes in file order (e.g. DBC magic "WDBC" is NOT reversed).
    std::string fourccRaw() {
        require(4);
        std::string s(reinterpret_cast<const char*>(data_ + pos_), 4);
        pos_ += 4;
        return s;
    }

private:
    void require(size_t n) const {
        if (pos_ + n > size_) throw std::out_of_range("ByteReader: read past end");
    }
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_;
};

} // namespace wf
