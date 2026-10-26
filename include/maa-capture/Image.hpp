#pragma once

// ============================================================================
// 最小图像模块（替代 OpenCV 依赖）
//
// 从 OpenCV 源码提取本项目实际用到的子集语义：
//   - 图像缓冲：分配/外部包装（不拥有）/ROI 视图（共享引用计数，生命周期安全）/
//     clone 深拷贝 —— 对应 cv::Mat 的 Mat(rows,cols,type) / Mat(rows,cols,type,data,step) /
//     mat(rect) / clone()
//   - 颜色转换：BGRA<->BGR（对应 cv::cvtColor COLOR_BGRA2BGR / COLOR_BGR2BGRA，
//     BGR 与 BGRA 通道顺序一致，转换仅去/补 alpha；alpha 补 255）
//   - Rect 相交：完整复刻 OpenCV core/types.hpp 的 operator &（含防溢出处理）
//   - 像素类型：Pixel4/Pixel3（对应 cv::Vec4b / Vec3b）
//
// 参考：
//   - https://github.com/opencv/opencv  modules/core/include/opencv2/core/types.hpp
//   - https://github.com/opencv/opencv  modules/imgproc/src/color_rgb.*
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <memory>
#include <limits>
#include <stdexcept>

#include "Common.h"

MWS_NS_BEGIN

// ---------------------------------------------------------------------------
// 像素类型（对应 cv::Vec4b / cv::Vec3b —— BGRA / BGR 各通道字节）
// ---------------------------------------------------------------------------

struct Pixel4
{
    std::uint8_t b;
    std::uint8_t g;
    std::uint8_t r;
    std::uint8_t a;

    std::uint8_t& operator[](std::size_t i)
    {
        return (&b)[i];
    }
    std::uint8_t operator[](std::size_t i) const
    {
        return (&b)[i];
    }
};

struct Pixel3
{
    std::uint8_t b;
    std::uint8_t g;
    std::uint8_t r;

    std::uint8_t& operator[](std::size_t i)
    {
        return (&b)[i];
    }
    std::uint8_t operator[](std::size_t i) const
    {
        return (&b)[i];
    }
};

// ---------------------------------------------------------------------------
// 标量（对应 cv::Scalar；setTo 等填充用，值域 0..255）
// ---------------------------------------------------------------------------

struct Scalar
{
    std::uint8_t v[4] = { 0, 0, 0, 255 };

    Scalar() = default;
    Scalar(std::uint8_t b, std::uint8_t g, std::uint8_t r)
        : v { b, g, r, 255 }
    {
    }

    std::uint8_t& operator[](std::size_t i)
    {
        return v[i];
    }
    std::uint8_t operator[](std::size_t i) const
    {
        return v[i];
    }
};

// ---------------------------------------------------------------------------
// 矩形（对应 cv::Rect；operator& 相交公式完整复刻 OpenCV core/types.hpp）
// ---------------------------------------------------------------------------

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    Rect() = default;
    Rect(int x_, int y_, int w_, int h_)
        : x(x_)
        , y(y_)
        , width(w_)
        , height(h_)
    {
    }

    bool empty() const
    {
        return width <= 0 || height <= 0;
    }

    // 与 OpenCV operator &= 等价（含负数原点防溢出处理）
    Rect& operator&=(const Rect& b)
    {
        if (empty() || b.empty()) {
            *this = Rect();
            return *this;
        }

        const Rect& rx_min = (x < b.x) ? *this : b;
        const Rect& rx_max = (x < b.x) ? b : *this;
        const Rect& ry_min = (y < b.y) ? *this : b;
        const Rect& ry_max = (y < b.y) ? b : *this;

        // 防止 (rx_max.x - rx_min.x) 溢出（仅当 rx_min.x < 0 时可能）
        if ((rx_min.x < 0 && rx_min.x + rx_min.width < rx_max.x) || (ry_min.y < 0 && ry_min.y + ry_min.height < ry_max.y)) {
            *this = Rect();
            return *this;
        }

        width = std::min(rx_min.width - (rx_max.x - rx_min.x), rx_max.width);
        height = std::min(ry_min.height - (ry_max.y - ry_min.y), ry_max.height);
        x = rx_max.x;
        y = ry_max.y;

        if (empty()) {
            *this = Rect();
        }
        return *this;
    }
};

