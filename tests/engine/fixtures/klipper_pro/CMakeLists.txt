# Phase 6.5 — Klipper-Pro E2E fixture staging.
#
# Runs `cargo build --release` on engine/plugins/klipper-pro/ at build time,
# then copies the resulting .so + plugin.json + moonraker_mock.py into
# ${CMAKE_BINARY_DIR}/tests/engine/fixtures/klipper_pro/ so the E2E test can
# discover it via TEST_FIXTURES_DIR.
#
# Cargo's output name follows the [package] name convention with `-` → `_`
# and a `lib` prefix on Linux/macOS:
#     klipper-pro  →  libklipper_pro.{so,dylib}
# We rename it to com.orcaslicer.klipper-pro.{so,dylib} during staging so it
# matches PluginManager's <plugin_id><platform_ext> resolution rule.

set(_KLIPPER_DIR    "${CMAKE_SOURCE_DIR}/engine/plugins/klipper-pro")
set(_STAGED_DIR     "${CMAKE_BINARY_DIR}/tests/engine/fixtures/klipper_pro")
set(_CARGO_TARGET   "${_KLIPPER_DIR}/target/release")

if (APPLE)
    set(_CARGO_OUT  "${_CARGO_TARGET}/libklipper_pro.dylib")
    set(_STAGED_LIB "${_STAGED_DIR}/com.orcaslicer.klipper-pro.dylib")
elseif (WIN32)
    set(_CARGO_OUT  "${_CARGO_TARGET}/klipper_pro.dll")
    set(_STAGED_LIB "${_STAGED_DIR}/com.orcaslicer.klipper-pro.dll")
else ()
    set(_CARGO_OUT  "${_CARGO_TARGET}/libklipper_pro.so")
    set(_STAGED_LIB "${_STAGED_DIR}/com.orcaslicer.klipper-pro.so")
endif ()

# Build the Rust crate. Cargo's own incremental build cache makes re-runs
# cheap when nothing has changed.
add_custom_command(
    OUTPUT  "${_CARGO_OUT}"
    COMMAND ${CMAKE_COMMAND} -E env
        "CARGO_TARGET_DIR=${_KLIPPER_DIR}/target"
        cargo build --release --manifest-path "${_KLIPPER_DIR}/Cargo.toml"
    DEPENDS
        "${_KLIPPER_DIR}/Cargo.toml"
        "${_KLIPPER_DIR}/src/lib.rs"
        "${_KLIPPER_DIR}/src/agent.rs"
        "${_KLIPPER_DIR}/src/moonraker.rs"
        "${_KLIPPER_DIR}/src/jobs.rs"
    COMMENT "Building klipper-pro plugin (cargo build --release)"
    VERBATIM)

add_custom_command(
    OUTPUT  "${_STAGED_LIB}"
            "${_STAGED_DIR}/manifest.json"
            "${_STAGED_DIR}/moonraker_mock.py"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${_STAGED_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${_CARGO_OUT}"                 "${_STAGED_LIB}"
    COMMAND ${CMAKE_COMMAND} -E copy "${_KLIPPER_DIR}/plugin.json"   "${_STAGED_DIR}/manifest.json"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/moonraker_mock.py"
                                     "${_STAGED_DIR}/moonraker_mock.py"
    DEPENDS
        "${_CARGO_OUT}"
        "${_KLIPPER_DIR}/plugin.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/moonraker_mock.py"
    COMMENT "Staging klipper-pro fixture into ${_STAGED_DIR}"
    VERBATIM)

add_custom_target(test_fixture_klipper_pro ALL
    DEPENDS
        "${_STAGED_LIB}"
        "${_STAGED_DIR}/manifest.json"
        "${_STAGED_DIR}/moonraker_mock.py")
