// Phase 3.2.4 — WasmPlugin lifecycle implementation.

#include "WasmPlugin.hpp"
#include "WasmHost.hpp"
#include "WasmImports.hpp"

#include "orca/plugin_api.h"

#include <cstdio>
#include <utility>

namespace orca::wasm {

namespace {

constexpr const char* kCheckDebugFn   = "orca_plugin_check_debug_consistent";
constexpr const char* kRegisterFn     = "orca_plugin_register";
constexpr const char* kUnregisterFn   = "orca_plugin_unregister";

// Engine-side "is debug" sentinel passed to the guest's check function.
// Mirrors the int flag the native loader uses (PluginManager.cpp:592).
constexpr std::int32_t kEngineIsDebug =
#if !defined(NDEBUG)
    1;
#else
    0;
#endif

ErrorCode map_orca_rc(std::int32_t rc) {
    switch (rc) {
        case ORCA_OK:                    return ErrorCode::Unknown;  // unreachable
        case ORCA_ERR_INVALID_ARGUMENT:  return ErrorCode::InvalidArgument;
        case ORCA_ERR_NOT_FOUND:         return ErrorCode::NotFound;
        case ORCA_ERR_ALREADY_EXISTS:    return ErrorCode::AlreadyExists;
        case ORCA_ERR_IO:                return ErrorCode::IoError;
        case ORCA_ERR_PARSE:             return ErrorCode::ParseError;
        case ORCA_ERR_CANCELLED:         return ErrorCode::Cancelled;
        case ORCA_ERR_UNSUPPORTED:       return ErrorCode::Unsupported;
        case ORCA_ERR_PERMISSION_DENIED: return ErrorCode::PermissionDenied;
        default:                         return ErrorCode::Unknown;
    }
}

} // namespace

struct WasmPlugin::Impl {
    ImportContext                 ctx;
    std::string                   version;
    std::unique_ptr<WasmInstance> instance;
};

WasmPlugin::WasmPlugin(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

WasmPlugin::~WasmPlugin() {
    if (!impl_ || !impl_->instance) return;
    if (impl_->instance->has_export(kUnregisterFn)) {
        auto rc = impl_->instance->call_void(kUnregisterFn);
        if (!rc.ok()) {
            std::fprintf(stderr,
                "[orca][wasm][%s] %s failed: %s\n",
                impl_->ctx.plugin_id.c_str(),
                kUnregisterFn,
                rc.error().message.c_str());
        }
    }
}

Result<std::unique_ptr<WasmPlugin>>
WasmPlugin::load(WasmHost& host, const Manifest& manifest, Session* session) {
    using R = Result<std::unique_ptr<WasmPlugin>>;

    if (manifest.id.empty())
        return R{Error{ErrorCode::InvalidArgument,
                       "WasmPlugin::load: empty manifest id"}};
    if (manifest.wasm_path.empty())
        return R{Error{ErrorCode::InvalidArgument,
                       "WasmPlugin::load: empty wasm_path for " + manifest.id}};

    auto impl = std::make_unique<Impl>();
    impl->ctx.plugin_id   = manifest.id;
    impl->ctx.permissions = manifest.permissions;
    impl->ctx.session     = session;
    impl->version         = manifest.version;

    auto inst_res = host.load_wasm(manifest.wasm_path, impl->ctx);
    if (!inst_res.ok())
        return R{inst_res.error()};
    impl->instance = std::move(inst_res).value();

    // 1. Debug/release consistency. The export is optional — guests
    //    targeting a single ABI flavor can skip it.
    if (impl->instance->has_export(kCheckDebugFn)) {
        auto rc = impl->instance->call_i32_to_i32(kCheckDebugFn, kEngineIsDebug);
        if (!rc.ok()) return R{rc.error()};
        if (rc.value() != 0)
            return R{Error{ErrorCode::Unsupported,
                           manifest.id +
                           ": orca_plugin_check_debug_consistent reported mismatch"}};
    }

    // 2. orca_plugin_register — mandatory. The registry handle is opaque
    //    to the guest; today we pass 0, which means "no slot registration
    //    surface available." A future revision will pass a real handle
    //    once wasm-side slot imports exist.
    if (!impl->instance->has_export(kRegisterFn))
        return R{Error{ErrorCode::NotFound,
                       manifest.id + ": missing required export " + kRegisterFn}};

    auto reg = impl->instance->call_i32_i64_to_i32(
        kRegisterFn,
        static_cast<std::int32_t>(ORCA_PLUGIN_ABI_VERSION),
        /*registry_handle*/ 0);
    if (!reg.ok()) return R{reg.error()};
    if (reg.value() != ORCA_OK)
        return R{Error{map_orca_rc(reg.value()),
                       manifest.id + ": " + kRegisterFn + " returned " +
                       std::to_string(reg.value())}};

    return ok(std::unique_ptr<WasmPlugin>(new WasmPlugin(std::move(impl))));
}

const std::string& WasmPlugin::id() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->ctx.plugin_id : empty;
}

const std::string& WasmPlugin::version() const noexcept {
    static const std::string empty;
    return impl_ ? impl_->version : empty;
}

std::uint64_t WasmPlugin::permissions() const noexcept {
    return impl_ ? impl_->ctx.permissions : 0;
}

} // namespace orca::wasm
