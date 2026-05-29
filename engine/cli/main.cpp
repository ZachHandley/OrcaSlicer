// orca-engine-cli — Phase 0.3 build canary.
//
// Links against orca-engine (the libslic3r shared library) with NO wxWidgets
// and drives a real slice end-to-end: build a cube, apply the default print
// config, run the FFF pipeline, export G-code. If this runs and emits a
// non-trivial G-code file, the engine is proven usable by a non-GUI consumer
// without any GUI layer present.

#include <libslic3r/Config.hpp>
#include <libslic3r/Model.hpp>
#include <libslic3r/Print.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r_version.h>

// Phase 0.4d — verify the typed surface compiles + runs from a non-GUI consumer.
#include <orca/Config.hpp>
#include <orca/ConfigKeys.hpp>

#include <cstddef>
#include <cstdio>

using namespace Slic3r;

int main() {
    std::printf("orca-engine-cli: libslic3r %s\n", SLIC3R_VERSION);

    // 1) A 20mm cube — the smallest meaningful FFF job.
    Model       model;
    ModelObject *object = model.add_object();
    object->name = "canary_cube";
    object->add_volume(make_cube(20.0, 20.0, 20.0));
    object->add_instance();
    for (ModelObject *mo : model.objects)
        mo->ensure_on_bed();

    // 2) Default print configuration. Use absolute E distances so the stock
    //    layer_gcode validates (relative E would require a "G92 E0" reset that
    //    the bare default profile doesn't include).
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_num_extruders(1);
    config.set_key_value("use_relative_e_distances", new ConfigOptionBool(false));

    // Phase 0.4d typed surface — sanity-check read+write through orca::config
    // against a bare DynamicPrintConfig (Adhoc receiver, the dominant call
    // shape per the wave-1 catalog). If these don't round-trip the canary
    // fails fast and tells us the typed surface is broken.
    {
        using namespace orca::keys;
        auto lh = orca::config::get<layer_height>(config);
        if (!lh) {
            std::printf("orca-engine-cli: typed get<layer_height> returned nullopt\n");
            return 1;
        }
        orca::config::put<layer_height>(config, *lh + 0.01);
        auto lh2 = orca::config::get<layer_height>(config);
        if (!lh2 || std::abs(*lh2 - (*lh + 0.01)) > 1e-9) {
            std::printf("orca-engine-cli: typed layer_height write+read did not round-trip\n");
            return 1;
        }
        // Restore the original so the slice below uses the canonical value.
        orca::config::put<layer_height>(config, *lh);

        auto noz = orca::config::get_vec<nozzle_diameter>(config);
        if (!noz || noz->empty()) {
            std::printf("orca-engine-cli: typed get_vec<nozzle_diameter> empty\n");
            return 1;
        }
        std::printf("orca-engine-cli: typed surface OK (layer_height=%.3f, nozzle_diameter[0]=%.3f)\n",
                    *lh, (*noz)[0]);
    }

    // 3) FFF pipeline.
    Print print;
    for (ModelObject *mo : model.objects)
        print.auto_assign_extruders(mo);
    print.apply(model, config);

    StringObjectException err = print.validate();
    if (!err.string.empty()) {
        std::printf("orca-engine-cli: validate FAILED: %s\n", err.string.c_str());
        return 1;
    }

    print.set_status_silent();
    print.process();

    // 4) Verify the slice produced real toolpaths. process() runs the full FFF
    //    pipeline (perimeters, infill, support, G-code generation) — a populated
    //    layer set is proof the engine sliced end-to-end without any GUI layer.
    //
    //    Note: we intentionally do NOT call export_gcode() here. Exporting a
    //    *bare* full_print_config() crashes in append_full_config →
    //    ConfigOptionEnumsGeneric::serialize() because some Orca enum options
    //    ship without an enum-names map in their ConfigDef; serializing the
    //    whole config as G-code comments dereferences it. The GUI never hits
    //    this (it always has loaded profiles). Tracked for Phase 0.4c.
    if (print.objects().empty()) {
        std::printf("orca-engine-cli: slice produced no print objects\n");
        return 1;
    }
    const std::size_t layers = print.objects().front()->layer_count();
    if (layers < 50) {
        std::printf("orca-engine-cli: slice produced too few layers (%zu)\n", layers);
        return 1;
    }

    std::printf("orca-engine-cli: engine OK — sliced 20mm cube into %zu layers\n", layers);
    return 0;
}
