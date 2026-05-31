//! Phase 5.1 — OrcaSlicer plugin SDK for Rust authors.
//!
//! Two surfaces live here:
//!   - `abi` — raw FFI bindings to `engine/include/orca/plugin_api.h`,
//!             stable across plugin builds via [`abi::ABI_VERSION`].
//!   - `plugin` / `ctx` / `events` / `presets` / `gcode` / `device` —
//!             safe Rust wrappers. Plugin authors implement [`Plugin`]
//!             plus zero-or-more domain traits and invoke
//!             [`export_plugin!`] to emit the three mandatory cdylib
//!             exports.
//!
//! Phase 5.2 layers a derive macro (`#[orca::plugin]`) on top so
//! authors don't have to write the export macro themselves.

pub mod abi;
pub mod ctx;
pub mod device;
pub mod error;
pub mod events;
pub mod gcode;
pub mod plugin;
pub mod presets;

// ---------- Prelude — most authors `use orca_plugin_sdk::*;` ---------

pub use crate::ctx::{Ctx, LogLevel};
pub use crate::error::{Error, Result};
pub use crate::plugin::Plugin;

pub mod prelude {
    pub use crate::abi;
    pub use crate::ctx::{current, with_host, Ctx, LogLevel};
    pub use crate::error::{Error, Result};
    pub use crate::events::{self, Event, Subscription};
    pub use crate::plugin::Plugin;
}
