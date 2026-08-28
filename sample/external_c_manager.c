/* ==========================================================================
 * 示例：纯 C 消费「截图管理器 C API」
 *
 * 演示：
 *   1. 用纯 C 回调实现一个自定义截图单元（4x4 渐变），注册进管理器
 *   2. connect() 测速选择（仅自定义单元，避免依赖真实窗口）
 *   3. screencap() 取图并打印首行像素
 *
 * 也演示把管理器切换到内置方式（示例中 hwnd 传 NULL，内置前台方式会失败，
 * 因此只在有真实窗口的场景下启用——见 main 中注释掉的组合）。
 * ========================================================================== */

#include <stdio.h>
#include <string.h>

#include "maa-capture/MaaWSCustomScreencap.h"

typedef struct CaptureContext_
{
    unsigned char pixels[4 * 4 * 4];
} CaptureContext;

static MaaWSBool on_screencap(void* trans_arg, MaaWSImageBuffer* out)
{
    CaptureContext* ctx = (CaptureContext*)trans_arg;
    if (!ctx || !out) {
        return 0;
    }

    out->data = ctx->pixels;
    out->width = 4;
    out->height = 4;
    out->channels = 4; /* BGRA，桥接层会转 BGR */
    out->step = 4 * 4;
    return 1;
}

int main(void)
{
    CaptureContext ctx;
    int y, x;
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            unsigned char* p = &ctx.pixels[(y * 4 + x) * 4];
            p[0] = (unsigned char)(x * 64);
            p[1] = (unsigned char)(y * 64);
            p[2] = 128;
            p[3] = 255;
        }
    }

    /* 创建管理器：只用自定义单元（不启用内置方式，无需窗口句柄） */
    MaaWSManagerHandle mgr = MaaWSManagerCreate(NULL, MAA_WS_METHOD_NONE);
    if (!mgr) {
        fprintf(stderr, "manager create failed\n");
        return 1;
    }

    MaaWSCustomScreencapCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.screencap = on_screencap;

    if (!MaaWSManagerRegisterCustomUnit(mgr, "gradient-4x4", &cb, &ctx)) {
        fprintf(stderr, "register failed\n");
        MaaWSManagerDestroy(mgr);
        return 1;
    }

    if (!MaaWSManagerConnect(mgr)) {
        fprintf(stderr, "connect failed\n");
        MaaWSManagerDestroy(mgr);
        return 1;
    }

    printf("selected unit: %s\n", MaaWSManagerSelectedUnitName(mgr));

    MaaWSImageBuffer img;
    memset(&img, 0, sizeof(img));
    if (!MaaWSManagerScreencap(mgr, &img)) {
        fprintf(stderr, "screencap failed\n");
        MaaWSManagerDestroy(mgr);
        return 1;
    }

    printf("[manager] screencap ok: %dx%d channels=%d step=%d\n", img.width, img.height, img.channels, img.step);
    /* BGR 归一化输出：首像素 B=0 G=0 R=128 */
    if (img.data) {
        printf("[manager] first pixel RGB = %u,%u,%u\n", img.data[0], img.data[1], img.data[2]);
    }

    MaaWSManagerInactive(mgr);
    MaaWSManagerDestroy(mgr);
    return 0;
}