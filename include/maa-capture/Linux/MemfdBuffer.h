#pragma once

// ============================================================================
// memfd 共享内存缓冲
//
// 提取自 MaaFramework source/MaaLinuxControlUnit/Base/MemfdBuffer.h/.cpp
// （命名空间 MAA_CTRL_UNIT_NS → MWS_NS，去掉 MaaUtils 依赖）。
//
// 用 memfd_create 分配一块共享内存并 mmap，fd 交给 wl_shm_create_pool
// 作为 wlr-screencopy 的截图目标缓冲。
// ============================================================================

#if !defined(__linux__)
#error "MemfdBuffer is Linux-only"
#endif

#include "maa-capture/Common.h"

MWS_NS_BEGIN

class MemfdBuffer
{
public:
    MemfdBuffer(int size);
    MemfdBuffer(int width, int height, int stride);
    ~MemfdBuffer();

    MemfdBuffer(const MemfdBuffer&) = delete;
    MemfdBuffer& operator=(const MemfdBuffer&) = delete;

    void* ptr() const { return ptr_; }

    int size() const { return size_; }

    int fd() const { return fd_; }

    bool available() const { return available_; }

private:
    bool create_buffer();
    bool available_ = false;
    int fd_ = -1;
    int size_ = 0;
    void* ptr_ = nullptr;
};

MWS_NS_END
