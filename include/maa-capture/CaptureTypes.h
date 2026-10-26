#pragma once
#include <stdint.h>
/* ABI 2: fixed-width fields and NUL-terminated UTF-8 arrays. Capacities include
 * NUL. Initialize struct_size to sizeof(struct); reserved fields must be zero. */
#define MAA_WS_ABI_VERSION 2u
#define MAA_WS_KEY_CAPACITY 512
#define MAA_WS_METHOD_NONE 0ULL
#define MAA_WS_METHOD_AUTO 0ULL
#define MAA_WS_METHOD_GDI 1ULL
#define MAA_WS_METHOD_FRAMEPOOL (1ULL << 1)
#define MAA_WS_METHOD_DXGI_DESKTOP_DUP (1ULL << 2)
#define MAA_WS_METHOD_DXGI_DESKTOP_DUP_WINDOW (1ULL << 3)
#define MAA_WS_METHOD_PRINTWINDOW (1ULL << 4)
#define MAA_WS_METHOD_SCREENDC (1ULL << 5)
#define MAA_WS_METHOD_PIPEWIRE (1ULL << 6)
#define MAA_WS_METHOD_WLR (1ULL << 7)
#define MAA_WS_METHOD_SCREENCAPTUREKIT (1ULL << 8)
#define MAA_WS_PLATFORM_WINDOWS 1u
#define MAA_WS_PLATFORM_MACOS 2u
#define MAA_WS_PLATFORM_LINUX 4u
#define MAA_WS_TARGET_MONITOR 1u
#define MAA_WS_TARGET_WINDOW 2u
#define MAA_WS_TARGET_PIPEWIRE 3u
#define MAA_WS_TARGET_WLR 4u
#define MAA_WS_TARGET_CUSTOM 5u
#define MAA_WS_SCOPE_SCREEN 1u
#define MAA_WS_SCOPE_WINDOW 2u
#define MAA_WS_SCOPE_SOURCE 4u
#define MAA_WS_SCOPE_UNKNOWN 8u
#define MAA_WS_IDENTITY_PERSISTENT 1u
#define MAA_WS_IDENTITY_SESSION 2u
#define MAA_WS_IDENTITY_AMBIGUOUS 3u
#define MAA_WS_OK 0u
#define MAA_WS_INVALID_ARGUMENT 1u
#define MAA_WS_INVALID_STATE 2u
#define MAA_WS_UNSUPPORTED_OPTION 3u
#define MAA_WS_METHOD_UNAVAILABLE 4u
#define MAA_WS_TARGET_NOT_FOUND 5u
#define MAA_WS_TARGET_AMBIGUOUS 6u
#define MAA_WS_SOURCE_MISMATCH 7u
#define MAA_WS_LAYOUT_CHANGED 8u
#define MAA_WS_TARGET_CHANGED 9u
#define MAA_WS_PERMISSION_DENIED 10u
#define MAA_WS_TIMEOUT 11u
#define MAA_WS_UNSUPPORTED_GEOMETRY 12u
#define MAA_WS_CAPTURE_FAILED 13u
#define MAA_WS_INTERNAL_ERROR 14u
#define MAA_WS_CANCELLED 15u
#define MAA_WS_DEADLINE_EXCEEDED 16u
#define MAA_WS_STALE_FRAME 17u
#define MAA_WS_STAGE_CREATE 1u
#define MAA_WS_STAGE_ENUMERATE 2u
#define MAA_WS_STAGE_CONNECT 3u
#define MAA_WS_STAGE_CAPTURE 4u
#define MAA_WS_STAGE_CALLBACK 5u
#define MAA_WS_UNIT_PHYSICAL_PIXEL 1u
#define MAA_WS_UNIT_POINT 2u
#define MAA_WS_UNIT_PROVIDER 3u
#define MAA_WS_SPACE_DESKTOP 1u
#define MAA_WS_SPACE_SOURCE 2u
#define MAA_WS_AXIS_X_RIGHT_Y_DOWN 1u
#define MAA_WS_PIXELS_NATIVE 1u
#define MAA_WS_PIXELS_PROVIDER 2u
#define MAA_WS_VERIFY_PROVIDER 1u
#define MAA_WS_VERIFY_SAMPLED 2u
#define MAA_WS_VALID_IDENTITY 1u
#define MAA_WS_VALID_GEOMETRY 2u
#define MAA_WS_VALID_SCALE 4u
#define MAA_WS_VALID_ROTATION 8u
#define MAA_WS_VALID_MONITOR 16u
#define MAA_WS_LIMIT_NONATOMIC 1u
#define MAA_WS_LIMIT_VISIBLE_DESKTOP 2u
#define MAA_WS_LIMIT_PROVIDER_IDENTITY 4u
#define MAA_WS_FEATURE_FRAMES 1ULL
#define MAA_WS_FEATURE_MONITORS 2ULL
#define MAA_WS_FEATURE_STRICT_SOURCE 4ULL
#define MAA_WS_FEATURE_SAMPLE_INFO 8ULL
#define MAA_WS_FEATURE_FRAMEPOOL_CACHE 16ULL
#define MAA_WS_FEATURE_ADVANCED_POLICY 32ULL
#define MAA_WS_FEATURE_METHOD_BEHAVIOR 64ULL
#define MAA_WS_FEATURE_CANCELLATION 128ULL
#define MAA_WS_FEATURE_DXGI_CACHE 256ULL
#define MAA_WS_FEATURE_STRICT_BACKGROUND 512ULL
#define MAA_WS_FEATURE_FRAME_FRESHNESS 1024ULL
#define MAA_WS_FEATURE_DEADLINES 2048ULL
#define MAA_WS_POLICY_FRAMEPOOL_CACHE 1ULL
#define MAA_WS_POLICY_DXGI_CACHE 2ULL
#define MAA_WS_POLICY_STRICT_BACKGROUND 4ULL
#define MAA_WS_POLICY_FRESH_ONLY 8ULL
#define MAA_WS_POLICY_DIAGNOSTICS 16ULL
#define MAA_WS_BEHAVIOR_NO_ACTIVATE 1ULL
#define MAA_WS_BEHAVIOR_MAY_ACTIVATE 2ULL
#define MAA_WS_BEHAVIOR_PSEUDO_MINIMIZE 4ULL
#define MAA_WS_BEHAVIOR_COOPERATIVE_WAIT 8ULL
#define MAA_WS_BEHAVIOR_UNCANCELLABLE_CALLS 16ULL
/* These describe sampling/delivery, never a target's rendering timestamp. */
#define MAA_WS_SAMPLE_UNKNOWN 0u
#define MAA_WS_SAMPLE_NEW 1u
#define MAA_WS_SAMPLE_REUSED 2u
#define MAA_WS_SAMPLE_CONNECT_PENDING 4u
typedef uint8_t MaaWSBool;
typedef uint64_t MaaWSMethod;
typedef struct MaaWSRect_ { double x, y, width, height; } MaaWSRect;
typedef struct MaaWSError_ {
    uint32_t struct_size, code, stage, reserved;
    uint64_t method;
    int64_t native_code;
    char native_domain[64], message[256];
} MaaWSError;
typedef struct MaaWSCaptureTarget_ {
    uint32_t struct_size, type, platform, reserved;
    uint64_t window_id, process_id;
    int32_t pipewire_fd;
    uint32_t node_id, output_index, scope;
    char source_key[MAA_WS_KEY_CAPACITY], socket_name[256];
} MaaWSCaptureTarget;
typedef struct MaaWSCaptureOptions_ {
    uint32_t struct_size, reserved;
    MaaWSCaptureTarget target;
    uint64_t methods; /* zero = automatic selection within target scope */
} MaaWSCaptureOptions;
/* Additive ABI 2 extension. Existing structures and entry semantics stay fixed. */
typedef struct MaaWSCapturePolicy_ {
    uint32_t struct_size, reserved;
    uint64_t flags;
} MaaWSCapturePolicy;
/* Durations are monotonic nanoseconds; zero disables a limit. This independent
 * structure does not change MaaWSCapturePolicy or the old creation entries. */
