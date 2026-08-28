#include "maa-capture/Linux/WaylandHelper.h"

#include "maa-capture/Logger.h"

#define DEFAULT_DELETER(TypeName, DeleteFunction)                   \
    namespace std                                                   \
    {                                                               \
    void default_delete<TypeName>::operator()(TypeName* ptr)        \
    {                                                               \
        LogDebug << "Delete protocol " #TypeName << VAR_VOIDP(ptr); \
        DeleteFunction(ptr);                                        \
        }                                                           \
    }

DEFAULT_DELETER(wl_display, wl_display_disconnect)
DEFAULT_DELETER(wl_registry, wl_registry_destroy)
DEFAULT_DELETER(wl_output, wl_output_destroy)
DEFAULT_DELETER(wl_shm, wl_shm_destroy)
DEFAULT_DELETER(wl_shm_pool, wl_shm_pool_destroy)
DEFAULT_DELETER(wl_buffer, wl_buffer_destroy)
DEFAULT_DELETER(zwlr_screencopy_manager_v1, zwlr_screencopy_manager_v1_destroy)
DEFAULT_DELETER(zwlr_screencopy_frame_v1, zwlr_screencopy_frame_v1_destroy)

#undef DEFAULT_DELETER