inline Rect operator&(const Rect& a, const Rect& b)
{
    Rect c = a;
    return c &= b;
}

// ---------------------------------------------------------------------------
// 图像（对应本项目用到的 cv::Mat 子集）
// ---------------------------------------------------------------------------

class Image
{
public:
    int rows = 0;
    int cols = 0;
    std::size_t step = 0; // 行字节数（0 = 空图像）
    std::uint8_t* data = nullptr;

    Image() = default;

    // 深拷贝语义与 cv::Mat 拷贝构造一致（共享底层缓冲）
    Image(const Image&) = default;
    Image& operator=(const Image&) = default;
    Image(Image&&) noexcept = default;
    Image& operator=(Image&&) noexcept = default;

    // 分配新图像（对应 cv::Mat(rows, cols, type)），像素未初始化
    Image(int rows_, int cols_, int channels)
        : rows(rows_)
        , cols(cols_)
        , step(static_cast<std::size_t>(cols_) * static_cast<std::size_t>(channels))
        , channels_(channels)
    {
        if (rows_ <= 0 || cols_ <= 0 || channels <= 0) {
            rows = cols = 0;
            step = 0;
            channels_ = 0;
            return;
        }
        if (static_cast<std::size_t>(cols_) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(channels)
            || step > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(rows_))
            throw std::overflow_error("Image dimensions overflow address space");
        const std::size_t total = step * static_cast<std::size_t>(rows_);
        auto buffer = std::make_shared<Buffer>();
        buffer->owned = std::make_shared<std::uint8_t[]>(total);
        buffer->base = buffer->owned.get();
        buffer->size = total;
        // 共享缓冲自身先由 data_ 持有
        buffer_ = std::move(buffer);
        data = buffer_->base;
    }

    // 包装外部数据（对应 cv::Mat(rows, cols, type, data, step)），不拥有内存
    static Image wrap(int rows_, int cols_, int channels, void* external, std::size_t external_step)
    {
        Image img;
        if (!external || rows_ <= 0 || cols_ <= 0 || channels <= 0) {
            return img;
        }
        img.rows = rows_;
        img.cols = cols_;
        img.channels_ = channels;
        img.step = external_step > 0 ? external_step : static_cast<std::size_t>(cols_) * channels;
        if (static_cast<std::size_t>(cols_) > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(channels)
            || img.step < static_cast<std::size_t>(cols_) * channels
            || img.step > std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(rows_))
            return Image();
        auto buffer = std::make_shared<Buffer>();
        buffer->base = static_cast<std::uint8_t*>(external);
        buffer->size = img.step * static_cast<std::size_t>(rows_);
        buffer->owned = nullptr; // 外部内存，不释放
        img.buffer_ = std::move(buffer);
        img.data = img.buffer_->base;
        return img;
    }

    int channels() const
    {
        return channels_;
    }

    bool empty() const
    {
        return !data || rows == 0 || cols == 0;
    }

    // 总元素数（rows*cols），对应 OpenCV Mat::total()
    std::size_t total() const
    {
        return static_cast<std::size_t>(rows) * static_cast<std::size_t>(cols);
    }

