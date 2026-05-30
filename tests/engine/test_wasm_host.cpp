// Phase 3.2.2 — WasmHost smoke test.
//
// Validates that WasmHost can be constructed (engine initialized) and can
// load a minimal valid wasm module. Uses the smallest legal wasm binary —
// 8 bytes: magic "\0asm" + version 1 — which has no sections, no imports,
// and no exports. That's enough to round-trip wasmtime_module_new +
// wasmtime_instance_new with an empty import set, which is exactly what
// load_wasm does today. Real fixture plugins (with an _init export and
// host imports) land in Phase 3.4 alongside the WIT bindings.

#include <catch2/catch_all.hpp>

#include "runtime/WasmHost.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path write_empty_wasm(const std::string& name) {
    namespace fs = std::filesystem;
    const fs::path out = fs::path{TEST_TMP_DIR} / name;
    fs::create_directories(out.parent_path());

    // 8-byte minimal wasm: magic + version 1.
    constexpr std::array<std::uint8_t, 8> kEmptyWasm{
        0x00, 0x61, 0x73, 0x6d,   // \0asm
        0x01, 0x00, 0x00, 0x00,   // version 1
    };
    std::ofstream out_s(out, std::ios::binary | std::ios::trunc);
    out_s.write(reinterpret_cast<const char*>(kEmptyWasm.data()),
                static_cast<std::streamsize>(kEmptyWasm.size()));
    return out;
}

} // namespace

TEST_CASE("Phase 3.2.2 — WasmHost loads a minimal empty wasm module",
          "[wasm][host]") {
    orca::wasm::WasmHost host;

    SECTION("load_wasm succeeds on a valid empty module") {
        const auto path = write_empty_wasm("empty_module.wasm");

        auto res = host.load_wasm(path);
        REQUIRE(res.ok());

        const auto& instance = res.value();
        REQUIRE(instance != nullptr);
        CHECK(instance->module_size() == 8);
    }

    SECTION("load_wasm returns NotFound for a missing file") {
        namespace fs = std::filesystem;
        const fs::path missing = fs::path{TEST_TMP_DIR} / "does_not_exist.wasm";
        std::error_code ec;
        fs::remove(missing, ec);

        auto res = host.load_wasm(missing);
        REQUIRE_FALSE(res.ok());
        CHECK(res.error().code == orca::ErrorCode::NotFound);
    }

    SECTION("load_wasm returns ParseError for garbage bytes") {
        namespace fs = std::filesystem;
        const fs::path junk = fs::path{TEST_TMP_DIR} / "garbage.wasm";
        std::ofstream out_s(junk, std::ios::binary | std::ios::trunc);
        const char garbage[] = "this is not a wasm module";
        out_s.write(garbage, sizeof(garbage) - 1);
        out_s.close();

        auto res = host.load_wasm(junk);
        REQUIRE_FALSE(res.ok());
        CHECK(res.error().code == orca::ErrorCode::ParseError);
    }
}
