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
    Presets  presets;
    Project  project;
    Slicer   slicer;
    Exporter exporter;
    Events   events;
};

std::unique_ptr<Session> Session::create() {
    // Can't use std::make_unique because the ctor is private and friend is
    // limited to this class.
    return std::unique_ptr<Session>(new Session());
}

Session::Session() : impl_(std::make_unique<Impl>()) {}
Session::~Session() = default;

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

} // namespace orca
