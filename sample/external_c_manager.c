#include "maa-capture/MaaWSCustomScreencap.h"
#include <stdio.h>
#include <string.h>
static MaaWSBool capture(void* ctx, MaaWSFrameInput* out, MaaWSError* error) {
    static const uint8_t pixels[] = {10,20,30,255,40,50,60,255};
    (void)ctx; (void)error;
    MaaWSImageView image = {sizeof(MaaWSImageView),2,1,4,8,8,pixels};
    out->image = image;
    out->info.target_type = MAA_WS_TARGET_CUSTOM;
    out->info.scope = MAA_WS_SCOPE_UNKNOWN;
    strcpy(out->info.source_key, "sample");
    return 1;
}
int main(void) {
    MaaWSCaptureOptions options = {0};
    MaaWSError error = {0};
    MaaWSCustomScreencapCallbacks callbacks = {0};
    MaaWSManagerHandle manager;
    MaaWSFrameHandle frame = NULL;
    MaaWSImageView image = {0};
    int ok;
    if (MaaWSGetAbiVersion() != MAA_WS_ABI_VERSION) return 1;
    options.struct_size = sizeof(options); options.target.struct_size = sizeof(options.target);
    options.target.type = MAA_WS_TARGET_CUSTOM; options.target.scope = MAA_WS_SCOPE_UNKNOWN;
    strcpy(options.target.source_key, "sample"); error.struct_size = sizeof(error);
    manager = MaaWSManagerCreate(&options, &error);
    if (!manager) { fprintf(stderr, "%s\n", error.message); return 2; }
    callbacks.struct_size = sizeof(callbacks); callbacks.screencap = capture;
    ok = MaaWSManagerRegisterCustomUnit(manager, "sample", MAA_WS_SCOPE_UNKNOWN, 0, &callbacks, NULL)
        && MaaWSManagerConnect(manager) && MaaWSManagerScreencap(manager, &frame);
    image.struct_size = sizeof(image);
    ok = ok && MaaWSFrameGetImage(frame, &image) && image.width == 2 && image.data[0] == 10;
    MaaWSManagerDestroy(manager); /* frame remains valid */
    MaaWSFrameDestroy(frame);
    return ok ? 0 : 3;
}
