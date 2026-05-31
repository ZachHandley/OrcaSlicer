//! Check — slot-set equality.
//!
//! manifest.provides[] is supposed to be authoritative documentation of what
//! slot kinds the plugin will register. This check asserts the BINARY agrees:
//! every kind in manifest.provides[] must show up in the recorded ABI
//! handshake's slot_kinds list, and vice versa.
//!
//! The mapping from kind-string to numeric ORCA_SLOT_* constant lives here
//! (single source of truth — the SDK abi.rs constants and the engine's
//! plugin_api.h header). If the manifest references a kind we don't know,
//! that's a fail.

use std::collections::BTreeSet;

use orca_plugin_sdk::abi;

use super::abi::RecordedState;
use super::manifest::ParsedManifest;
use crate::report::CheckResult;

fn kind_for_name(name: &str) -> Option<u32> {
    Some(match name {
        "pipeline_observer"    => abi::ORCA_SLOT_PIPELINE_OBSERVER,
        "pipeline_interceptor" => abi::ORCA_SLOT_PIPELINE_INTERCEPTOR,
        "gcode_filter"         => abi::ORCA_SLOT_GCODE_FILTER,
        "placeholder_provider" => abi::ORCA_SLOT_PLACEHOLDER_PROVIDER,
        "printer_agent"        => abi::ORCA_SLOT_PRINTER_AGENT,
        "profile_pack"         => abi::ORCA_SLOT_PROFILE_PACK,
        "settings_page"        => abi::ORCA_SLOT_SETTINGS_PAGE,
        "device_tab"           => abi::ORCA_SLOT_DEVICE_TAB,
        "menu_command"         => abi::ORCA_SLOT_MENU_COMMAND,
        "sidebar_panel"        => abi::ORCA_SLOT_SIDEBAR_PANEL,
        _                      => return None,
    })
}

pub fn check(manifest: &ParsedManifest, recorded: &RecordedState) -> CheckResult {
    // If the manifest doesn't list provides[], skip the equality check
    // (still a pass — the manifest is just less documented). The binary
    // can still register slots.
    if manifest.provides.is_empty() && recorded.slot_kinds.is_empty() {
        return CheckResult::pass("slots");
    }

    let mut errors = Vec::<String>::new();

    let declared: BTreeSet<u32> = manifest.provides.iter()
        .filter_map(|n| match kind_for_name(n) {
            Some(k) => Some(k),
            None    => {
                errors.push(format!(
                    "manifest provides {n:?} is not a known slot kind"));
                None
            }
        })
        .collect();
    let registered: BTreeSet<u32> = recorded.slot_kinds.iter().copied().collect();

    let missing_in_binary:   Vec<u32> = declared.difference(&registered).copied().collect();
    let undeclared_in_binary: Vec<u32> = registered.difference(&declared).copied().collect();

    if !missing_in_binary.is_empty() {
        errors.push(format!(
            "manifest declared {:?} but binary didn't register them",
            missing_in_binary));
    }
    if !undeclared_in_binary.is_empty() {
        errors.push(format!(
            "binary registered {:?} but manifest doesn't declare them",
            undeclared_in_binary));
    }

    if errors.is_empty() {
        CheckResult::pass("slots")
    } else {
        CheckResult::fail("slots", errors.join("\n"))
    }
}
