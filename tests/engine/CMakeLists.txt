get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)

# Build the fixture plugin used by test_plugin_loader.cpp first, so the engine
# tests target can depend on it.
add_subdirectory(fixtures/loader_plugin)

add_executable(${_TEST_NAME}_tests
    ${_TEST_NAME}_tests.cpp
    test_session.cpp
    test_events_bus.cpp
    test_c_abi_events_bridge.cpp
    test_plugin_loader.cpp
    test_plugin_registry.cpp
    test_pipeline_events.cpp
    )

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r Catch2::Catch2WithMain)
target_include_directories(${_TEST_NAME}_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    # engine-internal headers (PluginRegistry, etc.) — needed by test_plugin_registry.cpp
    ${CMAKE_SOURCE_DIR}/engine/src
    )

# Where the fixture plugin's binary + manifest.json land after build. The loader
# test reads $TEST_FIXTURES_DIR/loader_plugin/{<id>.so, manifest.json}. Match
# the staged dir set in fixtures/loader_plugin/CMakeLists.txt.
target_compile_definitions(${_TEST_NAME}_tests PRIVATE
    "TEST_FIXTURES_DIR=\"${CMAKE_BINARY_DIR}/tests/engine/fixtures\""
    )

# Ensure the fixture binary + its manifest copy step run before the tests build.
add_dependencies(${_TEST_NAME}_tests test_fixture_loader_plugin)

set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

orcaslicer_copy_test_dlls()

catch_discover_tests(${_TEST_NAME}_tests)
