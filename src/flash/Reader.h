#pragma once
// Little-endian byte + MSB-first bit reader for SWF records.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "flash/Geom.h"

namespace flash {

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    size_t pos() const { return pos_; }
    size_t size() const { return size_; }
    size_t remaining() const { return pos_ < size_ ? size_ - pos_ : 0; }
    const uint8_t* ptr() const { return data_ + pos_; }
    bool eof() const { return pos_ >= size_; }
    void seek(size_t p) {
        pos_ = p;
        bitCount_ = 0;
    }
    void skip(size_t n) {
        align();
        pos_ += n;
    }

    uint8_t u8() {
        align();
        return pos_ < size_ ? data_[pos_++] : 0;
    }
    uint16_t u16() {
        uint16_t lo = u8();
        return (uint16_t)(lo | (u8() << 8));
    }
    uint32_t u32() {
        uint32_t lo = u16();
        return lo | ((uint32_t)u16() << 16);
    }
    int16_t s16() { return (int16_t)u16(); }
    int32_t s32() { return (int32_t)u32(); }
    double fixed8() { return s16() / 256.0; }     // 8.8
    double fixed16() { return s32() / 65536.0; }  // 16.16
    float f32() {
        uint32_t v = u32();
        float f;
        std::memcpy(&f, &v, 4);
        return f;
    }
    std::string str() {
        align();
        std::string s;
        while (pos_ < size_ && data_[pos_]) s.push_back((char)data_[pos_++]);
        pos_++;
        return s;
    }

    void align() { bitCount_ = 0; }

    uint32_t ub(int n) {
        uint32_t v = 0;
        while (n--) {
            if (bitCount_ == 0) {
                bitBuf_ = pos_ < size_ ? data_[pos_++] : 0;
                bitCount_ = 8;
            }
            bitCount_--;
            v = (v << 1) | ((bitBuf_ >> bitCount_) & 1);
        }
        return v;
    }
    int32_t sb(int n) {
        if (n == 0) return 0;
        uint32_t v = ub(n);
        if (v & (1u << (n - 1))) v |= ~0u << n;
        return (int32_t)v;
    }
    double fb(int n) { return sb(n) / 65536.0; }
    bool flag() { return ub(1) != 0; }

    Rect rect() {
        align();
        int n = ub(5);
        Rect r;
        r.xMin = sb(n);
        r.xMax = sb(n);
        r.yMin = sb(n);
        r.yMax = sb(n);
        align();
        return r;
    }

    Matrix matrix() {
        align();
        Matrix m;
        if (flag()) {
            int n = ub(5);
            m.a = fb(n);
            m.d = fb(n);
        }
        if (flag()) {
            int n = ub(5);
            m.b = fb(n);
            m.c = fb(n);
        }
        int n = ub(5);
        m.tx = sb(n);
        m.ty = sb(n);
        align();
        return m;
    }

    ColorTransform cxform(bool withAlpha) {
        align();
        ColorTransform cx;
        bool hasAdd = flag();
        bool hasMult = flag();
        int n = ub(4);
        if (hasMult) {
            cx.rMul = sb(n) / 256.f;
            cx.gMul = sb(n) / 256.f;
            cx.bMul = sb(n) / 256.f;
            if (withAlpha) cx.aMul = sb(n) / 256.f;
        }
        if (hasAdd) {
            cx.rAdd = (float)sb(n);
            cx.gAdd = (float)sb(n);
            cx.bAdd = (float)sb(n);
            if (withAlpha) cx.aAdd = (float)sb(n);
        }
        align();
        return cx;
    }

    Color rgb() {
        Color c;
        c.r = u8();
        c.g = u8();
        c.b = u8();
        return c;
    }
    Color rgba() {
        Color c = rgb();
        c.a = u8();
        return c;
    }

private:
    const uint8_t* data_;
    size_t size_;
    size_t pos_ = 0;
    uint8_t bitBuf_ = 0;
    int bitCount_ = 0;
};

}  // namespace flash