    // ROI 视图（对应 cv::Mat mat(rect)）：共享底层缓冲，拷贝/生命周期安全
    Image roi(int x, int y, int w, int h) const
    {
        if (empty() || w <= 0 || h <= 0 || x < 0 || y < 0 || x > cols || y > rows || w > cols - x || h > rows - y) {
            return Image();
        }
        Image view;
        view.rows = h;
        view.cols = w;
        view.channels_ = channels_;
        view.step = step;
        view.buffer_ = buffer_; // 共享引用计数
        view.data = data + static_cast<std::size_t>(y) * step + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels_);
        return view;
    }

    // ROI 视图（Rect 版本，对应 cv::Mat operator()(cv::Rect)）
    Image roi(const Rect& r) const
    {
        return roi(r.x, r.y, r.width, r.height);
    }

    // 深拷贝（对应 Mat::clone()）
    Image clone() const
    {
        if (empty()) {
            return Image();
        }
        Image dst(rows, cols, channels_);
        copy_pixels(*this, dst);
        return dst;
    }

    // 逐像素设置（对应 Mat::setTo(Scalar)）
    void set_to(const Scalar& value)
    {
        if (empty()) {
            return;
        }
        const std::size_t pixels_per_row = static_cast<std::size_t>(cols) * channels_;
        for (int y = 0; y < rows; ++y) {
            std::uint8_t* row = data + static_cast<std::size_t>(y) * step;
            for (std::size_t i = 0; i < pixels_per_row; ++i) {
                row[i] = value[i % channels_];
            }
        }
    }

    // 把本图像（区域）拷贝到 dst（尺寸相同；对应 Mat::copyTo 的 ROI 用法）
    void copy_to(Image& dst) const
    {
        if (empty() || dst.empty()) {
            return;
        }
        const int h = std::min(rows, dst.rows);
        const int w = std::min(cols, dst.cols);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* s = data + static_cast<std::size_t>(y) * step;
            std::uint8_t* d = dst.data + static_cast<std::size_t>(y) * dst.step;
            std::memcpy(d, s, static_cast<std::size_t>(w) * channels_);
        }
    }

    // ---- 迭代器/元素访问（对应 Mat::begin<T>()/end<T>()/at<T>()）----

    template <typename T>
    T* begin()
    {
        return reinterpret_cast<T*>(data);
    }

    template <typename T>
    T* end()
    {
        // 与 OpenCV 一致：datastart + total()
        return reinterpret_cast<T*>(data) + total();
    }

    template <typename T>
    const T* begin() const
    {
        return reinterpret_cast<const T*>(data);
    }

    template <typename T>
    const T* end() const
    {
        return reinterpret_cast<const T*>(data) + total();
    }

    template <typename T>
    T& at(int y, int x)
    {
        return *reinterpret_cast<T*>(data + static_cast<std::size_t>(y) * step + static_cast<std::size_t>(x) * sizeof(T));
    }

    template <typename T>
    const T& at(int y, int x) const
    {
        return *reinterpret_cast<const T*>(data + static_cast<std::size_t>(y) * step + static_cast<std::size_t>(x) * sizeof(T));
    }

private:
    struct Buffer
    {
        std::shared_ptr<std::uint8_t[]> owned; // 非空 = 自有内存
        std::uint8_t* base = nullptr;          // 数据起始（自有时为 owned.get()）
        std::size_t size = 0;
    };

    static void copy_pixels(const Image& src, Image& dst)
    {
        const int h = std::min(src.rows, dst.rows);
        const int w = std::min(src.cols, dst.cols);
        for (int y = 0; y < h; ++y) {
            const std::uint8_t* s = src.data + static_cast<std::size_t>(y) * src.step;
            std::uint8_t* d = dst.data + static_cast<std::size_t>(y) * dst.step;
            std::memcpy(d, s, static_cast<std::size_t>(w) * src.channels_);
        }
    }

    std::shared_ptr<Buffer> buffer_; // 决定底层缓冲生命周期（ROI 视图共享）
    int channels_ = 0;
};

// ---------------------------------------------------------------------------
// 颜色转换（对应 cv::cvtColor COLOR_BGRA2BGR / COLOR_BGR2BGRA）
//
// BGRA 与 BGR 通道顺序一致（B,G,R[,A]），转换只涉及 alpha：
//   BGRA->BGR：丢弃 alpha
//   BGR->BGRA：补 alpha = 255（与 OpenCV 一致）
// ---------------------------------------------------------------------------

