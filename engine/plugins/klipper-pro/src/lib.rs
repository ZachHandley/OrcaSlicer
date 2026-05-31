//! Phase 6 — Klipper-Pro flagship plugin.
//!
//! Connects OrcaSlicer to a Klipper printer via the Moonraker HTTP API.
//! Registers as an ORCA_SLOT_PRINTER_AGENT named "klipper-pro" so the
//! user can pick it for any configured network printer.

use orca_plugin_sdk::prelude::*;

mod agent;
mod jobs;
mod moonraker;

pub use agent::KlipperAgent;

#[orca_plugin(
    id          = "com.orcaslicer.klipper-pro",
    version     = "0.1.0",
    name        = "Klipper-Pro",
    author      = "OrcaSlicer",
    description = "Send slices to Klipper-flavored printers via Moonraker.",
    permissions = ["network", "device_control", "ui_attach"],
)]
pub struct KlipperPro;

impl KlipperPro {
    fn on_register(ctx: &Ctx) -> Result<()> {
        ctx.log_info("klipper-pro: registering agent");
        orca_plugin_sdk::device::register::<KlipperAgent>()?;
        Ok(())
    }
}
