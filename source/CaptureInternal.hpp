#pragma once
#include "maa-capture/ScreencapBase.h"
#include <functional>
namespace mws::detail {
using MonitorProvider = std::function<std::optional<std::vector<MaaWSMonitorInfo>>(MaaWSError&)>;
std::optional<std::vector<MaaWSMonitorInfo>> platform_monitors(MaaWSError& error);
std::optional<MonitorSnapshot> snapshot_from(const MonitorProvider& provider, MaaWSError& error);
bool resolve_source(const CaptureTarget& target, MaaWSFrameInfo& info, uint64_t& process_identity, MaaWSError& error);
bool same_source(const MaaWSFrameInfo& before, const MaaWSFrameInfo& after, MaaWSError& error);
}
