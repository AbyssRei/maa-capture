#include "maa-capture/ScreencapMgr.h"
#include "maa-capture/MaaWSCustomScreencap.h"
#include <memory>
struct Provider : mws::ScreencapBase {
    std::optional<mws::CaptureFrame> screencap() override {
        mws::CaptureFrame f; f.image = mws::Image(1, 2, 3);
        f.image.set_to(mws::Scalar(10,20,30));
        f.source.struct_size = sizeof(f.source);
        f.source.target_type = MAA_WS_TARGET_CUSTOM;
        f.source.scope = MAA_WS_SCOPE_UNKNOWN;
        f.source.verification = MAA_WS_VERIFY_PROVIDER;
        f.source.pixel_semantics = MAA_WS_PIXELS_PROVIDER;
        mws::copy_text(f.source.source_key, "sample");
        return f;
    }
};
int main() {
    if (MaaWSGetAbiVersion() != MAA_WS_ABI_VERSION) return 1;
    mws::ScreencapMgr manager({mws::CaptureTarget::custom("sample")});
    if (!manager.register_unit("sample", MAA_WS_SCOPE_UNKNOWN, 0, std::make_shared<Provider>()) || !manager.connect()) return 2;
    return manager.screencap() ? 0 : 3;
}