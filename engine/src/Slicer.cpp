// Slicer — engine-side PrintService.
//
// Phase 0.4a placeholder so Session can own it. The real implementation
// (BackgroundSlicingProcess wrapper, async slice handles, progress) lands
// in Phase 0.4c.

#include "orca/Slicer.hpp"

namespace orca {

struct Slicer::Impl {
    bool busy = false;
};

Slicer::Slicer() : impl_(std::make_unique<Impl>()) {}
Slicer::~Slicer() = default;

Result<SliceHandle> Slicer::request_slice(SliceParams /*params*/) {
    return err<SliceHandle>(ErrorCode::NotImplemented, "Slicer::request_slice — Phase 0.4c");
}

void Slicer::cancel(SliceHandle /*handle*/) {}

SliceStatus Slicer::status(SliceHandle /*handle*/) const {
    return SliceStatus{};
}

bool Slicer::is_busy() const { return impl_->busy; }
void Slicer::cancel_all() {}

} // namespace orca
