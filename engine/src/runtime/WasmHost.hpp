// Phase 3.2.2 — WasmHost: wraps a single shared wasmtime_engine_t and exposes
// a load_wasm(path) -> WasmInstance entry point so callers (PluginManager,
// the wasm fixture tests, etc.) don't depend on wasmtime.h directly.
//
// Design note: this header deliberately uses opaque void* state so consumers
// only need <orca/Result.hpp> + standard library. The wasmtime C API headers
// only get pulled into WasmHost.cpp / WasmPlugin.cpp where the runtime work
// actually happens.
#pragma once

#include "orca/Result.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>

namespace orca::wasm {

/// RAII wrapper for one instantiated wasm module. Owns the wasmtime
/// store + module + instance triple. Pimpl so this header doesn't drag
/// wasmtime.h into every consumer.
class WasmInstance {
public:
    struct Impl;
    explicit WasmInstance(std::unique_ptr<Impl> impl) noexcept;
    ~WasmInstance();

    WasmInstance(const WasmInstance&)            = delete;
    WasmInstance& operator=(const WasmInstance&) = delete;
    WasmInstance(WasmInstance&&)                 = delete;
    WasmInstance& operator=(WasmInstance&&)      = delete;

    /// Compiled wasm bytecode size in bytes (informational/telemetry).
    std::size_t module_size() const noexcept;

private:
    std::unique_ptr<Impl> impl_;
};

/// One per process. Owns the wasmtime_engine_t — expensive to construct,
/// cheap to share. Stores are created per loaded plugin so guest memory
/// stays isolated, but they all hand back to the same engine.
class WasmHost {
public:
    WasmHost();
    ~WasmHost();

    WasmHost(const WasmHost&)            = delete;
    WasmHost& operator=(const WasmHost&) = delete;

    /// Read `path`, compile it as a wasm module, instantiate with an empty
    /// import set, and return the resulting instance. The full host import
    /// table (capabilities, gated by permissions) lands in Phase 3.2.3.
    Result<std::unique_ptr<WasmInstance>>
    load_wasm(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orca::wasm
