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
#include "WasmImports.hpp"

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

Result<std::int32_t>
WasmInstance::call_i64_to_i32(const std::string& fn_name, std::int64_t arg) {
    using R = Result<std::int32_t>;
    if (!impl_ || !impl_->store)
        return R{Error{ErrorCode::InvalidState, "instance not initialized"}};

    wasmtime_context_t* ctx = wasmtime_store_context(impl_->store);

    wasmtime_extern_t fn_ext;
    if (!wasmtime_instance_export_get(ctx, &impl_->instance,
                                      fn_name.data(), fn_name.size(),
                                      &fn_ext))
        return R{Error{ErrorCode::NotFound,
                       "wasm export not found: " + fn_name}};

    if (fn_ext.kind != WASMTIME_EXTERN_FUNC) {
        wasmtime_extern_delete(&fn_ext);
        return R{Error{ErrorCode::NotFound,
                       "wasm export is not a function: " + fn_name}};
    }

    wasmtime_val_t in;
    in.kind = WASMTIME_I64;
    in.of.i64 = arg;

    wasmtime_val_t out;
    out.kind = WASMTIME_I32;
    out.of.i32 = 0;

    wasm_trap_t* trap = nullptr;
    wasmtime_error_t* err = wasmtime_func_call(
        ctx, &fn_ext.of.func, &in, 1, &out, 1, &trap);
    wasmtime_extern_delete(&fn_ext);

    if (err) {
        const auto msg = take_error_message(err);
        return R{Error{ErrorCode::Unknown,
                       "wasmtime_func_call(" + fn_name + ") failed: " + msg}};
    }
    if (trap) {
        wasm_byte_vec_t tmsg;
        wasm_trap_message(trap, &tmsg);
        std::string msg(tmsg.data, tmsg.size);
        wasm_byte_vec_delete(&tmsg);
        wasm_trap_delete(trap);
        return R{Error{ErrorCode::ParseError,
                       "wasm trap in " + fn_name + ": " + msg}};
    }
    if (out.kind != WASMTIME_I32)
        return R{Error{ErrorCode::Unknown,
                       "wasm export returned non-i32 from " + fn_name}};
    return ok(out.of.i32);
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
WasmHost::load_wasm(const std::filesystem::path& path, ImportContext& ictx) {
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

    // 2. create store; attach the ImportContext so every host_* callback
    //    can find it via wasmtime_context_get_data. We do NOT take ownership
    //    of ictx — caller guarantees lifetime — so the finalizer is null.
    inst_impl->store = wasmtime_store_new(impl_->engine, &ictx, /*finalizer*/ nullptr);
    if (!inst_impl->store)
        return R{Error{ErrorCode::Unknown, "wasmtime_store_new returned null"}};

    // 3. install host imports on a fresh linker and instantiate.
    wasmtime_linker_t* linker = wasmtime_linker_new(impl_->engine);
    if (!linker)
        return R{Error{ErrorCode::Unknown, "wasmtime_linker_new returned null"}};

    auto imports_res = install_imports(linker, impl_->engine);
    if (!imports_res.ok()) {
        wasmtime_linker_delete(linker);
        return R{imports_res.error()};
    }

    {
        wasmtime_context_t* ctx = wasmtime_store_context(inst_impl->store);
        wasm_trap_t* trap = nullptr;
        wasmtime_error_t* err = wasmtime_linker_instantiate(
            linker, ctx, inst_impl->module, &inst_impl->instance, &trap);
        wasmtime_linker_delete(linker);

        if (err) {
            const auto msg = take_error_message(err);
            return R{Error{ErrorCode::Unknown,
                           "wasmtime_linker_instantiate failed: " + msg}};
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