inline void cvt_bgra_to_bgr(const Image& src, Image& dst)
{
    if (src.empty() || src.channels() != 4) {
        dst = Image();
        return;
    }
    dst = Image(src.rows, src.cols, 3);
    for (int y = 0; y < src.rows; ++y) {
        const Pixel4* s = reinterpret_cast<const Pixel4*>(src.data + static_cast<std::size_t>(y) * src.step);
        Pixel3* d = reinterpret_cast<Pixel3*>(dst.data + static_cast<std::size_t>(y) * dst.step);
        for (int x = 0; x < src.cols; ++x) {
            d[x].b = s[x].b;
            d[x].g = s[x].g;
            d[x].r = s[x].r;
        }
    }
}

inline void cvt_bgr_to_bgra(const Image& src, Image& dst)
{
    if (src.empty() || src.channels() != 3) {
        dst = Image();
        return;
    }
    dst = Image(src.rows, src.cols, 4);
    for (int y = 0; y < src.rows; ++y) {
        const Pixel3* s = reinterpret_cast<const Pixel3*>(src.data + static_cast<std::size_t>(y) * src.step);
        Pixel4* d = reinterpret_cast<Pixel4*>(dst.data + static_cast<std::size_t>(y) * dst.step);
        for (int x = 0; x < src.cols; ++x) {
            d[x].b = s[x].b;
            d[x].g = s[x].g;
            d[x].r = s[x].r;
            d[x].a = 255; // 与 OpenCV BGR->BGRA 一致
        }
    }
}

// 便捷：直接返回新图像（对应原 bgra_to_bgr 用法）
inline Image bgra_to_bgr(const Image& src)
{
    Image dst;
    cvt_bgra_to_bgr(src, dst);
    return dst;
}

// ---------------------------------------------------------------------------
// 颜色转换（对应 cv::cvtColor COLOR_RGBA2BGR / COLOR_RGB2BGR）
//
// RGBA/RGB 与 BGRA/BGR 通道顺序相反，转换交换 R/B：
//   RGBA->BGR：交换 R/B 并丢弃 alpha
//   RGB->BGR ：交换 R/B
// ---------------------------------------------------------------------------

inline void cvt_rgba_to_bgr(const Image& src, Image& dst)
{
    if (src.empty() || src.channels() != 4) {
        dst = Image();
        return;
    }
    dst = Image(src.rows, src.cols, 3);
    for (int y = 0; y < src.rows; ++y) {
        const Pixel4* s = reinterpret_cast<const Pixel4*>(src.data + static_cast<std::size_t>(y) * src.step);
        Pixel3* d = reinterpret_cast<Pixel3*>(dst.data + static_cast<std::size_t>(y) * dst.step);
        for (int x = 0; x < src.cols; ++x) {
            d[x].b = s[x].r;
            d[x].g = s[x].g;
            d[x].r = s[x].b;
        }
    }
}

inline void cvt_rgb_to_bgr(const Image& src, Image& dst)
{
    if (src.empty() || src.channels() != 3) {
        dst = Image();
        return;
    }
    dst = Image(src.rows, src.cols, 3);
    for (int y = 0; y < src.rows; ++y) {
        const Pixel3* s = reinterpret_cast<const Pixel3*>(src.data + static_cast<std::size_t>(y) * src.step);
        Pixel3* d = reinterpret_cast<Pixel3*>(dst.data + static_cast<std::size_t>(y) * dst.step);
        for (int x = 0; x < src.cols; ++x) {
            d[x].b = s[x].r;
            d[x].g = s[x].g;
            d[x].r = s[x].b;
        }
    }
}

