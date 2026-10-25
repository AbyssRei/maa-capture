#include "maa-capture/MaaWSCustomScreencap.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
struct State { int inactive = 0; };
MaaWSBool capture(void*, MaaWSImageBuffer* out)
{
    static std::array<unsigned char, 12> pixels { 10, 20, 30, 255, 40, 50, 60, 255, 0, 0, 0, 0 };
    *out = MaaWSImageBuffer { pixels.data(), 2, 1, 4, 12 };
    return 1;
}
MaaWSBool inactive(void* context)
{
    ++static_cast<State*>(context)->inactive;
    return 1;
}
}
int main()
{
#if defined(_WIN32)
    const bool framepool = (MaaWSManagerGetAvailableMethods() & MAA_WS_METHOD_FRAMEPOOL) != 0;
    if (framepool != static_cast<bool>(MWS_FRAMEPOOL_AVAILABLE)) return 1;
#endif
    State state;
    MaaWSCustomScreencapCallbacks callbacks { nullptr, nullptr, capture, inactive };
    auto manager = MaaWSManagerCreate(nullptr, MAA_WS_METHOD_NONE);
    if (!manager) return 2;
    bool ok = MaaWSManagerRegisterCustomUnit(manager, "gradient", &callbacks, &state)
        && MaaWSManagerConnect(manager);
    MaaWSImageBuffer image {};
    const std::array<unsigned char, 6> expected { 10, 20, 30, 40, 50, 60 };
    ok = ok && MaaWSManagerScreencap(manager, &image) && image.width == 2 && image.height == 1
        && image.channels == 3 && image.step == 6
        && std::memcmp(image.data, expected.data(), expected.size()) == 0;
    const char* selected = MaaWSManagerSelectedUnitName(manager);
    ok = ok && selected && std::strcmp(selected, "gradient") == 0;
    MaaWSManagerInactive(manager);
    ok = ok && state.inactive == 1;
    MaaWSManagerDestroy(manager);
    if (!ok) return 3;
    State first, second;
    manager = MaaWSManagerCreate(nullptr, MAA_WS_METHOD_NONE);
    if (!manager) return 4;
    ok = MaaWSManagerRegisterCustomUnit(manager, "first", &callbacks, &first)
        && MaaWSManagerRegisterCustomUnit(manager, "second", &callbacks, &second)
        && MaaWSManagerConnect(manager);
    ok = ok && first.inactive + second.inactive == 1;
    MaaWSManagerInactive(manager);
    ok = ok && first.inactive + second.inactive == 3;
    MaaWSManagerDestroy(manager);
    if (!ok) return 5;
    std::puts("native API: callback lifetime, padded BGRA to BGR, selection, and cleanup OK");
    return 0;
}