typedef struct MaaWSAdvancedCapturePolicy_ {
    uint32_t struct_size, reserved;
    uint64_t flags, max_sample_age_ns, connect_timeout_ns, capture_timeout_ns, first_frame_timeout_ns;
} MaaWSAdvancedCapturePolicy;
typedef struct MaaWSMethodBehavior_ {
    uint32_t struct_size, reserved;
    uint64_t method, flags;
} MaaWSMethodBehavior;
typedef struct MaaWSFrameSampleInfo_ {
    uint32_t struct_size, flags;
    uint64_t sample_id, sampled_at_ns, delivered_at_ns, session_id;
} MaaWSFrameSampleInfo;
typedef struct MaaWSMonitorInfo_ {
    uint32_t struct_size, index, identity_scope, primary;
    uint64_t layout_token, native_id;
    MaaWSRect desktop_rect;
    uint32_t coordinate_unit, coordinate_space, axis, valid_fields;
    uint32_t pixel_width, pixel_height, rotation, reserved;
    double scale_x, scale_y;
    char monitor_key[MAA_WS_KEY_CAPACITY], name[256];
} MaaWSMonitorInfo;
typedef struct MaaWSFrameInfo_ {
    uint32_t struct_size, target_type;
    uint64_t frame_id, timestamp_ns, method;
    uint32_t scope, valid_fields, verification, coordinate_unit;
    uint32_t coordinate_space, axis, pixel_semantics, limitations;
    uint64_t layout_before, layout_after, native_id, process_id;
    MaaWSRect desktop_rect, content_rect;
    double scale_x, scale_y;
    uint32_t pixel_width, pixel_height, rotation, reserved;
    char source_key[MAA_WS_KEY_CAPACITY];
    MaaWSMonitorInfo monitor; /* valid only with MAA_WS_VALID_MONITOR */
} MaaWSFrameInfo;
typedef struct MaaWSImageView_ {
    uint32_t struct_size, width, height, channels;
    uint64_t step, data_size;
    const uint8_t* data;
} MaaWSImageView;
typedef struct MaaWSFrameInput_ {
    uint32_t struct_size, reserved;
    MaaWSImageView image;
    MaaWSFrameInfo info;
} MaaWSFrameInput;
typedef struct MaaWSMethodCapabilities_ {
    uint32_t struct_size, platforms;
    uint64_t method;
    uint32_t scopes, available, flags, reserved;
} MaaWSMethodCapabilities;
typedef struct MaaWSRuntimeInfo_ {
    uint32_t struct_size, abi_version;
    uint64_t features;
    char version[32], commit[48], platform[16], architecture[16];
} MaaWSRuntimeInfo;