// ---------------------------------------------------------------------------
// 颜色转换（对应 cv::cvtColor COLOR_YUV2BGR_YUY2）
//
// YUY2（YUYV）打包格式：每 4 字节 [Y0, Cb, Y1, Cr] 表示 2 个像素。
// 与 OpenCV 相同采用 BT.601 全范围（JPEG）系数：
//   B = Y + 1.773 * (Cb - 128)
//   G = Y - 0.344 * (Cb - 128) - 0.714 * (Cr - 128)
//   R = Y + 1.403 * (Cr - 128)
// 定点实现（1 << 12），与 OpenCV 结果可能有 ±1 的舍入差异。
// 源图像用 2 通道包装（每像素 2 字节）。
// ---------------------------------------------------------------------------

inline void cvt_yuy2_to_bgr(const Image& src, Image& dst)
{
    if (src.empty() || src.channels() != 2) {
        dst = Image();
        return;
    }
    dst = Image(src.rows, src.cols, 3);

    constexpr int kShift = 12;
    constexpr int kCbCoeffB = static_cast<int>(1.773 * (1 << kShift) + 0.5);
    constexpr int kCbCoeffG = static_cast<int>(0.344 * (1 << kShift) + 0.5);
    constexpr int kCrCoeffG = static_cast<int>(0.714 * (1 << kShift) + 0.5);
    constexpr int kCrCoeffR = static_cast<int>(1.403 * (1 << kShift) + 0.5);

    const auto clamp8 = [](int v) -> std::uint8_t {
        return static_cast<std::uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : v));
    };

    for (int y = 0; y < src.rows; ++y) {
        const std::uint8_t* s = src.data + static_cast<std::size_t>(y) * src.step;
        std::uint8_t* d = dst.data + static_cast<std::size_t>(y) * dst.step;
        for (int x = 0; x < src.cols; x += 2) {
            // [Y0, Cb, Y1, Cr]；奇数宽度的末列没有配对像素，色度/亮度沿用本组
            const int y0 = s[0];
            const int cb = s[1] - 128;
            const bool has_pair = x + 1 < src.cols;
            const int y1 = has_pair ? s[2] : y0;
            const int cr = (has_pair ? s[3] : s[1]) - 128;

            d[0] = clamp8(y0 + ((kCbCoeffB * cb) >> kShift));
            d[1] = clamp8(y0 - ((kCbCoeffG * cb + kCrCoeffG * cr) >> kShift));
            d[2] = clamp8(y0 + ((kCrCoeffR * cr) >> kShift));

            if (has_pair) {
                d[3] = clamp8(y1 + ((kCbCoeffB * cb) >> kShift));
                d[4] = clamp8(y1 - ((kCbCoeffG * cb + kCrCoeffG * cr) >> kShift));
                d[5] = clamp8(y1 + ((kCrCoeffR * cr) >> kShift));
            }

            s += 4;
            d += 6;
        }
    }
}

// ---------------------------------------------------------------------------
// 颜色转换码与统一分发（对应 cv::cvtColor 的 code 用法，
// 供 PipeWire / Wlr 等单元按协商/缓冲格式查表转换）
// ---------------------------------------------------------------------------

enum ColorConversionCode
{
    COLOR_NONE = -1, // 源已是 BGR，无需转换
    COLOR_BGRA2BGR = 0,
    COLOR_RGBA2BGR,
    COLOR_RGB2BGR,
    COLOR_YUV2BGR_YUY2,
    COLOR_BGR2BGRA,
};

inline void cvt_color(const Image& src, Image& dst, ColorConversionCode code)
{
    switch (code) {
    case COLOR_BGRA2BGR:
        cvt_bgra_to_bgr(src, dst);
        break;
    case COLOR_RGBA2BGR:
        cvt_rgba_to_bgr(src, dst);
        break;
    case COLOR_RGB2BGR:
        cvt_rgb_to_bgr(src, dst);
        break;
    case COLOR_YUV2BGR_YUY2:
        cvt_yuy2_to_bgr(src, dst);
        break;
    case COLOR_BGR2BGRA:
        cvt_bgr_to_bgra(src, dst);
        break;
    case COLOR_NONE:
    default:
        dst = src;
        break;
    }
}

MWS_NS_END
