/* ==========================================================================
 * 示例：纯 C 消费「外部回调截图接口」（对应 MaaCustomControllerCallbacks::screencap）
 *
 * 仅包含纯 C 头文件 MaaWSCustomScreencap.h，不依赖任何 C++ 设施。
 * 生成一张 4x4 的 BGRA 渐变图写入 out buffer，验证 C 契约可用。
 * ========================================================================== */

#include <stdio.h>
#include <string.h>

#include "maa-capture/MaaWSCustomScreencap.h"

/* ---- 回调上下文 ---- */
typedef struct CaptureContext_
{
    unsigned char pixels[4 * 4 * 4]; /* 4x4 BGRA */
} CaptureContext;

/*
 * 外部截图回调：等价 MaaCustomControllerCallbacks.screencap
 * 成功返回 1，失败返回 0；图像写入 out。
 */
static MaaWSBool on_screencap(void* trans_arg, MaaWSImageBuffer* out)
{
    CaptureContext* ctx = (CaptureContext*)trans_arg;
    if (!ctx || !out) {
        return 0;
    }

    out->data = ctx->pixels;
    out->width = 4;
    out->height = 4;
    out->channels = 4;
    out->step = 4 * 4; /* 行字节数 */
    return 1;
}

int main(void)
{
    CaptureContext ctx;
    int y, x;

    /* 填充 4x4 BGRA 渐变 */
    for (y = 0; y < 4; ++y) {
        for (x = 0; x < 4; ++x) {
            unsigned char* p = &ctx.pixels[(y * 4 + x) * 4];
            p[0] = (unsigned char)(x * 64); /* B */
            p[1] = (unsigned char)(y * 64); /* G */
            p[2] = 128;                     /* R */
            p[3] = 255;                     /* A */
        }
    }

    MaaWSCustomScreencapCallbacks cb;
    memset(&cb, 0, sizeof(cb));
    cb.screencap = on_screencap;

    MaaWSCustomScreencapHandle handle = MaaWSCustomScreencapCreate(&cb, &ctx);
    if (!handle) {
        fprintf(stderr, "create failed\n");
        return 1;
    }

    MaaWSImageBuffer img;
    memset(&img, 0, sizeof(img));
    if (MaaWSCustomScreencapScreencap(handle, &img)) {
        printf("[C only] screencap ok: %dx%d channels=%d\n", img.width, img.height, img.channels);
        /* 校验首个像素 */
        printf("[C only] first pixel BGRA = %u,%u,%u,%u\n",
               img.data[0], img.data[1], img.data[2], img.data[3]);
    }
    else {
        fprintf(stderr, "screencap failed\n");
        MaaWSCustomScreencapDestroy(handle);
        return 1;
    }

    MaaWSCustomScreencapDestroy(handle);
    return 0;
}