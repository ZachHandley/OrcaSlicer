#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Pulling in the service headers here makes `#include "orca/Session.hpp"` the
// single entry point a consumer needs — they can call session.presets().X()
// without a second include. The headers are themselves cheap (forward-decls
// of libslic3r types + small POD vocabulary types). If a consumer wants the
// minimal surface they can include the specific service header directly.
#include "Presets.hpp"
#include "Project.hpp"
#include "Slicer.hpp"
#include "Export.hpp"
#include "Events.hpp"
#include "Result.hpp"

// Forward declarations of libslic3r types referenced by the migration-scaffold
// attach_* methods below. Phase 0.4a–0.6 use these to BORROW GUI/CLI-owned
// objects; Phase 1 collapses ownership into Session.
namespace Slic3r {
class PresetBundle;
class Model;
} // namespace Slic3r

namespace orca {

// Top-level entry point to the orca engine. Owns the four services and the
// event bus; all engine state lives behind this object. A single long-lived
// instance is the expected usage; multi-instance is supported but untested.
class Session {
public:
    static std::unique_ptr<Session> create();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&)                 = delete;
    Session& operator=(Session&&)      = delete;

    Presets&        presets();
    const Presets&  presets() const;

    Project&        project();
    const Project&  project() const;

    Slicer&         slicer();
    const Slicer&   slicer() const;

    Exporter&       exporter();
    const Exporter& exporter() const;

    Events&         events();
    const Events&   events() const;

    // ---------- Migration scaffold (Phase 0.4a–0.6) ----------
    //
    // During the incremental migration, the GUI/CLI continues to own the
    // PresetBundle and Model. Session BORROWS them via these setters; the
    // wrappers in presets()/project() then route into the borrowed objects
    // so rewritten call sites compile and run identically.
    //
    // Phase 1 collapses ownership into Session and removes these setters.
    void attach_preset_bundle(Slic3r::PresetBundle* bundle);
    void detach_preset_bundle();

    void attach_model(Slic3r::Model* model);
    void detach_model();

    // ---------- Plugin host (Phase 1) ----------
    //
    // The Session owns a PluginManager (loader) and a PluginRegistry (slot
    // dispatch table). Both are engine-internal C++ types; consumers drive
    // them through these Session methods or the C ABI in plugin_api.h.

    // Walks <plugins_dir>/<id>/manifest.json entries and loads each native
    // plugin found. Returns the number of plugins successfully loaded.
    std::size_t discover_and_load_plugins(const std::filesystem::path& plugins_dir);

    // Load a single plugin by directory. ORCA_OK on success.
    Result<void> load_plugin(const std::filesystem::path& plugin_dir);

    // Unload by id; ORCA_ERR_NOT_FOUND if not loaded.
    Result<void> unload_plugin(const std::string& plugin_id);

    void unload_all_plugins();

    std::vector<std::string> loaded_plugin_ids() const;
    bool                     is_plugin_loaded(const std::string& plugin_id) const;

    // Read-only count of registered slots across all kinds. Useful for tests.
    std::size_t registered_slot_count() const;

private:
    Session();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orca
