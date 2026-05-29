#ifndef ORCA_PLUGIN_API_H
#define ORCA_PLUGIN_API_H

/*
 * Engine <-> plugin C ABI for the orca engine.
 *
 * This header is the loader-side counterpart to c_api.h. A plugin shared
 * library includes BOTH headers:
 *   - c_api.h     — the surface the plugin DRIVES the engine through.
 *   - plugin_api.h — the surface the engine LOADS the plugin through.
 *
 * Boundary rules (same as c_api.h):
 *   - POD only across the boundary. No C++ types, no exceptions across the
 *     ABI line. Header compiles cleanly as both C11 and C++17.
 *   - Strings are UTF-8 const char*, borrowed unless documented otherwise.
 *   - Handles are opaque struct pointers; the engine owns their lifetime.
 *
 * Every plugin shared library MUST export the three symbols declared at the
 * bottom of this file: orca_plugin_register, orca_plugin_unregister, and
 * orca_plugin_check_debug_consistent. The engine resolves them by name via
 * dlsym / GetProcAddress. The pattern mirrors the production loader in
 * src/slic3r/Utils/BBLNetworkPlugin.cpp:537-637 (vtable assembly) and
 * src/slic3r/Utils/BBLNetworkPlugin.cpp:222-232 (debug/release sanity check).
 */

#include "orca/c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Cross-platform symbol-export decoration. Matches the visibility model used
 * by the engine itself and by BBLNetworkPlugin's exported entry points. */
#if defined(_WIN32)
#  define ORCA_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define ORCA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/* ============ ABI version ============ */

/* The engine refuses to load a plugin whose abi_version argument to
 * orca_plugin_register does not equal ORCA_PLUGIN_ABI_VERSION. Bump this
 * on any breaking change to the layouts, function signatures, or semantics
 * defined in this header. */
#define ORCA_PLUGIN_ABI_VERSION 1u

/* ============ Opaque handles ============ */

/* Registry: opaque object the plugin uses, during orca_plugin_register, to
 * declare its manifest and add slot implementations. */
typedef struct orca_plugin_registry orca_plugin_registry_t;

/* Host: opaque tag for the host vtable struct defined below. The plugin
 * receives a pointer to an orca_plugin_host_t; this tag exists so other
 * APIs can take a host pointer by name. */
typedef struct orca_plugin_host     orca_plugin_host_t;

/* Numeric slot id returned by orca_registry_add_slot and consumed by
 * orca_registry_remove_slot. 0 is reserved as invalid. */
typedef uint64_t orca_plugin_slot_id_t;

/* Lifetime: every handle above is owned by the engine. The plugin may only
 * retain the registry pointer and the host pointer for the duration of the
 * orca_plugin_register call. After that call returns the plugin must use
 * the host vtable (which is stable until orca_plugin_unregister) for any
 * further callbacks into the engine. */

/* ============ Slot kinds ============ */

/* Slot kinds the plugin can implement. The value space is partitioned so the
 * high nibble groups categories:
 *   0x00xx — pipeline / data slots (always active, including headless/CLI).
 *   0x01xx — GUI slots; inert when the engine runs without a GUI host
 *            (CLI, headless, automation). The plugin is still loaded but
 *            registrations for these kinds are stored and never invoked.
 *   0x10xx — raw escape hatch.
 *
 * The vtable layout for each kind is declared in Phase 2 of the plugin work.
 * For now orca_registry_add_slot accepts the vtable as an opaque
 * const void* whose concrete type is determined by `kind`. */
typedef enum {
    ORCA_SLOT_PIPELINE_OBSERVER    = 0x0001,
    ORCA_SLOT_PIPELINE_INTERCEPTOR = 0x0002,
    ORCA_SLOT_GCODE_FILTER         = 0x0003,
    ORCA_SLOT_PLACEHOLDER_PROVIDER = 0x0004,
    ORCA_SLOT_PRINTER_AGENT        = 0x0010,
    ORCA_SLOT_PROFILE_PACK         = 0x0020,
    ORCA_SLOT_SETTINGS_PAGE        = 0x0100,
    ORCA_SLOT_DEVICE_TAB           = 0x0101,
    ORCA_SLOT_MENU_COMMAND         = 0x0102,
    ORCA_SLOT_SIDEBAR_PANEL        = 0x0103,
    ORCA_SLOT_RAW_SUBSCRIBER       = 0x1000
} orca_slot_kind_t;

