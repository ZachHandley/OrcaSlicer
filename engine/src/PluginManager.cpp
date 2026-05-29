// PluginManager — Phase 1.2.1 skeleton.
//
// Discovers <plugins_dir>/<id>/manifest.json entries, dlopens the entry
// binary, resolves the three mandatory orca_plugin_* exports, runs the
// debug/release sanity check + ABI handshake, and tracks loaded plugins.
// Mirrors src/slic3r/Utils/BBLNetworkPlugin.cpp (load: 116-146,
// unload: 185-212, get_function: 314-328, debug check: 222-232).
//
// Scope: just enough to load and unload. Slot vtable dispatch and the
// real per-slot host calls (load_profile_pack, placeholder injection,
// register_printer_agent, raw event subscribers) are stubs returning
// ORCA_ERR_UNSUPPORTED until Phase 2 wires them.

#include "PluginManager.hpp"

#include "orca/Session.hpp"
#include "orca/c_api.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

namespace orca {

// ---------------------------------------------------------------------------
// Function-pointer typedefs for the three mandatory plugin exports.
// Names + signatures from engine/include/orca/plugin_api.h.
// ---------------------------------------------------------------------------
using orca_plugin_register_fn_t =
    orca_error_code_t (*)(uint32_t,
                          orca_plugin_registry_t*,
                          const orca_plugin_host_t*);
using orca_plugin_unregister_fn_t =
    void (*)(void);
using orca_plugin_check_debug_consistent_fn_t =
    int (*)(int);

// ---------------------------------------------------------------------------
// LoadedPlugin — per-plugin record. Owns the OS module handle and the
// resolved entry-point function pointers.
// ---------------------------------------------------------------------------
struct PluginManager::LoadedPlugin {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    uint64_t    permissions = 0;

#if defined(_WIN32)
    HMODULE handle = nullptr;
#else
    void* handle = nullptr;
#endif

    orca_plugin_register_fn_t              fn_register   = nullptr;
    orca_plugin_unregister_fn_t            fn_unregister = nullptr;
    orca_plugin_check_debug_consistent_fn_t fn_check_debug = nullptr;
};

// ---------------------------------------------------------------------------
// Impl — owns bound session/registry, the host vtable handed to every
// loaded plugin, and the map of loaded plugins. The host vtable itself
// dispatches via the static s_instance below; that's how the C-callable
// thunks find their way back to this PluginManager.
// ---------------------------------------------------------------------------
struct PluginManager::Impl {
    Session*        session  = nullptr;
    PluginRegistry* registry = nullptr;

    orca_plugin_host_t host_vtable{};

    mutable std::mutex mtx;

