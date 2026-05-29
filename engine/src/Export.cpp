// Exporter — engine-side ExportService.
//
// Phase 0.4a placeholder. Real impl lands in Phase 0.4c.

#include "orca/Export.hpp"

namespace orca {

struct Exporter::Impl {
    bool busy = false;
};

Exporter::Exporter() : impl_(std::make_unique<Impl>()) {}
Exporter::~Exporter() = default;

Result<ExportHandle> Exporter::export_gcode(ExportParams /*params*/) {
    return err<ExportHandle>(ErrorCode::NotImplemented, "Exporter::export_gcode — Phase 0.4c");
}

void Exporter::cancel(ExportHandle /*handle*/) {}
bool Exporter::is_busy() const { return impl_->busy; }

} // namespace orca