/* ============ Manifest + permissions ============ */

/* Permission bits declared by the plugin in orca_plugin_manifest_t.permissions.
 *
 * For native shared-library plugins, permissions are honor-system: the engine
 * surfaces declared permissions to the user (install confirmation, settings
 * UI) but cannot prevent a native plugin from doing whatever its process can
 * do. For WASM plugins, the same bits drive Wasmtime import gating and ARE
 * enforced at runtime. Always declare the smallest accurate set. */
#define ORCA_PERM_NETWORK          (1ull << 0)
#define ORCA_PERM_FILESYSTEM_READ  (1ull << 1)
#define ORCA_PERM_FILESYSTEM_WRITE (1ull << 2)
#define ORCA_PERM_SETTINGS_READ    (1ull << 3)
#define ORCA_PERM_SETTINGS_WRITE   (1ull << 4)
#define ORCA_PERM_PROFILES_INSTALL (1ull << 5)
#define ORCA_PERM_DEVICE_CONTROL   (1ull << 6)
#define ORCA_PERM_SLICE_INTERCEPT  (1ull << 7)
#define ORCA_PERM_GCODE_MODIFY     (1ull << 8)
#define ORCA_PERM_UI_ATTACH        (1ull << 9)
#define ORCA_PERM_EVENTS_RAW       (1ull << 10)

/* Plugin self-description. Field order is part of the ABI; do not reorder
 * within a given ORCA_PLUGIN_ABI_VERSION. New fields must be appended after
 * a struct_size bump on both sides. The engine validates struct_size before
 * reading any other field. */
typedef struct {
    uint32_t    struct_size;  /* sizeof(orca_plugin_manifest_t); engine validates */
    const char* id;           /* reverse-DNS, e.g. "com.example.klipper-pro" */
    const char* name;         /* human-readable */
    const char* version;      /* semver string, e.g. "1.0.0" */
    const char* author;
    const char* description;
    uint64_t    permissions;  /* bitfield of ORCA_PERM_* */
} orca_plugin_manifest_t;

/* ============ Host vtable ============ */

/* Logging callback. `level` follows the standard ordering used by the engine:
 *   0 = trace, 1 = debug, 2 = info, 3 = warn, 4 = error. */
typedef void (*orca_log_fn_t)(int level, const char* msg);

/* Raw event subscriber callback. Unlike orca_event_callback_t from c_api.h,
 * this variant carries the payload size so the plugin can survive seeing
 * future payload structs that grew. The plugin must treat `payload` as
 * borrowed and valid only for the duration of the call. */
typedef void (*orca_raw_event_callback_t)(
    orca_event_kind_t kind,
    const void*       payload,
    size_t            payload_size,
    void*             user_data);

/* The host vtable the engine populates and hands to the plugin via
 * orca_plugin_register. struct_size is at the top so engines older than the
 * plugin can be detected: the plugin must only read fields whose offset is
 * strictly less than struct_size. */
typedef struct orca_plugin_host {
    uint32_t struct_size;

    /* Logging. Safe to call from any thread. */
    orca_log_fn_t log;

    /* Session — the engine session this plugin is bound to. The returned
     * pointer is owned by the engine and valid until orca_plugin_unregister
     * returns. */
    orca_session_t* (*session)(void);

    /* Event subscription — wraps orca_events_subscribe from c_api.h.
     * The plugin must call events_unsubscribe for every id it received
     * before orca_plugin_unregister returns. */
    orca_subscription_id_t (*events_subscribe)(
        orca_event_kind_t     kind,
        orca_event_callback_t cb,
        void*                 user_data);
    void (*events_unsubscribe)(orca_subscription_id_t id);

    /* Raw escape-hatch subscriber — the "I know what I'm doing" backdoor.
     * Receives EVERY event whose bit is set in kind_mask, including future
     * event kinds the plugin was not built against. Requires
     * ORCA_PERM_EVENTS_RAW; the engine returns 0 (invalid id) otherwise. */
    orca_subscription_id_t (*events_subscribe_raw)(
        uint64_t                  kind_mask,
        orca_raw_event_callback_t cb,
        void*                     user_data);

    /* Preset / config access — wraps orca_presets_* from c_api.h. The
     * const variant is always available; presets_mut returns NULL if the
     * plugin lacks ORCA_PERM_SETTINGS_WRITE. */
    const orca_presets_t* (*presets_const)(void);
    orca_presets_t*       (*presets_mut)(void);

    /* Profile-pack loader — wraps PresetBundle::load_vendor_configs_from_json.
     * Requires ORCA_PERM_PROFILES_INSTALL. */
    orca_error_code_t (*load_profile_pack)(const char* dir);

    /* PlaceholderParser variable injection (Phase 2.3). All three variants
     * upsert a single named variable into the engine's placeholder context. */
    orca_error_code_t (*placeholder_set_string)(const char* name, const char* value);
    orca_error_code_t (*placeholder_set_int)   (const char* name, int64_t     value);
    orca_error_code_t (*placeholder_set_float) (const char* name, double      value);

    /* PrinterAgent registration (Phase 2.4). `vtable` is an
     * orca_slot_printer_agent_t* once Phase 2.4 lands; today it is treated
     * opaquely. `self` is passed back into every agent vtable callback. */
    orca_error_code_t (*register_printer_agent)(
        const char* agent_id,
        const void* vtable,
        void*       self);

    /* String free — for the few host calls that hand owned UTF-8 buffers
     * back to the plugin. Pairs with the same allocator the engine uses
     * internally so cross-DLL frees stay safe. */
    void (*string_free)(const char* s);
} orca_plugin_host_t;

