#include "maa-capture/MaaWSCustomScreencap.h"
#include <array>
#include <cstdio>
#include <cstring>
struct State { int released = 0, inactive = 0; bool wrong = false, short_data = false; std::array<uint8_t,12> pixels {10,20,30,255,40,50,60,255,0,0,0,0}; };
static MaaWSBool capture(void* ptr, MaaWSFrameInput* out, MaaWSError*) {
    auto& s = *static_cast<State*>(ptr);
    out->image = {sizeof(MaaWSImageView),2,1,4,12,s.short_data ? 7ULL : 12ULL,s.pixels.data()};
    out->info.target_type = MAA_WS_TARGET_CUSTOM; out->info.scope = MAA_WS_SCOPE_UNKNOWN;
    std::strcpy(out->info.source_key, s.wrong ? "wrong" : "test"); return 1;
}
static void release(void* ptr) { ++static_cast<State*>(ptr)->released; }
static void inactive(void* ptr) { ++static_cast<State*>(ptr)->inactive; }
int main() {
    if (MaaWSGetAbiVersion() != 2) return 1;
#if defined(_WIN32)
    if (bool(MaaWSManagerGetAvailableMethods() & MAA_WS_METHOD_FRAMEPOOL) != bool(MWS_FRAMEPOOL_AVAILABLE)) return 1;
#endif
    MaaWSRuntimeInfo runtime {}; runtime.struct_size = sizeof(runtime);
    if (!MaaWSGetRuntimeInfo(&runtime) || runtime.abi_version != 2) return 2;
    MaaWSCaptureOptions options {}; options.struct_size = sizeof(options); options.target.struct_size = sizeof(options.target);
    options.target.type = MAA_WS_TARGET_CUSTOM; options.target.scope = MAA_WS_SCOPE_UNKNOWN; std::strcpy(options.target.source_key, "test");
    MaaWSError error {}; error.struct_size = sizeof(error);
    auto manager = MaaWSManagerCreate(&options, &error); if (!manager) return 3;
    State state;
    MaaWSCustomScreencapCallbacks callbacks {sizeof(callbacks),0,nullptr,capture,release,inactive};
    bool ok = MaaWSManagerRegisterCustomUnit(manager,"test",MAA_WS_SCOPE_UNKNOWN,0,&callbacks,&state) && MaaWSManagerConnect(manager);
    MaaWSFrameHandle first = nullptr, second = nullptr;
    ok = ok && MaaWSManagerScreencap(manager,&first);
    state.pixels[0] = 99;
    ok = ok && MaaWSManagerScreencap(manager,&second);
    MaaWSImageView a {}, b {}; a.struct_size = sizeof(a); b.struct_size = sizeof(b);
    MaaWSFrameSampleInfo sample {};sample.struct_size=sizeof(sample);
    ok = ok && MaaWSFrameGetSampleInfo(first,&sample) && sample.sample_id==0 && sample.sampled_at_ns==0
        && sample.flags==MAA_WS_SAMPLE_CONNECT_PENDING && sample.delivered_at_ns>0;
    ok = ok && MaaWSFrameGetImage(first,&a) && MaaWSFrameGetImage(second,&b) && a.data[0] == 10 && b.data[0] == 99 && a.step == 6 && a.channels == 3;
    state.wrong = true;
    MaaWSFrameHandle rejected = reinterpret_cast<void*>(1);
    ok = ok && !MaaWSManagerScreencap(manager,&rejected) && !rejected && MaaWSManagerGetLastError(manager,&error) && error.code == MAA_WS_SOURCE_MISMATCH;
    auto saved = error; MaaWSManagerGetLastError(manager,&error); ok = ok && std::memcmp(&error,&saved,sizeof(error)) == 0;
    state.wrong = false; state.short_data = true;
    ok = ok && !MaaWSManagerScreencap(manager,&rejected) && !rejected && MaaWSManagerGetLastError(manager,&error) && error.code == MAA_WS_INVALID_ARGUMENT;
    MaaWSManagerInactive(manager); MaaWSManagerInactive(manager);
    ok = ok && state.inactive == 1 && state.released == 4;
    MaaWSManagerDestroy(manager);
    ok = ok && MaaWSFrameGetSampleInfo(first,&sample) && sample.sample_id==0;
    ok = ok && a.data[0] == 10; // frame lifetime is independent of manager
    MaaWSFrameDestroy(first); MaaWSFrameDestroy(second);
    if (!ok) return 4;
    std::puts("ABI 2: frame lifetime, provider mismatch, stride/length, release and close OK");
}