    std::unordered_map<std::string, std::unique_ptr<LoadedPlugin>> plugins;
    std::vector<std::string>                                       load_order;
};

// ---------------------------------------------------------------------------
// Static instance pointer for the C thunks. Set in bind_session and cleared
// in the destructor so the host vtable can be assembled without per-callback
// context. This is the same trick BBLNetworkPlugin uses for its globals.
// Only one PluginManager per engine is expected; if that ever changes the
// thunks need a different dispatch strategy.
// ---------------------------------------------------------------------------
namespace {
std::atomic<PluginManager::Impl*> s_instance{nullptr};

// =====================================================================
// Cross-platform dynamic-library helpers (mirrors BBLNetworkPlugin.cpp).
// =====================================================================

#if defined(_WIN32)
HMODULE dl_open(const std::filesystem::path& path)
{
    // Wide-char path through LoadLibraryW, matching BBLNetworkPlugin's
    // MultiByteToWideChar + LoadLibrary pattern (line 116-134).
    std::wstring wpath = path.wstring();
    return ::LoadLibraryW(wpath.c_str());
}
void dl_close(HMODULE h) { if (h) ::FreeLibrary(h); }
void* dl_sym(HMODULE h, const char* name)
{
    return reinterpret_cast<void*>(::GetProcAddress(h, name));
}
std::string dl_last_error()
{
    DWORD err = ::GetLastError();
    std::ostringstream oss;
    oss << "LoadLibrary/GetProcAddress failed, code=" << err;
    return oss.str();
}
constexpr const char* kPlatformExt = ".dll";
#else
void* dl_open(const std::filesystem::path& path)
{
    return ::dlopen(path.c_str(), RTLD_LAZY);
}
void dl_close(void* h) { if (h) ::dlclose(h); }
void* dl_sym(void* h, const char* name) { return ::dlsym(h, name); }
std::string dl_last_error()
{
    const char* e = ::dlerror();
    return e ? std::string{e} : std::string{"unknown dlopen/dlsym error"};
}
#  if defined(__APPLE__)
constexpr const char* kPlatformExt = ".dylib";
#  else
constexpr const char* kPlatformExt = ".so";
#  endif
#endif

// =====================================================================
// Permission-name → bit lookup. Mirrors the ORCA_PERM_* bits declared
// in plugin_api.h. Unknown names are silently ignored (logged at debug
// level by the caller).
// =====================================================================
uint64_t permission_bit(const std::string& name)
{
    if (name == "network")           return ORCA_PERM_NETWORK;
    if (name == "filesystem.read")   return ORCA_PERM_FILESYSTEM_READ;
    if (name == "filesystem.write")  return ORCA_PERM_FILESYSTEM_WRITE;
    if (name == "settings.read")     return ORCA_PERM_SETTINGS_READ;
    if (name == "settings.write")    return ORCA_PERM_SETTINGS_WRITE;
    if (name == "profiles.install")  return ORCA_PERM_PROFILES_INSTALL;
    if (name == "device.control")    return ORCA_PERM_DEVICE_CONTROL;
    if (name == "slice.intercept")   return ORCA_PERM_SLICE_INTERCEPT;
    if (name == "gcode.modify")      return ORCA_PERM_GCODE_MODIFY;
    if (name == "ui.attach")         return ORCA_PERM_UI_ATTACH;
    if (name == "events.raw")        return ORCA_PERM_EVENTS_RAW;
    return 0;
}

// =====================================================================
// Simple stderr logger. BOOST_LOG_TRIVIAL is fine too, but the engine
// keeps boost-logging out of its core surface so std::cerr is enough
// for Phase 1. Pairs with the host's log thunk below.
// =====================================================================
void log_line(int level, const char* msg)
{
    static const char* kLevels[] = {"trace", "debug", "info", "warn", "error"};
    const char* lvl = (level >= 0 && level <= 4) ? kLevels[level] : "?";
    std::cerr << "[orca][plugin][" << lvl << "] "
              << (msg ? msg : "") << '\n';
}

// =====================================================================
// Host vtable thunks. All callbacks dispatch through s_instance because
// the C ABI has no per-callback context. They forward into the engine's
// existing C ABI (c_api.cpp) for the bits already implemented and return
// ORCA_ERR_UNSUPPORTED for the bits Phase 2 owns.
// =====================================================================

extern "C" void host_log_thunk(int level, const char* msg)
{
    log_line(level, msg);
}

extern "C" orca_session_t* host_session_thunk(void)
{
    auto* impl = s_instance.load(std::memory_order_acquire);
    if (!impl || !impl->session) return nullptr;
    return reinterpret_cast<orca_session_t*>(impl->session);
}

extern "C" orca_subscription_id_t host_events_subscribe_thunk(
    orca_event_kind_t     kind,
    orca_event_callback_t cb,
    void*                 user_data)
{
    auto* impl = s_instance.load(std::memory_order_acquire);
    if (!impl || !impl->session) return 0;
    orca_events_t* ev = orca_session_events(
        reinterpret_cast<orca_session_t*>(impl->session));
    if (!ev) return 0;
    return orca_events_subscribe(ev, kind, cb, user_data);
}

extern "C" void host_events_unsubscribe_thunk(orca_subscription_id_t id)
{
    auto* impl = s_instance.load(std::memory_order_acquire);
    if (!impl || !impl->session) return;
    orca_events_t* ev = orca_session_events(
        reinterpret_cast<orca_session_t*>(impl->session));
    if (ev) orca_events_unsubscribe(ev, id);
}

extern "C" orca_subscription_id_t host_events_subscribe_raw_thunk(
    uint64_t                  /*kind_mask*/,
    orca_raw_event_callback_t /*cb*/,
    void*                     /*user_data*/)
{
    // Phase 2: raw escape-hatch subscriber. Until then signal "no subscription".
    return 0;
}

extern "C" const orca_presets_t* host_presets_const_thunk(void)
{
    auto* impl = s_instance.load(std::memory_order_acquire);
    if (!impl || !impl->session) return nullptr;
    return orca_session_presets(
        reinterpret_cast<orca_session_t*>(impl->session));
}

extern "C" orca_presets_t* host_presets_mut_thunk(void)
{
    auto* impl = s_instance.load(std::memory_order_acquire);
    if (!impl || !impl->session) return nullptr;
    return orca_session_presets(
        reinterpret_cast<orca_session_t*>(impl->session));
}

extern "C" orca_error_code_t host_load_profile_pack_thunk(const char* /*dir*/)
{
    return ORCA_ERR_UNSUPPORTED; // Phase 2.
}

extern "C" orca_error_code_t host_placeholder_set_string_thunk(
    const char* /*name*/, const char* /*value*/)
{
    return ORCA_ERR_UNSUPPORTED; // Phase 2.3.
}

extern "C" orca_error_code_t host_placeholder_set_int_thunk(
    const char* /*name*/, int64_t /*value*/)
{
    return ORCA_ERR_UNSUPPORTED; // Phase 2.3.
}

extern "C" orca_error_code_t host_placeholder_set_float_thunk(
    const char* /*name*/, double /*value*/)
{
    return ORCA_ERR_UNSUPPORTED; // Phase 2.3.
}

extern "C" orca_error_code_t host_register_printer_agent_thunk(
    const char* /*agent_id*/, const void* /*vtable*/, void* /*self*/)
{
    return ORCA_ERR_UNSUPPORTED; // Phase 2.4.
}

extern "C" void host_string_free_thunk(const char* s)
{
    // The engine always allocates owned UTF-8 buffers with new char[N].
    // Keep both ends on the same allocator so cross-DLL frees stay safe.
    delete[] s;
}

} // namespace

// ---------------------------------------------------------------------------
// PluginManager — ctor / dtor.
// ---------------------------------------------------------------------------
PluginManager::PluginManager() : impl_(std::make_unique<Impl>()) {}

PluginManager::~PluginManager()
{
    try { unload_all(); } catch (...) { /* destructor swallows */ }

    // Clear the static instance only if it still points to us — if a future
    // multi-manager arrangement reassigned it, leave that arrangement alone.
    PluginManager::Impl* expected = impl_.get();
    s_instance.compare_exchange_strong(expected, nullptr,
                                       std::memory_order_acq_rel);
}

// ---------------------------------------------------------------------------
// bind_session — wire the session pointer, assemble the host vtable, and
// publish ourselves as the dispatch target for the C thunks. Plugins
// loaded after this call receive the same &host_vtable pointer.
// ---------------------------------------------------------------------------
void PluginManager::bind_session(Session* session, PluginRegistry* registry)
{
    std::lock_guard<std::mutex> guard(impl_->mtx);

    impl_->session  = session;
    impl_->registry = registry;

    auto& h = impl_->host_vtable;
    h.struct_size               = static_cast<uint32_t>(sizeof(orca_plugin_host_t));
    h.log                       = &host_log_thunk;
    h.session                   = &host_session_thunk;
    h.events_subscribe          = &host_events_subscribe_thunk;
    h.events_unsubscribe        = &host_events_unsubscribe_thunk;
    h.events_subscribe_raw      = &host_events_subscribe_raw_thunk;
    h.presets_const             = &host_presets_const_thunk;
    h.presets_mut               = &host_presets_mut_thunk;
    h.load_profile_pack         = &host_load_profile_pack_thunk;
    h.placeholder_set_string    = &host_placeholder_set_string_thunk;
    h.placeholder_set_int       = &host_placeholder_set_int_thunk;
    h.placeholder_set_float     = &host_placeholder_set_float_thunk;
    h.register_printer_agent    = &host_register_printer_agent_thunk;
    h.string_free               = &host_string_free_thunk;

    s_instance.store(impl_.get(), std::memory_order_release);
}

// ---------------------------------------------------------------------------
// discover_and_load — walk plugins_dir for subdirectories containing a
// manifest.json and try each one. Continues past individual failures.
// ---------------------------------------------------------------------------
std::size_t PluginManager::discover_and_load(const std::filesystem::path& plugins_dir)
{
    std::error_code ec;
    if (!std::filesystem::is_directory(plugins_dir, ec))
        return 0;

    std::size_t loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(plugins_dir, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        const auto manifest_path = entry.path() / "manifest.json";
        if (!std::filesystem::exists(manifest_path)) continue;
        if (load_plugin(entry.path()) == ORCA_OK)
            ++loaded;
    }
    return loaded;
}

// ---------------------------------------------------------------------------
// load_plugin — the full load sequence:
//   1. parse manifest.json
//   2. dedupe against already-loaded plugins
//   3. resolve entry binary path
//   4. dlopen / LoadLibrary
//   5. resolve the three mandatory exports
//   6. debug/release sanity check
//   7. ABI handshake via orca_plugin_register
// Any failure unwinds the OS handle before returning.
// ---------------------------------------------------------------------------
orca_error_code_t PluginManager::load_plugin(const std::filesystem::path& plugin_dir)
{
    // --- 1. parse manifest.json -----------------------------------------
    const auto manifest_path = plugin_dir / "manifest.json";
    std::ifstream ifs(manifest_path);
    if (!ifs.is_open()) {
        log_line(4, ("manifest not found: " + manifest_path.string()).c_str());
        return ORCA_ERR_NOT_FOUND;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception& ex) {
        log_line(4, (std::string{"manifest parse failed: "} + ex.what()).c_str());
        return ORCA_ERR_PARSE;
    }

    if (!j.is_object() || !j.contains("id") || !j["id"].is_string()) {
        log_line(4, "manifest missing required string field 'id'");
        return ORCA_ERR_PARSE;
    }

    auto plugin = std::make_unique<LoadedPlugin>();
    plugin->id          = j["id"].get<std::string>();
    plugin->name        = j.value("name",        std::string{});
    plugin->version     = j.value("version",     std::string{});
    plugin->author      = j.value("author",      std::string{});
    plugin->description = j.value("description", std::string{});

    if (j.contains("permissions") && j["permissions"].is_array()) {
        for (const auto& p : j["permissions"]) {
            if (!p.is_string()) continue;
            plugin->permissions |= permission_bit(p.get<std::string>());
        }
    }

    // --- 2. dedupe -------------------------------------------------------
    {
        std::lock_guard<std::mutex> guard(impl_->mtx);
        if (impl_->plugins.find(plugin->id) != impl_->plugins.end()) {
            log_line(3, ("plugin already loaded: " + plugin->id).c_str());
            return ORCA_ERR_ALREADY_EXISTS;
        }
    }

    // --- 3. resolve entry binary path -----------------------------------
    // Phase 1 convention: <plugin_dir>/<id><platform_ext>. Versioned
    // filenames (lib<id>_<ver>.<ext>) arrive in Phase 3.1.
    const auto entry_path = plugin_dir / (plugin->id + kPlatformExt);
    if (!std::filesystem::exists(entry_path)) {
        log_line(4, ("entry binary not found: " + entry_path.string()).c_str());
        return ORCA_ERR_NOT_FOUND;
    }

    // --- 4. dlopen ------------------------------------------------------
    plugin->handle = dl_open(entry_path);
    if (!plugin->handle) {
        log_line(4, ("dlopen failed for " + entry_path.string() + ": " + dl_last_error()).c_str());
        return ORCA_ERR_IO;
    }

    // --- 5. resolve the three mandatory exports -------------------------
    plugin->fn_register = reinterpret_cast<orca_plugin_register_fn_t>(
        dl_sym(plugin->handle, "orca_plugin_register"));
    plugin->fn_unregister = reinterpret_cast<orca_plugin_unregister_fn_t>(
        dl_sym(plugin->handle, "orca_plugin_unregister"));
    plugin->fn_check_debug = reinterpret_cast<orca_plugin_check_debug_consistent_fn_t>(
        dl_sym(plugin->handle, "orca_plugin_check_debug_consistent"));

    if (!plugin->fn_register || !plugin->fn_unregister || !plugin->fn_check_debug) {
        log_line(4, ("missing mandatory orca_plugin_* export in " + entry_path.string()).c_str());
        dl_close(plugin->handle);
        return ORCA_ERR_PARSE;
    }

    // --- 6. debug/release consistency check -----------------------------
    // Matches BBLNetworkPlugin.cpp:222-232 — zero means consistent. The
    // engine reports its own status: NDEBUG defined → release (0), else
    // debug (1). Any non-zero return aborts the load.
#if defined(NDEBUG)
    const int engine_is_debug = 0;
#else
    const int engine_is_debug = 1;
#endif
    if (plugin->fn_check_debug(engine_is_debug) != 0) {
        log_line(3, ("debug/release mismatch for " + plugin->id + "; aborting load").c_str());
        dl_close(plugin->handle);
        return ORCA_ERR_UNSUPPORTED;
    }

    // --- 7. ABI handshake -----------------------------------------------
    // PluginRegistry's concrete type lives in Phase 1.2.2; for now we
    // forward whatever pointer the orchestrator handed bind_session as
    // the opaque registry argument.
    auto* registry_opaque =
        reinterpret_cast<orca_plugin_registry_t*>(impl_->registry);

    const orca_error_code_t rc =
        plugin->fn_register(ORCA_PLUGIN_ABI_VERSION,
                            registry_opaque,
                            &impl_->host_vtable);
    if (rc != ORCA_OK) {
        log_line(4, ("orca_plugin_register returned non-OK for " + plugin->id).c_str());
        dl_close(plugin->handle);
        return rc;
    }

    // --- success: stash + log -------------------------------------------
    std::string id_copy = plugin->id;
    std::string version_copy = plugin->version;
    {
        std::lock_guard<std::mutex> guard(impl_->mtx);
        impl_->load_order.push_back(id_copy);
        impl_->plugins.emplace(id_copy, std::move(plugin));
    }
    log_line(2, ("loaded plugin " + id_copy + " v" + version_copy).c_str());
    return ORCA_OK;
}

// ---------------------------------------------------------------------------
// unload_plugin — call orca_plugin_unregister, dlclose, drop from maps.
// ---------------------------------------------------------------------------
orca_error_code_t PluginManager::unload_plugin(const std::string& plugin_id)
{
    std::unique_ptr<LoadedPlugin> taken;
    {
        std::lock_guard<std::mutex> guard(impl_->mtx);
        auto it = impl_->plugins.find(plugin_id);
        if (it == impl_->plugins.end())
            return ORCA_ERR_NOT_FOUND;
        taken = std::move(it->second);
        impl_->plugins.erase(it);

        auto lit = std::find(impl_->load_order.begin(), impl_->load_order.end(), plugin_id);
        if (lit != impl_->load_order.end())
            impl_->load_order.erase(lit);
    }

    if (taken->fn_unregister) {
        try {
            taken->fn_unregister();
        } catch (...) {
            log_line(4, ("orca_plugin_unregister threw for " + plugin_id).c_str());
        }
    }
    dl_close(taken->handle);
    taken->handle = nullptr;

    log_line(2, ("unloaded plugin " + plugin_id).c_str());
    return ORCA_OK;
}

// ---------------------------------------------------------------------------
// unload_all — iterate in reverse load order so plugins that registered
// late (potentially depending on earlier ones) tear down first.
// ---------------------------------------------------------------------------
void PluginManager::unload_all()
{
    std::vector<std::string> order_snapshot;
    {
        std::lock_guard<std::mutex> guard(impl_->mtx);
        order_snapshot = impl_->load_order;
    }
    for (auto it = order_snapshot.rbegin(); it != order_snapshot.rend(); ++it)
        unload_plugin(*it);
}

// ---------------------------------------------------------------------------
// Read-only inspection.
// ---------------------------------------------------------------------------
std::vector<std::string> PluginManager::loaded_plugin_ids() const
{
    std::lock_guard<std::mutex> guard(impl_->mtx);
    return impl_->load_order;
}

bool PluginManager::is_loaded(const std::string& plugin_id) const
{
    std::lock_guard<std::mutex> guard(impl_->mtx);
    return impl_->plugins.find(plugin_id) != impl_->plugins.end();
}

} // namespace orca
