// Opt-in real desktop verification. Paints only its own solid-color test window.
#include "maa-capture/ScreencapMgr.h"
#include "maa-capture/MaaWSCustomScreencap.h"
#include "maa-capture/Windows/SafeWindows.hpp"
#include "maa-capture/Windows/PseudoMinimizeHelper.h"
#include "maa-capture/Windows/PrintWindowWithPseudoMinimizeScreencap.h"
#include "maa-capture/Windows/DesktopDupScreencap.h"
#include "maa-capture/Windows/DesktopDupWindowScreencap.h"
#include <future>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace mws;
namespace mws {
struct ScreencapMgrTestAccess {
    static void replace(ScreencapMgr& manager,const char* key,std::shared_ptr<ScreencapBase> unit){manager.units_[key]=std::move(unit);}
};
}
static void paint(HDC dc) {
    RECT clip{};if(GetClipBox(dc,&clip)!=ERROR)FillRect(dc,&clip,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    const COLORREF colors[] {RGB(255,0,0),RGB(0,255,0),RGB(0,0,255),RGB(255,255,255)};
    for (int i=0;i<4;++i) {
        RECT r {(i%2)*160,(i/2)*120,(i%2+1)*160,(i/2+1)*120};
        HBRUSH brush=CreateSolidBrush(colors[i]); FillRect(dc,&r,brush); DeleteObject(brush);
    }
}
static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if (msg==WM_PAINT) {PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);paint(dc);EndPaint(h,&p);return 0;}
    if (msg==WM_PRINT || msg==WM_PRINTCLIENT) {paint(reinterpret_cast<HDC>(wp));return 0;}
    return DefWindowProcW(h,msg,wp,lp);
}
static void pump(int milliseconds=150) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(milliseconds);
    while (std::chrono::steady_clock::now()<end) {MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
}
static bool colors(const Image& i) {
    if (i.cols!=320 || i.rows!=240 || i.channels()!=3) return false;
    const unsigned char expected[4][3] {{0,0,255},{0,255,0},{255,0,0},{255,255,255}};
    for(int n=0;n<4;++n) {
        const auto* p=i.data+(60+(n/2)*120)*i.step+(80+(n%2)*160)*3;
        for(int c=0;c<3;++c) if (std::abs(int(p[c])-int(expected[n][c]))>2) return false;
    }
    return true;
}
static void ppm(const Image& image,const std::filesystem::path& path) {
    std::ofstream file(path,std::ios::binary);file<<"P6\n"<<image.cols<<" "<<image.rows<<"\n255\n";
    for(int y=0;y<image.rows;++y)for(int x=0;x<image.cols;++x) {const auto* p=image.data+y*image.step+x*3;const char rgb[]{char(p[2]),char(p[1]),char(p[0])};file.write(rgb,3);}
}
static bool lease_contracts(HWND h) {
    const auto baseline = GetWindowLongPtrW(h,GWL_EXSTYLE);
    for (bool layered : {false,true}) {
        const auto original = baseline | (layered ? WS_EX_LAYERED : 0);
        SetWindowLongPtrW(h,GWL_EXSTYLE,original);
        if(layered && !SetLayeredWindowAttributes(h,RGB(3,4,5),177,LWA_ALPHA|LWA_COLORKEY))return false;
        for (bool reverse : {false,true}) {
            PseudoMinimizeHelper a(h),b(h);
            // Starting a helper must not snapshot the other helper's mutation.
            if(!a.start()||!b.start())return false;
            ShowWindow(h,SW_MINIMIZE);
            if(!a.ensure_not_minimized()||!b.ensure_not_minimized()||!PseudoMinimizeHelper::logically_minimized(h))return false;
            SetWindowLongPtrW(h,GWL_EXSTYLE,GetWindowLongPtrW(h,GWL_EXSTYLE)|WS_EX_NOACTIVATE);
            if(!(reverse?b.stop():a.stop())||IsIconic(h))return false;
            if(!(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TRANSPARENT))return false;
            if(!(reverse?a.stop():b.stop())||!IsIconic(h))return false;
            if(GetWindowLongPtrW(h,GWL_EXSTYLE)!=(original|WS_EX_NOACTIVATE))return false;
            if(layered) {
                COLORREF key;BYTE alpha;DWORD flags;
                if(!GetLayeredWindowAttributes(h,&key,&alpha,&flags)||key!=RGB(3,4,5)||alpha!=177||flags!=(LWA_ALPHA|LWA_COLORKEY))return false;
            }
            SetWindowLongPtrW(h,GWL_EXSTYLE,original);ShowWindow(h,SW_SHOWNOACTIVATE);
        }
    }
    SetWindowLongPtrW(h,GWL_EXSTYLE,baseline);
    {
        ShowWindow(h,SW_MINIMIZE);PseudoMinimizeHelper helper(h);
        if(!helper.start()||!helper.ensure_not_minimized())return false;
        ShowWindow(h,SW_RESTORE);SetForegroundWindow(h);
        const bool observed=GetForegroundWindow()==h;
        if(!helper.stop()||(observed&&IsIconic(h))||GetWindowLongPtrW(h,GWL_EXSTYLE)!=baseline)return false;
    }
    // Two independent host threads operate on the same HWND. The owner thread
    // pumps messages while ShowWindow synchronously talks to it.
    ShowWindow(h,SW_MINIMIZE);
    std::promise<void> joined, release;
    auto both = joined.get_future(); auto released = release.get_future().share();
    auto first = std::async(std::launch::async,[&] {
        PseudoMinimizeHelper helper(h);
        const bool ok = helper.start()&&helper.ensure_not_minimized(); joined.set_value();
        released.wait(); return helper.stop()&&ok;
    });
    while(both.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    auto second = std::async(std::launch::async,[&] {
        PseudoMinimizeHelper helper(h);return helper.start()&&helper.ensure_not_minimized()&&helper.stop();
    });
    while(second.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    const bool second_result=second.get();const bool still_restored=!IsIconic(h);
    const bool second_ok=second_result&&still_restored;release.set_value();
    while(first.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    const bool first_ok=first.get();
    const bool ok=first_ok&&second_ok&&IsIconic(h)&&GetWindowLongPtrW(h,GWL_EXSTYLE)==baseline;
    if(!ok)std::fprintf(stderr,"lease threads: first=%d second=%d still_restored=%d final_minimized=%d style=%lld expected=%lld\n",first_ok,second_result,still_restored,bool(IsIconic(h)),GetWindowLongPtrW(h,GWL_EXSTYLE),baseline);
    ShowWindow(h,SW_SHOWNOACTIVATE);return ok;
}
static bool focus_owned(HWND h) {
    // Minimize can briefly leave no foreground window. Let that transition
    // settle before deciding that Windows denied this owned-window control.
    for(int attempt=0;attempt<8;++attempt) {
        DWORD foreground_thread=GetWindowThreadProcessId(GetForegroundWindow(),nullptr);
        const DWORD thread=GetCurrentThreadId();
        const bool attached=foreground_thread&&foreground_thread!=thread&&AttachThreadInput(thread,foreground_thread,TRUE);
        ShowWindow(h,SW_SHOWNOACTIVATE);SetForegroundWindow(h);SetFocus(h);
        if(attached)AttachThreadInput(thread,foreground_thread,FALSE);
        pump(40);if(GetForegroundWindow()==h)return true;
    }
    return false;
}
static bool focus_trials(HWND h,const MaaWSMonitorInfo& monitor,unsigned cycles,const std::filesystem::path& output) {
    struct Window {HWND h;~Window(){if(h)DestroyWindow(h);}};
    Window other_a{CreateWindowExW(WS_EX_TOOLWINDOW,L"MwsColorFixture",L"owned focus control A",WS_POPUP,
        int(monitor.desktop_rect.x)+400,int(monitor.desktop_rect.y)+40,120,90,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    Window other_b{CreateWindowExW(WS_EX_TOOLWINDOW,L"MwsColorFixture",L"owned focus control B",WS_POPUP,
        int(monitor.desktop_rect.x)+540,int(monitor.desktop_rect.y)+40,120,90,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr)};
    if(!other_a.h||!other_b.h)return false;
    std::ofstream csv;
    if(!output.empty())csv.open(output/("focus-"+std::to_string(monitor.index)+".csv"));
    csv<<"case,cycle,phase,foreground,input_tick,iconic,style\n";
    const auto record=[&](const char* scenario,unsigned cycle,const char* phase){
        LASTINPUTINFO input{sizeof(input)};GetLastInputInfo(&input);
        csv<<scenario<<','<<cycle<<','<<phase<<','<<reinterpret_cast<uintptr_t>(GetForegroundWindow())<<','<<input.dwTime<<','
           <<bool(IsIconic(h))<<','<<GetWindowLongPtrW(h,GWL_EXSTYLE)<<'\n';csv.flush();
    };
    const auto baseline=GetWindowLongPtrW(h,GWL_EXSTYLE);
    unsigned failures=0,input_changes=0,no_input_target_restored=0;
    for(const char* scenario:{"no_switch","activating_minimize","other_windows","target_activation"})for(unsigned cycle=0;cycle<cycles;++cycle){
        if(!focus_owned(other_a.h)){record(scenario,cycle,"control_unavailable");std::fprintf(stderr,"Owned focus control unavailable case=%s cycle=%u\n",scenario,cycle);return false;}
        ShowWindow(h,std::string_view(scenario)=="activating_minimize"?SW_MINIMIZE:SW_SHOWMINNOACTIVE);
        if(std::string_view(scenario)!="activating_minimize")pump(8);
        LASTINPUTINFO initial{sizeof(initial)};GetLastInputInfo(&initial);
        PseudoMinimizeHelper a(h),b(h);
        bool ok=a.start()&&b.start()&&a.ensure_not_minimized()&&b.ensure_not_minimized();record(scenario,cycle,"acquired");
        if(std::string_view(scenario)=="other_windows")ok=focus_owned(other_b.h)&&ok;
        else if(std::string_view(scenario)=="target_activation") {ShowWindow(h,SW_RESTORE);ok=focus_owned(h)&&ok;}
        else pump(12);
        record(scenario,cycle,"after_switch");
        LASTINPUTINFO after{sizeof(after)};GetLastInputInfo(&after);
        const bool input_changed=after.dwTime!=initial.dwTime;input_changes+=input_changed;
        ok=a.stop()&&ok;record(scenario,cycle,"first_release");
        ok=b.stop()&&ok;record(scenario,cycle,"last_release");
        if(!input_changed&&std::string_view(scenario)=="target_activation"&&!IsIconic(h))++no_input_target_restored;
        // External activation is a supported restoration intent even without
        // new input. Every scenario must restore owned style bits.
        if(GetWindowLongPtrW(h,GWL_EXSTYLE)!=baseline)ok=false;
        if(!input_changed&&std::string_view(scenario)!="target_activation"&&!IsIconic(h))ok=false;
        if(std::string_view(scenario)=="target_activation"&&IsIconic(h))ok=false;
        if(!ok)++failures;
        focus_owned(other_a.h);
    }
    unsigned thread_failures=0;
    for(unsigned cycle=0;cycle<cycles;++cycle){
        if(!focus_owned(other_a.h))return false;
        ShowWindow(h,SW_MINIMIZE);
        std::promise<void> acquired,release;
        auto ready=acquired.get_future();auto released=release.get_future().share();
        auto first=std::async(std::launch::async,[&]{PseudoMinimizeHelper a(h);const bool ok=a.start()&&a.ensure_not_minimized();acquired.set_value();released.wait();return a.stop()&&ok;});
        while(ready.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump(8);
        record("host_threads",cycle,"first_acquired");
        if(cycle%2)focus_owned(other_b.h);
        auto second=std::async(std::launch::async,[&]{PseudoMinimizeHelper b(h);return b.start()&&b.ensure_not_minimized()&&b.stop();});
        while(second.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump(8);
        const bool second_ok=second.get()&&!IsIconic(h);record("host_threads",cycle,"second_released");release.set_value();
        while(first.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump(8);
        const bool ok=first.get()&&second_ok&&IsIconic(h)&&GetWindowLongPtrW(h,GWL_EXSTYLE)==baseline;
        if(!ok)++thread_failures;record("host_threads",cycle,"last_release");
    }
    std::printf("focus display=%u cases=%u failures=%u thread_failures=%u input_changes=%u programmatic_target_left_restored=%u\n",
        monitor.index,cycles*5,failures,thread_failures,input_changes,no_input_target_restored);
    ShowWindow(h,SW_SHOWNOACTIVATE);return failures==0&&thread_failures==0;
}
static bool manager_contracts(HWND h) {
    const auto style=GetWindowLongPtrW(h,GWL_EXSTYLE);
    const auto target=CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId());
    for(uint64_t second_method:{Method_PrintWindow,Method_FramePool}) for(bool reverse:{false,true}) {
        if(!(available_builtin_methods()&second_method))continue;
        ShowWindow(h,SW_MINIMIZE);
        ScreencapMgr first({target,Method_PrintWindow}),second({target,second_method});
        if(!first.connect()||!second.connect()||!first.screencap()||!second.screencap())return false;
        (reverse?second:first).inactive();
        if(IsIconic(h)||!(GetWindowLongPtrW(h,GWL_EXSTYLE)&WS_EX_TRANSPARENT))return false;
        // Even when another Manager has physically restored it, GDI is refused.
        ScreencapMgr gdi({target,Method_GDI});
        if(gdi.connect()||gdi.last_error().code!=MAA_WS_UNSUPPORTED_GEOMETRY)return false;
        (reverse?first:second).inactive();
        if(!IsIconic(h)||GetWindowLongPtrW(h,GWL_EXSTYLE)!=style)return false;
    }
    ShowWindow(h,SW_MINIMIZE);
    {
        ScreencapMgr combined({target,Method_PrintWindow|Method_GDI| (available_builtin_methods()&Method_FramePool)});
        if(!combined.connect()||combined.selected_unit_name()=="GDI"||!combined.screencap())return false;
        combined.inactive();
        if(!IsIconic(h)||GetWindowLongPtrW(h,GWL_EXSTYLE)!=style)return false;
    }
    ShowWindow(h,SW_SHOWNOACTIVATE);
    ScreencapMgr moved({target,Method_PrintWindow});
    if(!moved.connect())return false;
    RECT r;GetWindowRect(h,&r);SetWindowPos(h,nullptr,r.left+10,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    auto frame=moved.screencap();
    if(!frame||frame->source.desktop_rect.x!=r.left+10)return false;
    moved.inactive();SetWindowPos(h,nullptr,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(h,SW_MINIMIZE);
    std::promise<void> joined,release;
    auto both=joined.get_future();auto released=release.get_future().share();
    auto first=std::async(std::launch::async,[&]{
        ScreencapMgr m({target,Method_PrintWindow});const bool ok=m.connect()&&bool(m.screencap());joined.set_value();released.wait();m.inactive();return ok;
    });
    while(both.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    auto second=std::async(std::launch::async,[&]{ScreencapMgr m({target,Method_PrintWindow});return m.connect()&&bool(m.screencap());});
    while(second.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    const bool other=second.get()&&!IsIconic(h);release.set_value();
    while(first.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready)pump();
    if(!first.get()||!other||!IsIconic(h)||GetWindowLongPtrW(h,GWL_EXSTYLE)!=style)return false;
    ShowWindow(h,SW_SHOWNOACTIVATE);
    return true;
}
class InjectedBackend : public TargetScreencapBase {
public:
    explicit InjectedBackend(CaptureTarget t) : TargetScreencapBase(t,Method_FramePool,true) {}
    uint32_t failure=MAA_WS_TIMEOUT;
    std::optional<Image> capture_image() override {
        if(failure){error_=capture_error(failure,MAA_WS_STAGE_CAPTURE,"injected backend failure",method_);return {};}
        Image image(240,320,3);std::memset(image.data,33,image.rows*image.step);return image;
    }
    void inactive() override {invalidate_session();}
};
static bool cache_contracts(HWND h) {
    const auto target=CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId());
    InjectedBackend backend(target);
    if(backend.screencap()||backend.last_error().code!=MAA_WS_TIMEOUT){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=0;auto sampled=backend.screencap();if(!sampled||sampled->sample.flags!=MAA_WS_SAMPLE_NEW){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=MAA_WS_TIMEOUT;auto reused=backend.screencap();
    if(!reused||reused->sample.flags!=MAA_WS_SAMPLE_REUSED||reused->sample.sample_id!=sampled->sample.sample_id
        ||reused->source.timestamp_ns!=sampled->source.timestamp_ns||reused->sample.sampled_at_ns!=sampled->sample.sampled_at_ns){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=MAA_WS_PERMISSION_DENIED;if(backend.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=MAA_WS_TIMEOUT;if(backend.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=0;if(!backend.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}backend.inactive();backend.failure=MAA_WS_TIMEOUT;if(backend.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    backend.failure=0;if(!backend.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    RECT r;GetWindowRect(h,&r);SetWindowPos(h,nullptr,r.left+5,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    backend.failure=MAA_WS_TIMEOUT;const bool rejected=!backend.screencap();
    SetWindowPos(h,nullptr,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    if(!rejected){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    if(!(available_builtin_methods()&Method_FramePool))return true;
    ScreencapMgr manager({target,Method_FramePool,MAA_WS_POLICY_FRAMEPOOL_CACHE});
    if(!manager.connect()){std::fprintf(stderr,"cached FramePool connect: %s\n",manager.last_error().message);{std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}}
    auto first=manager.screencap();
    if(!first||!(first->sample.flags&MAA_WS_SAMPLE_CONNECT_PENDING)||!colors(first->image)){std::fprintf(stderr,"cache assertion line %d: %s\n",__LINE__,manager.last_error().message);return false;}
    bool saw_replay=false;
    for(int i=0;i<4;++i) {
        auto next=manager.screencap();if(!next||!colors(next->image)){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
        if(next->sample.flags&MAA_WS_SAMPLE_REUSED){
            if(next->sample.sample_id<first->sample.sample_id||next->sample.sampled_at_ns!=next->source.timestamp_ns
                ||(next->sample.sample_id==first->sample.sample_id&&next->source.timestamp_ns!=first->source.timestamp_ns)){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
            saw_replay=true;break;
        }
        first=std::move(next);
    }
    if(!saw_replay){std::fprintf(stderr,"No static FramePool reuse observed\n");{std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}}
    // A visible change must eventually produce a new sample containing the
    // marker, even after multiple no-update deliveries.
    HDC dc=GetDC(h);SetPixel(dc,10,10,RGB(93,82,71));ReleaseDC(h,dc);
    pump(); // Let the one-shot update arrive before the next caller request.
    bool changed=false;
    for(int i=0;i<4;++i){auto f=manager.screencap();if(!f){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}const auto* p=f->image.data+10*f->image.step+30;
        if(f->sample.sample_id>first->sample.sample_id&&p[0]==71&&p[1]==82&&p[2]==93){changed=true;break;}}
    if(!changed){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    ScreencapMgr second({target,Method_FramePool,MAA_WS_POLICY_FRAMEPOOL_CACHE});
    dc=GetDC(h);SetPixel(dc,10,10,RGB(57,68,79));ReleaseDC(h,dc);pump();
    if(!second.connect()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    auto other=second.screencap();if(!other){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    const auto* marker=other->image.data+10*other->image.step+30;
    if(marker[0]!=79||marker[1]!=68||marker[2]!=57){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    second.inactive();
    // Resize invalidates both the retained first frame and the cache. A fresh
    // sample or a classified error is acceptable; old dimensions never are.
    SetWindowPos(h,nullptr,0,0,330,250,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);UpdateWindow(h);
    auto resized=manager.screencap();if(resized&&(resized->image.cols!=330||resized->image.rows!=250||(resized->sample.flags&MAA_WS_SAMPLE_REUSED))){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    SetWindowPos(h,nullptr,0,0,320,240,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    manager.inactive();UpdateWindow(h);InvalidateRect(h,nullptr,FALSE);UpdateWindow(h);
    if(!manager.connect()||!manager.screencap()){std::fprintf(stderr,"cache assertion line %d\n",__LINE__);return false;}
    manager.inactive();
    return sampled->image.data[0]==33; // independent frame survives invalidation
}
static bool lifetime_contracts(HWND h) {
    const auto target=CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId());
    ScreencapMgr old({target,Method_PrintWindow});if(!old.connect())return false;
    // Simulate an observed lifetime-token loss while keeping PID/thread and
    // geometry identical. Neither the pending pixels nor an old lease may be
    // reattached to the subsequent marked lifetime.
    EnumPropsExW(h,[](HWND window,LPWSTR name,HANDLE,ULONG_PTR)->BOOL {
        if(!IS_INTRESOURCE(name)&&std::wstring(name).starts_with(L"maa-capture.lease."))RemovePropW(window,name);
        return TRUE;
    },0);
    if(old.screencap()||old.last_error().code!=MAA_WS_TARGET_CHANGED)return false;
    ScreencapMgr replacement({target,Method_PrintWindow});if(!replacement.connect())return false;
    old.inactive();if(!replacement.screencap())return false;
    replacement.inactive();return true;
}
static bool enhancement_contracts(HWND h,const MaaWSMonitorInfo& monitor) {
    const auto target=CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId());
    const auto baseline=GetWindowLongPtrW(h,GWL_EXSTYLE);
    const auto check=[&](bool ok,int line){if(!ok)std::fprintf(stderr,"enhancement assertion line %d\n",line);return ok;};
#define ENHANCE(x) do{if(!check(bool(x),__LINE__))return false;}while(0)
    HWND focus=CreateWindowExW(WS_EX_TOOLWINDOW,L"MwsColorFixture",L"owned focus fixture",WS_POPUP,
        int(monitor.desktop_rect.x)+400,int(monitor.desktop_rect.y)+40,120,90,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ENHANCE(focus);struct Focus {HWND h;~Focus(){DestroyWindow(h);}} focus_window{focus};
    ShowWindow(focus,SW_SHOW);SetForegroundWindow(focus);pump();
    // Windows may deny a background test process foreground permission. Use
    // the actual pre-existing foreground HWND as the sentinel in that case;
    // target capture must preserve it without switching it back afterward.
    const HWND sentinel=GetForegroundWindow();ENHANCE(sentinel&&sentinel!=h);
    std::atomic_bool changed=false;
    std::jthread observer([&](std::stop_token stop){while(!stop.stop_requested()){if(GetForegroundWindow()!=sentinel)changed=true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}});
    for(int n=0;n<3;++n){
        ShowWindow(h,SW_SHOWMINNOACTIVE);
        ScreencapMgr strict({target,0,MAA_WS_POLICY_STRICT_BACKGROUND|MAA_WS_POLICY_DIAGNOSTICS});
        ENHANCE(strict.connect()&&strict.screencap());
        ENHANCE(strict.selected_unit_name()!="ScreenDC"&&strict.selected_unit_name()!="DXGI_DesktopDup_Window");
        strict.inactive();ENHANCE(IsIconic(h)&&GetWindowLongPtrW(h,GWL_EXSTYLE)==baseline);
    }
    observer.request_stop();observer.join();ENHANCE(!changed&&GetForegroundWindow()==sentinel);
    MaaWSError error{};
    ENHANCE(!validate_options({target,Method_ScreenDC,MAA_WS_POLICY_STRICT_BACKGROUND},error)&&error.code==MAA_WS_UNSUPPORTED_OPTION);
    ENHANCE(!validate_options({target,Method_DXGI_DesktopDup_Window,MAA_WS_POLICY_DXGI_CACHE|MAA_WS_POLICY_STRICT_BACKGROUND},error));
    ShowWindow(h,SW_SHOWNOACTIVATE);InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
    {
        InjectedBackend injected(target);injected.failure=0;ENHANCE(injected.screencap());injected.failure=MAA_WS_TIMEOUT;
        auto op=std::make_shared<CaptureOperation>();op->max_sample_age_ns=1;injected.set_operation(op);
        ENHANCE(!injected.screencap()&&injected.last_error().code==MAA_WS_STALE_FRAME);
        op=std::make_shared<CaptureOperation>();op->policy_flags=MAA_WS_POLICY_FRESH_ONLY;injected.set_operation(op);
        ENHANCE(!injected.screencap()&&injected.last_error().code==MAA_WS_STALE_FRAME);
    }
    {
        ScreencapMgr fresh({target,Method_PrintWindow,MAA_WS_POLICY_FRESH_ONLY});
        ENHANCE(fresh.connect());auto first=fresh.screencap();
        ENHANCE(first&&first->sample.sample_id>=2&&first->sample.flags==MAA_WS_SAMPLE_NEW);
    }
    {
        ShowWindow(h,SW_SHOWMINNOACTIVE);
        CaptureOptions options{target,Method_PrintWindow,MAA_WS_POLICY_STRICT_BACKGROUND};options.capture_timeout_ns=10000000;
        // Controlled non-cooperative work while a real PrintWindow wrapper
        // holds the mutation lease. Actual OS PrintWindow may bypass WM_PRINT
        // through the compositor, so WM_PRINT sleeps cannot reliably block it.
        struct DelayedPrint : PrintWindowWithPseudoMinimizeScreencap {
            using PrintWindowWithPseudoMinimizeScreencap::PrintWindowWithPseudoMinimizeScreencap;
            bool delay=false;
            std::optional<CaptureFrame> screencap() override {
                if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(100));
                return PrintWindowWithPseudoMinimizeScreencap::screencap();
            }
        };
        auto unit=std::make_shared<DelayedPrint>(target);
        ScreencapMgr blocked(options);ScreencapMgrTestAccess::replace(blocked,"PrintWindow",unit);
        ENHANCE(blocked.connect()&&blocked.screencap());unit->delay=true;
        const auto start=std::chrono::steady_clock::now();auto frame=blocked.screencap();
        const auto elapsed=std::chrono::steady_clock::now()-start;
        ENHANCE(!frame&&blocked.last_error().code==MAA_WS_DEADLINE_EXCEEDED&&!blocked.connected());
        ENHANCE(elapsed>=std::chrono::milliseconds(90)&&IsIconic(h)&&GetWindowLongPtrW(h,GWL_EXSTYLE)==baseline);
    }
    if(available_builtin_methods()&Method_FramePool){
        ShowWindow(h,SW_SHOWMINNOACTIVE);
        CaptureOptions options{target,Method_FramePool,MAA_WS_POLICY_STRICT_BACKGROUND|MAA_WS_POLICY_FRAMEPOOL_CACHE|MAA_WS_POLICY_FRESH_ONLY};
        ScreencapMgr cancelled(options);ENHANCE(cancelled.connect());
        for(int n=0;n<3;++n){if(!cancelled.screencap())break;}
        auto signal=cancelled.cancellation_handle();std::atomic_bool requested=false;
        std::jthread request([&]{std::this_thread::sleep_for(std::chrono::milliseconds(50));requested=signal->request();});
        const auto start=std::chrono::steady_clock::now();auto frame=cancelled.screencap();request.join();
        ENHANCE(requested&&!frame&&cancelled.last_error().code==MAA_WS_CANCELLED&&!cancelled.connected());
        ENHANCE(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(500));
        ENHANCE(IsIconic(h)&&GetWindowLongPtrW(h,GWL_EXSTYLE)==baseline);
    }
    ShowWindow(h,SW_SHOWNOACTIVATE);InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
    // Cover the output with a static owned surface so desktop animations do not
    // masquerade as content changes in this DXGI no-update test.
    HWND cover=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,L"MwsColorFixture",L"owned static output fixture",WS_POPUP,
        int(monitor.desktop_rect.x),int(monitor.desktop_rect.y),int(monitor.pixel_width),int(monitor.pixel_height),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ENHANCE(cover);Focus cover_window{cover};ShowWindow(cover,SW_SHOWNOACTIVATE);UpdateWindow(cover);pump();
    {
        ScreencapMgr strict({CaptureTarget::monitor(monitor.monitor_key),Method_DXGI_DesktopDup});
        ENHANCE(strict.connect()&&strict.screencap());bool timeout=false;
        for(int n=0;n<6;++n){std::this_thread::sleep_for(std::chrono::milliseconds(100));if(!strict.screencap()){ENHANCE(strict.last_error().code==MAA_WS_TIMEOUT);timeout=true;break;}}
        std::printf("display %u: real DXGI default no-update timeout observed=%d\n",monitor.index,timeout);
    }
    {
        ScreencapMgr cache({CaptureTarget::monitor(monitor.monitor_key),Method_DXGI_DesktopDup,MAA_WS_POLICY_DXGI_CACHE|MAA_WS_POLICY_STRICT_BACKGROUND|MAA_WS_POLICY_DIAGNOSTICS});
        ENHANCE(cache.connect());auto first=cache.screencap();ENHANCE(first);bool replay=false;
        for(int n=0;n<6;++n){std::this_thread::sleep_for(std::chrono::milliseconds(100));auto frame=cache.screencap();ENHANCE(frame);
            if(frame->sample.flags&MAA_WS_SAMPLE_REUSED){ENHANCE(frame->sample.sample_id==first->sample.sample_id&&frame->source.timestamp_ns==first->source.timestamp_ns);replay=true;break;}first=std::move(frame);}
        std::printf("display %u: real DXGI no-update replay observed=%d\n",monitor.index,replay);cache.inactive();ENHANCE(first->image.data);
    }
    {
        // Static pixels can still generate mouse/compositor samples. Exercise
        // deterministic WAIT_TIMEOUT and access-lost paths after a *real*
        // DXGI output sample without calling such samples content changes.
        struct ControlledDup : DesktopDupScreencap {
            uint32_t failure=0;
            ControlledDup(CaptureTarget t,bool cache):DesktopDupScreencap(t,Method_DXGI_DesktopDup,cache){}
            std::optional<Image> capture_image() override {
                if(failure){error_=capture_error(failure,MAA_WS_STAGE_CAPTURE,"injected DXGI result",method_,failure==MAA_WS_TIMEOUT?DXGI_ERROR_WAIT_TIMEOUT:DXGI_ERROR_ACCESS_LOST,"HRESULT");return {};}
                return DesktopDupScreencap::capture_image();
            }
        };
        ControlledDup strict(CaptureTarget::monitor(monitor.monitor_key),false),cache(CaptureTarget::monitor(monitor.monitor_key),true);
        ENHANCE(strict.screencap());strict.failure=MAA_WS_TIMEOUT;ENHANCE(!strict.screencap()&&strict.last_error().code==MAA_WS_TIMEOUT);strict.inactive();
        auto first=cache.screencap();ENHANCE(first);cache.failure=MAA_WS_TIMEOUT;auto replay=cache.screencap();
        ENHANCE(replay&&replay->sample.flags==MAA_WS_SAMPLE_REUSED&&replay->sample.sample_id==first->sample.sample_id&&replay->source.timestamp_ns==first->source.timestamp_ns);
        cache.failure=MAA_WS_CAPTURE_FAILED;ENHANCE(!cache.screencap());cache.failure=MAA_WS_TIMEOUT;ENHANCE(!cache.screencap());
        ENHANCE(first->image.data);
    }
    {
        struct ControlledWindowDup : DesktopDupWindowScreencap {
            bool timeout=false;
            explicit ControlledWindowDup(CaptureTarget t):DesktopDupWindowScreencap(t,true){}
            std::optional<Image> capture_image() override {
                if(timeout){error_=capture_error(MAA_WS_TIMEOUT,MAA_WS_STAGE_CAPTURE,"injected DXGI WAIT_TIMEOUT",method_,DXGI_ERROR_WAIT_TIMEOUT,"HRESULT");return {};}
                return DesktopDupWindowScreencap::capture_image();
            }
        } cache(target);
        auto first=cache.screencap();ENHANCE(first);cache.timeout=true;auto replay=cache.screencap();
        ENHANCE(replay&&replay->image.cols==320&&replay->image.rows==240&&replay->source.desktop_rect.x==first->source.desktop_rect.x&&replay->sample.sample_id==first->sample.sample_id);
        RECT r;GetWindowRect(h,&r);SetWindowPos(h,nullptr,r.left+7,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        const bool rejected=!cache.screencap();SetWindowPos(h,nullptr,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        ENHANCE(rejected&&!cache.screencap());cache.inactive();ENHANCE(first->image.data);
    }
    std::printf("display %u: E1 DXGI cache/default timeout, E2 focus, E3 freshness, E4 cancellation/deadlines OK\n",monitor.index);
#undef ENHANCE
    return true;
}
static bool soak_contracts(HWND h,double seconds) {
    const auto target=CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId());
    const bool framepool=(available_builtin_methods()&Method_FramePool)!=0;
    CaptureOptions options{target,framepool?Method_FramePool:Method_PrintWindow,
        (framepool?MAA_WS_POLICY_FRAMEPOOL_CACHE:0)|MAA_WS_POLICY_STRICT_BACKGROUND|MAA_WS_POLICY_DIAGNOSTICS};
    options.max_sample_age_ns=5000000000;options.connect_timeout_ns=5000000000;
    options.capture_timeout_ns=500000000;options.first_frame_timeout_ns=5000000000;
    ScreencapMgr manager(options);
    if(!manager.connect())return false;
    DWORD warm_handles=0,last_handles=0,warm_gdi=0,last_gdi=0;uint64_t count=0;
    const auto start=std::chrono::steady_clock::now();
    while(std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<seconds) {
        auto frame=manager.screencap();if(!frame||!colors(frame->image))return false;
        ++count;GetProcessHandleCount(GetCurrentProcess(),&last_handles);last_gdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        if(!warm_handles&&std::chrono::steady_clock::now()-start>std::chrono::seconds(3)){warm_handles=last_handles;warm_gdi=last_gdi;}
        pump();
    }
    manager.inactive();
    std::printf("soak %.1fs flags=%llu max_age_ns=%llu capture_budget_ns=%llu frames=%llu handles=%lu->%lu gdi=%lu->%lu\n",seconds,options.policy_flags,options.max_sample_age_ns,options.capture_timeout_ns,count,warm_handles,last_handles,warm_gdi,last_gdi);
    return count>0&&(!warm_handles||(last_handles<=warm_handles+16&&last_gdi<=warm_gdi+4));
}
int main(int argc,char** argv) {
    if (MaaWSGetAbiVersion()!=2) return 1;
    auto old=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct Dpi {DPI_AWARENESS_CONTEXT old;~Dpi(){if(old)SetThreadDpiAwarenessContext(old);}} dpi{old};
    MaaWSError error {};auto snapshot=MonitorSnapshot::create(error);if(!snapshot||snapshot->items.empty()){std::fprintf(stderr,"No desktop snapshot: %s\n",error.message);return 2;}
    WNDCLASSW cls {};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MwsColorFixture";RegisterClassW(&cls);
    std::filesystem::path output;double soak_seconds=0;unsigned focus_cycles=0;
    for(int i=1;i<argc;++i){
        const std::string arg=argv[i];
        if(arg=="--output"&&i+1<argc)output=argv[++i];
        else if(arg=="--soak-seconds"&&i+1<argc)soak_seconds=std::stod(argv[++i]);
        else if(arg=="--focus-cycles"&&i+1<argc)focus_cycles=unsigned(std::stoul(argv[++i]));
        else return 64;
    }
    if(!output.empty())std::filesystem::create_directories(output);
    for(const auto& monitor:snapshot->items) {
        HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"maa-capture pixel validation",WS_POPUP,
            int(monitor.desktop_rect.x)+40,int(monitor.desktop_rect.y)+40,320,240,nullptr,nullptr,cls.hInstance,nullptr);
        if(!h)return 3;
        struct Window {HWND h;~Window(){DestroyWindow(h);}} window{h};
        ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);pump();
        if(focus_cycles){if(!focus_trials(h,monitor,focus_cycles,output))return 17;continue;}
        if(!lifetime_contracts(h)){std::fprintf(stderr,"Window lifetime contracts failed\n");return 14;}
        if(!lease_contracts(h)){std::fprintf(stderr,"Shared lease contracts failed\n");return 11;}
        InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
        if(!cache_contracts(h)){std::fprintf(stderr,"Frame cache contracts failed\n");return 13;}
        if(!enhancement_contracts(h,monitor))return 16;
        InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
        // WGC produces change-driven frames. Change one unsampled pixel so the
        // test exercises fresh frames instead of accepting a cached screenshot.
        std::jthread pulse([h](std::stop_token stop) {
            unsigned value=0;
            while(!stop.stop_requested()) {
                HDC dc=GetDC(h);SetPixel(dc,0,0,RGB((++value)%255,0,0));ReleaseDC(h,dc);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
        if(!manager_contracts(h)){std::fprintf(stderr,"Manager transactions failed\n");return 12;}
        InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
        {
            ScreencapMgr manager({CaptureTarget::monitor(monitor.monitor_key)});
            if(!manager.connect()){std::fprintf(stderr,"monitor: %s\n",manager.last_error().message);return 4;}
            auto f=manager.screencap();
            if(!f || std::strcmp(f->source.source_key,monitor.monitor_key) || !colors(f->image.roi(40,40,320,240)))return 5;
            if(!output.empty())ppm(f->image.roi(40,40,320,240),output/("monitor-"+std::to_string(monitor.index)+".ppm"));
        }
        for(uint64_t method:{Method_GDI,Method_FramePool,Method_DXGI_DesktopDup_Window,Method_PrintWindow,Method_ScreenDC}) {
            if(!(available_builtin_methods()&method))continue;
            ScreencapMgr manager({CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS,GetCurrentProcessId()),method});
            if(!manager.connect()){std::fprintf(stderr,"%s: %s (%lld)\n",method_name(method),manager.last_error().message,manager.last_error().native_code);return 6;}
            auto frame=manager.screencap();
            if(!frame||frame->source.native_id!=reinterpret_cast<uintptr_t>(h)||!colors(frame->image)){std::fprintf(stderr,"pixel mismatch: %s\n",method_name(method));return 7;}
            std::printf("display %u, %s: native 320x240 BGR quadrants OK\n",monitor.index,method_name(method));
        }
        const auto style=GetWindowLongPtrW(h,GWL_EXSTYLE);
        for(int n=0;n<20;++n) {
            ShowWindow(h,SW_MINIMIZE);
            const auto pool=available_builtin_methods()&Method_FramePool;
            const uint64_t methods[]{Method_PrintWindow,pool?pool:Method_PrintWindow,Method_PrintWindow|pool,Method_None};
            ScreencapMgr manager({CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS),methods[n%4]});
            if(!manager.connect()||!manager.screencap()){std::fprintf(stderr,"cycle %d: %s (%u)\n",n,manager.last_error().message,manager.last_error().code);return 8;}
            manager.inactive();manager.inactive();
            if(!IsIconic(h)||GetWindowLongPtrW(h,GWL_EXSTYLE)!=style)return 9;
        }
        ShowWindow(h,SW_SHOWNOACTIVATE);
        if(soak_seconds>0&&monitor.index==snapshot->items.front().index){
            InvalidateRect(h,nullptr,TRUE);UpdateWindow(h);pump();
            if(!soak_contracts(h,soak_seconds))return 15;
        }
    }
    ScreencapMgr missing({CaptureTarget::monitor("win32:missing")});
    if(missing.connect()||missing.screencap())return 10;
    std::puts("Desktop source/color/geometry and 20-cycle state restoration verified");
}
