// ============================================================================
// 示例：通过「外部回调接口」实现截图（对应 MaaCustomControllerCallbacks::screencap）
//
// 演示：
//   1. 以 C 结构体 MaaWSCustomScreencapCallbacks 定义外部回调
//      （语义与 MaaFramework 自定义控制器的 screencap 回调一致：回调把图像写入 out buffer）
//   2. 经 C API 创建句柄触发截图
//   3. 演示把回调桥接为 ScreencapBase 单元（CustomScreencapBridge），
//      注册进 ScreencapMgr 参与测速 —— 两个外部接口可以组合使用
// ============================================================================

#include <iostream>
#include <memory>

#include "maa-capture/CustomScreencap.h"
#include "maa-capture/Image.hpp"
#include "maa-capture/ScreencapMgr.h"

using namespace mws;

// ---- 回调上下文：模拟"外部宿主"持有一张图像 ----
struct CaptureContext
{
    Image image; // 由"外部"产生的截图（BGRA）
};

// ---- 外部截图回调（等价 MaaCustomControllerCallbacks.screencap）----
static MaaWSBool on_screencap(void* trans_arg, MaaWSImageBuffer* out)
{
    auto* ctx = static_cast<CaptureContext*>(trans_arg);
    if (!ctx || !out || ctx->image.empty()) {
        return 0;
    }

    // 把持有的图像转为 BGRA 连续存储并写入 out
    static Image s_storage; // 演示用：指向本对象持有的长期数据（C 回调无对象成员）
    if (ctx->image.channels() == 4) {
        s_storage = ctx->image;
    }
    else {
        cvt_bgr_to_bgra(ctx->image, s_storage);
    }

    out->data = s_storage.data;
    out->width = s_storage.cols;
    out->height = s_storage.rows;
    out->channels = 4;
    out->step = static_cast<int32_t>(s_storage.step);
    return 1;
}

static MaaWSBool on_inactive(void*)
{
    std::cout << "[callback] inactive called" << std::endl;
    return 1;
}

int main()
{
    // 构造一张模拟的外部截图（800x600 蓝绿色渐变）
    CaptureContext ctx;
    ctx.image = Image(600, 800, 4);
    for (int y = 0; y < ctx.image.rows; ++y) {
        for (int x = 0; x < ctx.image.cols; ++x) {
            Pixel4& p = ctx.image.at<Pixel4>(y, x);
            p[0] = static_cast<unsigned char>(x * 255 / ctx.image.cols); // B
            p[1] = static_cast<unsigned char>(y * 255 / ctx.image.rows); // G
            p[2] = 128;                                                  // R
            p[3] = 255;                                                  // A
        }
    }

    // 1) 经 C API 直接使用
    {
        MaaWSCustomScreencapCallbacks callbacks = { };
        callbacks.screencap = on_screencap;
        callbacks.inactive = on_inactive;

        MaaWSCustomScreencapHandle handle = MaaWSCustomScreencapCreate(&callbacks, &ctx);
        if (!handle) {
            std::cerr << "create failed" << std::endl;
            return 1;
        }

        MaaWSImageBuffer out = { };
        if (MaaWSCustomScreencapScreencap(handle, &out)) {
            std::cout << "[C API] screencap ok: " << out.width << "x" << out.height << " ch=" << out.channels << std::endl;
        }
        else {
            std::cerr << "[C API] screencap failed" << std::endl;
        }

        MaaWSCustomScreencapDestroy(handle);
    }

    // 2) 桥接进 ScreencapMgr，与内置方式一起测速
    {
        MaaWSCustomScreencapCallbacks callbacks = { };
        callbacks.screencap = on_screencap;
        callbacks.inactive = on_inactive;

        auto bridge = std::make_shared<CustomScreencapBridge>(&callbacks, &ctx);

        ScreencapMgr mgr;
        mgr.set_builtin_methods(Method_None); // 只测自定义，避免需要真实窗口
        mgr.register_unit("custom-callback", bridge);

        if (!mgr.connect()) {
            std::cerr << "connect failed" << std::endl;
            return 1;
        }

        std::cout << "selected unit: " << mgr.selected_unit_name() << std::endl;

        Image image;
        if (mgr.screencap(image)) {
            std::cout << "[bridge] screencap ok: " << image.cols << "x" << image.rows << " ch=" << image.channels() << std::endl;
        }
        else {
            std::cerr << "[bridge] screencap failed" << std::endl;
            return 1;
        }

        mgr.inactive();
    }

    return 0;
}