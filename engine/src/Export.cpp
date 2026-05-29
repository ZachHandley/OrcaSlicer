// Exporter — engine-side ExportService.
//
// Phase 0.4c. Synchronous G-code export of the Session's completed FFF Print.

#include "orca/Export.hpp"
#include "orca/Session.hpp"
#include "orca/Slicer.hpp"

#include <libslic3r/Print.hpp>
#include <libslic3r/GCode/GCodeProcessor.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace orca {

struct Exporter::Impl {
    Session*     session     = nullptr;
    bool         busy        = false;
    ExportHandle next_handle = 1;
};

Exporter::Exporter() : impl_(std::make_unique<Impl>()) {}
Exporter::~Exporter() = default;

void Exporter::bind_session(Session* session) { impl_->session = session; }

Result<ExportHandle> Exporter::export_gcode(ExportParams params) {
    if (!impl_->session)
        return err<ExportHandle>(ErrorCode::InvalidState, "Exporter: no Session bound");

    Slicer& slicer = impl_->session->slicer();
    if (slicer.is_busy())
        return err<ExportHandle>(ErrorCode::InvalidState, "Exporter: slice still running");

    Slic3r::Print* print = slicer.completed_fff_print();
    if (!print)
        return err<ExportHandle>(ErrorCode::InvalidState, "Exporter: no completed slice to export");

    Slic3r::GCodeProcessorResult* result = slicer.gcode_result();

    if (params.output_path.empty())
        return err<ExportHandle>(ErrorCode::InvalidArgument, "Exporter: empty output_path");

    std::string written;
    impl_->busy = true;
    try {
        written = print->export_gcode(params.output_path.string(), result, nullptr);
    } catch (std::exception& ex) {
        impl_->busy = false;
        return err<ExportHandle>(ErrorCode::IoError,
                                 std::string("Exporter: export_gcode threw: ") + ex.what());
    }
    impl_->busy = false;

    std::error_code       ec;
    std::filesystem::path final_path =
        written.empty() ? params.output_path : std::filesystem::path(written);
    auto sz = std::filesystem::file_size(final_path, ec);
    if (ec || sz == 0)
        return err<ExportHandle>(ErrorCode::IoError, "Exporter: export produced no/empty G-code");

    return ok<ExportHandle>(impl_->next_handle++);
}

void Exporter::cancel(ExportHandle /*handle*/) {}

bool Exporter::is_busy() const { return impl_->busy; }

} // namespace orca
