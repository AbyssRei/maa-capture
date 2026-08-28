// ============================================================================
// 示例：通过「外部继承接口」实现自定义截图单元
//
// 演示：
//   1. 继承 mws::ScreencapBase 实现自己的截图单元
//      （此处用纯色合成图代替真实截屏，实际可包装私有 API / 远程服务等）
//   2. 通过 ScreencapMgr::register_unit 注册，与内置 6 种方式一起参与测速
//   3. 只注册自定义单元（不启用内置方式），由自定义单元独立完成截图
// ============================================================================

#include <iostream>
#include <memory>

#include "maa-capture/Logger.h"
#include "maa-capture/Image.hpp"
#include "maa-capture/ScreencapMgr.h"

using namespace mws;

// ---- 外部继承接口实现 ----
class SolidColorScreencap : public ScreencapBase
{
public:
    SolidColorScreencap(Scalar color, int width, int height)
        : color_(color)
        , width_(width)
        , height_(height)
    {
    }

    virtual std::optional<Image> screencap() override
    {
        Image mat(height_, width_, 3);
        mat.set_to(color_); // BGR: 例如 Scalar(0, 0, 255) = 红色
        return mat;
    }

    virtual void inactive() override
    {
        LogInfo << "SolidColorScreencap::inactive called";
    }

private:
    Scalar color_;
    int width_;
    int height_;
};

// 先缩放显示信息
static void print_image_info(const char* tag, const Image& img)
{
    std::cout << "[" << tag << "] " << img.cols << "x" << img.rows << " channels=" << img.channels() << std::endl;
}

int main()
{
    // 1) 与内置方式一起测速：默认 builtin_methods = Method_All，
    //    调用 connect() 前注册的自定义单元也会参与测速
    {
        ScreencapMgr mgr;
        mgr.register_unit("solid-color-red", std::make_shared<SolidColorScreencap>(Scalar(0, 0, 255), 640, 480));
        mgr.register_unit("solid-color-green", std::make_shared<SolidColorScreencap>(Scalar(0, 255, 0), 640, 480));

        if (!mgr.connect()) {
            std::cerr << "connect failed (all methods unavailable)" << std::endl;
            return 1;
        }

        std::cout << "selected unit: " << mgr.selected_unit_name() << std::endl;

        Image image;
        if (!mgr.screencap(image)) {
            std::cerr << "screencap failed" << std::endl;
            return 1;
        }
        print_image_info("mixed", image);

        mgr.inactive();
    }

    // 2) 纯自定义：只注册外部实现的单元，不启用任何内置方式
    {
        ScreencapMgr mgr;
        mgr.set_builtin_methods(Method_None);
        mgr.register_unit("custom-only", std::make_shared<SolidColorScreencap>(Scalar(255, 0, 0), 320, 240));

        if (!mgr.connect()) {
            std::cerr << "connect failed" << std::endl;
            return 1;
        }

        std::cout << "selected unit: " << mgr.selected_unit_name() << std::endl;

        Image image;
        if (!mgr.screencap(image)) {
            std::cerr << "screencap failed" << std::endl;
            return 1;
        }
        print_image_info("custom-only", image);
    }

    return 0;
}