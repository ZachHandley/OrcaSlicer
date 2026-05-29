// Session — top-level engine entry point.
//
// Owns the four services + the event bus. Borrows GUI/CLI-owned PresetBundle
// and Model via attach_*() during the Phase 0.4a–0.6 migration; Phase 1
// collapses ownership in here.

#include "orca/Session.hpp"

#include "orca/Events.hpp"
#include "orca/Export.hpp"
#include "orca/Presets.hpp"
#include "orca/Project.hpp"
#include "orca/Slicer.hpp"

#include "PluginManager.hpp"
#include "PluginRegistry.hpp"

namespace orca {

// Project::Impl carries the borrowed Slic3r::Model* — we reach in via a
// trivial friend-equivalent helper so Session can wire attach_model. The
// helper is a private member of Project (added inline below as a free
// function in this TU), keeping the bridge code local to Session.cpp.
// We declare the body here as a friend-style backdoor; Project.hpp does not
// need to grow a public setter.

namespace detail {
// Trampolines defined alongside the service impls — see Presets.cpp / Project.cpp
// for the bundle/model borrow accessors. Session wires through them.
} // namespace detail

struct Session::Impl {
    Presets        presets;
    Project        project;
    Slicer         slicer;
    Exporter       exporter;
    Events         events;
    PluginRegistry registry;
    PluginManager  manager;
};

std::unique_ptr<Session> Session::create() {
    // Can't use std::make_unique because the ctor is private and friend is
    // limited to this class.
    return std::unique_ptr<Session>(new Session());
}

Session::Session() : impl_(std::make_unique<Impl>()) {
    impl_->slicer.bind_session(this);
    impl_->exporter.bind_session(this);
    impl_->manager.bind_session(this, &impl_->registry);
}
Session::~Session() {
    // Unload plugins BEFORE the rest of Session::Impl tears down so plugin
    // destructors can still reach the bus / presets / etc. through the host
    // vtable. If we let unique_ptr destruction order run naturally the manager
    // would unload after `events` is gone.
    impl_->manager.unload_all();
}

Presets&       Session::presets()       { return impl_->presets; }
const Presets& Session::presets() const { return impl_->presets; }

Project&       Session::project()       { return impl_->project; }
const Project& Session::project() const { return impl_->project; }

Slicer&        Session::slicer()        { return impl_->slicer; }
const Slicer&  Session::slicer() const  { return impl_->slicer; }

Exporter&       Session::exporter()       { return impl_->exporter; }
const Exporter& Session::exporter() const { return impl_->exporter; }

Events&        Session::events()        { return impl_->events; }
const Events&  Session::events() const  { return impl_->events; }

void Session::attach_preset_bundle(Slic3r::PresetBundle* bundle) {
    impl_->presets.attach_bundle(bundle);
}

void Session::detach_preset_bundle() {
    impl_->presets.detach_bundle();
}

void Session::attach_model(Slic3r::Model* model) {
    impl_->project.attach_model(model);
}

void Session::detach_model() {
    impl_->project.detach_model();
}

// ---------- Plugin host (Phase 1) ----------

std::size_t Session::discover_and_load_plugins(const std::filesystem::path& plugins_dir) {
    return impl_->manager.discover_and_load(plugins_dir);
}

Result<void> Session::load_plugin(const std::filesystem::path& plugin_dir) {
    orca_error_code_t rc = impl_->manager.load_plugin(plugin_dir);
    if (rc == ORCA_OK) return ok();
    // Map the C ABI code back to a C++ Result. The detailed message is on the
    // PluginManager side (logged); the Result here carries only the code.
    ErrorCode code = ErrorCode::Unknown;
    switch (rc) {
        case ORCA_ERR_INVALID_ARGUMENT: code = ErrorCode::InvalidArgument; break;
        case ORCA_ERR_NOT_FOUND:        code = ErrorCode::NotFound;        break;
        case ORCA_ERR_ALREADY_EXISTS:   code = ErrorCode::AlreadyExists;   break;
        case ORCA_ERR_IO:               code = ErrorCode::IoError;         break;
        case ORCA_ERR_PARSE:            code = ErrorCode::ParseError;      break;
        case ORCA_ERR_UNSUPPORTED:      code = ErrorCode::Unsupported;     break;
        default:                        code = ErrorCode::Unknown;         break;
    }
    return Result<void>{Error{code, "Session::load_plugin failed"}};
}

Result<void> Session::unload_plugin(const std::string& plugin_id) {
    orca_error_code_t rc = impl_->manager.unload_plugin(plugin_id);
    if (rc == ORCA_OK) return ok();
    return Result<void>{Error{
        rc == ORCA_ERR_NOT_FOUND ? ErrorCode::NotFound : ErrorCode::Unknown,
        "Session::unload_plugin failed"}};
}

void Session::unload_all_plugins() {
    impl_->manager.unload_all();
}

std::vector<std::string> Session::loaded_plugin_ids() const {
    return impl_->manager.loaded_plugin_ids();
}

bool Session::is_plugin_loaded(const std::string& plugin_id) const {
    return impl_->manager.is_loaded(plugin_id);
}

std::size_t Session::registered_slot_count() const {
    return impl_->registry.slot_count();
}

} // namespace orca
