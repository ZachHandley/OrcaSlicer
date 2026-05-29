#pragma once

#include <memory>

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

private:
    Session();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orca
