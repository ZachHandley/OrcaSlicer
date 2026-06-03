# OrcaForge Showcase Profile Pack

A tiny first-party `kind=data` plugin for the OrcaForge marketplace.

The pack ships one printer model, one filament, and one print profile under
the vendor name **OrcaForge Showcase**. It is intentionally minimal — its job
is to prove the data-plugin path end-to-end (manifest, zip layout, verify,
publish, install, load via `PresetBundle::load_vendor_configs_from_json`).

## What's inside

```
manifest.json                                  — plugin metadata (kind=data)
profiles/
  OrcaForge Showcase.json                      — vendor root (machine_model
                                                 / process / filament /
                                                 machine_list entries)
  OrcaForge Showcase/
    machine/
      OrcaForge Showcase.json                  — machine_model definition
      OrcaForge Showcase 0.4 nozzle.json       — printer (220x220x250)
    filament/
      OrcaForge Showcase PLA.json              — PLA @ 215°C / 60°C bed
    process/
      0.20mm Standard @OrcaForge Showcase.json — 0.2mm layer, 15% infill
```

Layout matches the OrcaSlicer vendor convention (see `resources/profiles/`
for canonical examples). The vendor root's `sub_path` values are resolved
under `profiles/OrcaForge Showcase/` by the loader.

## Build

```bash
make build
```

Produces `target/sample-profiles/com.orcaslicer.sample-profiles.orcaplugin`
— a zip containing `manifest.json`, `profiles/`, and this README. That zip
is the artifact the OrcaForge marketplace consumes.

## Why is this not a "real" printer?

It isn't a clone of a real Voron/Bambu/Anker machine. It exists to demo the
plugin path. Real profile packs ship as separate marketplace listings.
