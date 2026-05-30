// Phase 3.2.2 — WasmHost implementation.
//
// Owns a single shared wasm_engine_t (one per process). Each load_wasm call
// produces a fresh store + module + instance triple so guest memory remains
// isolated between loaded wasm plugins. The full host import table — and the
// permission gates around each import — arrives in Phase 3.2.3 (WasmImports);
// for now load_wasm instantiates with an empty import set, which is enough to
// validate the dep wiring, the wasmtime ABI handshake, and the path through
// PluginManager once kind=="wasm" dispatch lands in Phase 3.2.6.

#include "WasmHost.hpp"

#include <wasm.h>
#include <wasmtime.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace orca::wasm {

namespace {

/// Pull a wasmtime_error_t's message into a std::string and free the error.
/// All wasmtime entry points that return wasmtime_error_t* transfer ownership
/// of the error to the caller on failure.
std::string take_error_message(wasmtime_error_t* err) {
    if (!err) return {};
    wasm_byte_vec_t msg;
    wasmtime_error_message(err, &msg);
    std::string out(msg.data, msg.size);
    wasm_byte_vec_delete(&msg);
    wasmtime_error_delete(err);
    return out;
}

Result<std::vector<std::uint8_t>> read_file_bytes(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec))
        return Result<std::vector<std::uint8_t>>{
            Error{ErrorCode::NotFound, "wasm file not found: " + path.string()}};

    std::ifstream in(path, std::ios::binary);
    if (!in)
        return Result<std::vector<std::uint8_t>>{
            Error{ErrorCode::IoError, "cannot open wasm file: " + path.string()}};

    std::vector<std::uint8_t> bytes(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    return ok(std::move(bytes));
}

} // namespace

// ---------------------------------------------------------------------------
// WasmInstance — owns one store + module + instance, all bound to a single
// engine. Destruction order matters: instance lives inside the store, so we
// delete the module first, then the store (which sinks the instance with it).
// ---------------------------------------------------------------------------
struct WasmInstance::Impl {
    wasmtime_store_t*   store     = nullptr;
    wasmtime_module_t*  module    = nullptr;
    wasmtime_instance_t instance{}; // by-value; lifetime tied to store

    std::size_t module_bytes = 0;

    ~Impl() {
        if (module) wasmtime_module_delete(module);
        if (store)  wasmtime_store_delete(store);
    }
};

WasmInstance::WasmInstance(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

WasmInstance::~WasmInstance() = default;

std::size_t WasmInstance::module_size() const noexcept {
    return impl_ ? impl_->module_bytes : 0;
}

// ---------------------------------------------------------------------------
// WasmHost — owns the shared engine. The engine is expensive to construct
// (sets up Cranelift / Winch + Wasmtime config) so we only build it once.
// ---------------------------------------------------------------------------
struct WasmHost::Impl {
    wasm_engine_t* engine = nullptr;

    ~Impl() {
        if (engine) wasm_engine_delete(engine);
    }
};

WasmHost::WasmHost() : impl_(std::make_unique<Impl>()) {
    impl_->engine = wasm_engine_new();
}

WasmHost::~WasmHost() = default;

Result<std::unique_ptr<WasmInstance>>
WasmHost::load_wasm(const std::filesystem::path& path) {
    using R = Result<std::unique_ptr<WasmInstance>>;

    if (!impl_ || !impl_->engine)
        return R{Error{ErrorCode::Unknown, "wasm engine not initialized"}};

    auto bytes_res = read_file_bytes(path);
    if (!bytes_res.ok())
        return R{bytes_res.error()};

    const auto& bytes = bytes_res.value();
    if (bytes.empty())
        return R{Error{ErrorCode::ParseError, "empty wasm file: " + path.string()}};

    auto inst_impl = std::make_unique<WasmInstance::Impl>();
    inst_impl->module_bytes = bytes.size();

    // 1. compile module
    {
        wasmtime_module_t* mod = nullptr;
        wasmtime_error_t* err = wasmtime_module_new(
            impl_->engine, bytes.data(), bytes.size(), &mod);
        if (err) {
            const auto msg = take_error_message(err);
            return R{Error{ErrorCode::ParseError,
                           "wasmtime_module_new failed for " + path.string() +
                           ": " + msg}};
        }
        inst_impl->module = mod;
    }

    // 2. create store
    inst_impl->store = wasmtime_store_new(impl_->engine, nullptr, nullptr);
    if (!inst_impl->store)
        return R{Error{ErrorCode::Unknown, "wasmtime_store_new returned null"}};

    // 3. instantiate with empty imports (Phase 3.2.3 wires real imports)
    {
        wasmtime_context_t* ctx = wasmtime_store_context(inst_impl->store);
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* err = wasmtime_instance_new(
            ctx, inst_impl->module,
            /*imports*/  nullptr,
            /*n_imports*/ 0,
            &inst_impl->instance,
            &trap);
        if (err) {
            const auto msg = take_error_message(err);
            return R{Error{ErrorCode::Unknown,
                           "wasmtime_instance_new failed: " + msg}};
        }
        if (trap) {
            wasm_byte_vec_t tmsg;
            wasm_trap_message(trap, &tmsg);
            std::string msg(tmsg.data, tmsg.size);
            wasm_byte_vec_delete(&tmsg);
            wasm_trap_delete(trap);
            return R{Error{ErrorCode::Unknown,
                           "wasm trap during instantiation: " + msg}};
        }
    }

    return ok(std::make_unique<WasmInstance>(std::move(inst_impl)));
}

} // namespace orca::wasm
