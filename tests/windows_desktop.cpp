// Opt-in real desktop verification. Paints only its own solid-color test window.
#include "maa-capture/ScreencapMgr.h"
#include "maa-capture/MaaWSCustomScreencap.h"
#include "maa-capture/Windows/SafeWindows.hpp"
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
using namespace mws;
static void paint(HDC dc) {
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
static void pump() {
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(150);
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
int main(int argc,char** argv) {
    if (MaaWSGetAbiVersion()!=2) return 1;
    auto old=SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    struct Dpi {DPI_AWARENESS_CONTEXT old;~Dpi(){if(old)SetThreadDpiAwarenessContext(old);}} dpi{old};
    MaaWSError error {};auto snapshot=MonitorSnapshot::create(error);if(!snapshot||snapshot->items.empty()){std::fprintf(stderr,"No desktop snapshot: %s\n",error.message);return 2;}
    WNDCLASSW cls {};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"MwsColorFixture";RegisterClassW(&cls);
    const std::filesystem::path output=argc==3&&std::string(argv[1])=="--output"?argv[2]:"";
    if(!output.empty())std::filesystem::create_directories(output);
    for(const auto& monitor:snapshot->items) {
        HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"maa-capture pixel validation",WS_POPUP,
            int(monitor.desktop_rect.x)+40,int(monitor.desktop_rect.y)+40,320,240,nullptr,nullptr,cls.hInstance,nullptr);
        if(!h)return 3;
        struct Window {HWND h;~Window(){DestroyWindow(h);}} window{h};
        ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);pump();
        // WGC produces change-driven frames. Change one unsampled pixel so the
        // test exercises fresh frames instead of accepting a cached screenshot.
        std::jthread pulse([h](std::stop_token stop) {
            unsigned value=0;
            while(!stop.stop_requested()) {
                HDC dc=GetDC(h);SetPixel(dc,0,0,RGB((++value)%255,0,0));ReleaseDC(h,dc);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
            }
        });
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
            ScreencapMgr manager({CaptureTarget::window(reinterpret_cast<uintptr_t>(h),MAA_WS_PLATFORM_WINDOWS),Method_PrintWindow});
            if(!manager.connect()||!manager.screencap()){std::fprintf(stderr,"cycle %d: %s (%u)\n",n,manager.last_error().message,manager.last_error().code);return 8;}
            manager.inactive();manager.inactive();
            if(!IsIconic(h)||GetWindowLongPtrW(h,GWL_EXSTYLE)!=style)return 9;
        }
        ShowWindow(h,SW_SHOWNOACTIVATE);
    }
    ScreencapMgr missing({CaptureTarget::monitor("win32:missing")});
    if(missing.connect()||missing.screencap())return 10;
    std::puts("Desktop source/color/geometry and 20-cycle state restoration verified");
}
