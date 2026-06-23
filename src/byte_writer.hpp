#pragma once
// ---------------------------------------------------------------------------
// ByteWriter: the little-endian write counterpart to ByteReader. Appends
// values byte-by-byte to a growable buffer so encoders stay portable and free
// of struct-packing assumptions, exactly like the decode path.
// ---------------------------------------------------------------------------
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wf {

class ByteWriter {
public:
    ByteWriter() = default;

    void u8(uint8_t v)  { buf_.push_back(v); }
    void u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    }
    void u32(uint32_t v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void u64(uint64_t v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
    void f32(float f) {
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        u32(bits);
    }
    void bytes(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::vector<uint8_t>        take()       { return std::move(buf_); }
    size_t size() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
};

} // namespace wf
