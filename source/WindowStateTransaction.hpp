#pragma once
// Private transaction. Fake operations test partial failures without a desktop.
#include "maa-capture/CaptureTypes.hpp"
namespace mws::detail {
struct LayeredAttributes {
    uint32_t color = 0, flags = 0;
    uint8_t alpha = 255;
    bool operator==(const LayeredAttributes&) const = default;
};
struct WindowStateOps {
    virtual ~WindowStateOps() = default;
    virtual bool valid() = 0;
    virtual bool read_style(uint64_t&) = 0;
    virtual bool write_style(uint64_t) = 0;
    virtual bool read_layered(LayeredAttributes&) = 0;
    virtual bool write_layered(const LayeredAttributes&) = 0;
    virtual bool show(bool minimize) = 0;
    virtual bool user_restored() = 0;
    virtual bool conflict() = 0;
};
class WindowStateTransaction {
public:
    WindowStateTransaction(uint64_t layered, uint64_t transparent, uint32_t alpha_flag)
        : mask_(layered | transparent), layered_(layered), invisible_{0, alpha_flag, 0} {}
    bool modified() const { return modified_; }
    bool logical_minimize() const { return modified_ && minimize_intent_; }
    bool apply(WindowStateOps& ops) {
        if (modified_) return true;
        if (!ops.valid() || !ops.read_style(original_style_)) return false;
        had_layered_ = (original_style_ & layered_) != 0;
        if (had_layered_ && !ops.read_layered(original_attributes_)) return false;
        added_ = mask_ & ~original_style_;
        if (!ops.valid() || !ops.write_style(original_style_ | mask_)) return false;
        modified_ = true; minimize_intent_ = true;
        if (!ops.valid() || !ops.write_layered(invisible_)) {
            restore(ops, false); return false;
        }
        alpha_written_ = true;
        if (!ops.valid() || !ops.show(false)) { restore(ops, true); return false; }
        return true;
    }
    bool observe(WindowStateOps& ops) {
        if (!modified_) return true;
        if (!ops.valid()) { abandon(); return false; }
        if (ops.user_restored()) { minimize_intent_ = false; return restore(ops, false); }
        return true;
    }
    bool restore(WindowStateOps& ops, bool minimize) {
        if (!modified_) return true;
        if (!ops.valid()) { abandon(); return false; }
        uint64_t current = 0;
        if (!ops.read_style(current)) return false;
        LayeredAttributes attributes;
        if (alpha_written_ && (current & layered_)) {
            if (!ops.read_layered(attributes)) return false;
            if (!(attributes == invisible_)) {
                minimize_intent_ = false; return ops.conflict();
            }
        }
        if ((current & mask_) != mask_) minimize_intent_ = false;
        if (alpha_written_ && had_layered_ && (current & layered_)) {
            if (!ops.valid() || !ops.write_layered(original_attributes_)) return false;
            alpha_written_ = false;
        }
        // Preserve unrelated bits changed by the window owner.
        if (!ops.valid() || !ops.write_style(current & ~added_)) return false;
        if (minimize && minimize_intent_ && (!ops.valid() || !ops.show(true))) return false;
        abandon(); return true;
    }
    void abandon() { modified_ = alpha_written_ = minimize_intent_ = false; }
private:
    uint64_t mask_, layered_, original_style_ = 0, added_ = 0;
    LayeredAttributes invisible_, original_attributes_;
    bool modified_ = false, alpha_written_ = false, had_layered_ = false, minimize_intent_ = false;
};
}