/* ============ Registry surface ============ */

/* Functions the plugin INVOKES on the registry during orca_plugin_register.
 * The registry is single-threaded for the duration of that call; the plugin
 * must not retain the pointer past it. */

/* Declare the plugin's manifest. Must be called exactly once before any
 * orca_registry_add_slot. Returns ORCA_ERR_ALREADY_EXISTS on a second call,
 * ORCA_ERR_INVALID_ARGUMENT if manifest->struct_size is wrong. */
orca_error_code_t orca_registry_set_manifest(
    orca_plugin_registry_t*       registry,
    const orca_plugin_manifest_t* manifest);

/* Register a slot implementation. `vtable` is a pointer to the slot-specific
 * struct selected by `kind` (the engine casts internally based on kind).
 * `user_data` is passed back into every vtable callback. Returns 0 on
 * failure; otherwise a non-zero id usable with orca_registry_remove_slot. */
orca_plugin_slot_id_t orca_registry_add_slot(
    orca_plugin_registry_t* registry,
    orca_slot_kind_t        kind,
    const void*             vtable,
    void*                   user_data);

/* Remove a previously added slot. After return the engine will not invoke
 * the slot's vtable again. */
orca_error_code_t orca_registry_remove_slot(
    orca_plugin_registry_t* registry,
    orca_plugin_slot_id_t   slot_id);

/* ============ Mandatory plugin exports ============ */

/* The three symbols every plugin shared library MUST export. The engine
 * resolves them by name via dlsym / GetProcAddress and calls them in the
 * order documented below. */

/* Called once when the engine loads the library, after
 * orca_plugin_check_debug_consistent has succeeded. The plugin uses
 * `registry` to declare itself and `host` to learn how to call back into
 * the engine. Both pointers are owned by the engine; only `host` is valid
 * after this call returns.
 *
 * Returns ORCA_OK on success; any other code causes the engine to log the
 * error and unload the library. The plugin should return
 * ORCA_ERR_UNSUPPORTED if abi_version != ORCA_PLUGIN_ABI_VERSION. */
ORCA_PLUGIN_EXPORT orca_error_code_t orca_plugin_register(
    uint32_t                  abi_version,
    orca_plugin_registry_t*   registry,
    const orca_plugin_host_t* host);

/* Called once just before the engine dlclose()s the library. The plugin
 * tears down its state and unsubscribes any outstanding event handlers.
 * After this returns the plugin must not call any host function. */
ORCA_PLUGIN_EXPORT void orca_plugin_unregister(void);

/* Called by the engine BEFORE orca_plugin_register to detect debug/release
 * ABI mismatches that wreck C++ types across the boundary. Mirrors
 * src/slic3r/Utils/BBLNetworkPlugin.cpp:222-232.
 *
 * `engine_is_debug` is non-zero if the engine was built with debug C++
 * runtime, zero otherwise. The plugin returns 0 if its own debug/release
 * status matches, non-zero if it differs. A non-zero return aborts the
 * load with a clear error before any C++ types cross the boundary. */
ORCA_PLUGIN_EXPORT int orca_plugin_check_debug_consistent(int engine_is_debug);

#ifdef __cplusplus
}
#endif

#endif /* ORCA_PLUGIN_API_H */
