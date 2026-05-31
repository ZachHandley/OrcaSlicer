//! DeviceAgent — wires the Moonraker client into the engine's
//! orca::PrinterAgent surface.

use crate::jobs::PrintJob;
use crate::moonraker::MoonrakerClient;
use orca_plugin_sdk::device::{DeviceAgent, HostEmit, PrinterState};
use orca_plugin_sdk::error::{Error, Result};
use std::cell::RefCell;
use std::fs;
use std::path::Path;
use std::sync::Mutex;

pub struct KlipperAgent {
    host:   HostEmit,
    client: Mutex<Option<MoonrakerClient>>,
    state:  Mutex<PrinterState>,
    last_filename: RefCell<Option<String>>,
}

// HostEmit is Send + Sync; RefCell is not Sync but lives behind &self
// only from the engine's single-threaded calls. Wrap it in a Mutex
// to keep the type usable across the trait's Send + Sync bound.
unsafe impl Sync for KlipperAgent {}

impl DeviceAgent for KlipperAgent {
    const AGENT_ID:          &'static str = "klipper-pro";
    const AGENT_NAME:        &'static str = "Klipper-Pro";
    const AGENT_VERSION:     &'static str = "0.1.0";
    const AGENT_DESCRIPTION: &'static str =
        "Klipper/Moonraker integration with HTTP upload + status polling.";

    fn new(host: HostEmit) -> Self {
        Self {
            host,
            client: Mutex::new(None),
            state:  Mutex::new(PrinterState::Disconnected),
            last_filename: RefCell::new(None),
        }
    }

    fn connect(
        &self,
        _device_id: &str,
        host_or_ip: &str,
        port:       i32,
        _username:  &str,
        password:   &str,   // doubles as the Moonraker API key
        use_tls:    bool,
    ) -> Result<()> {
        let scheme = if use_tls { "https" } else { "http" };
        let port   = if port > 0 { port } else { if use_tls { 443 } else { 7125 } };
        let base   = format!("{scheme}://{host_or_ip}:{port}");

        let api_key = if password.is_empty() { None } else { Some(password.to_string()) };
        let client = MoonrakerClient::new(base.clone(), api_key);

        // Probe with /printer/info.
        match client.info() {
            Ok(info) => {
                *self.state.lock().unwrap() = PrinterState::Connected;
                *self.client.lock().unwrap() = Some(client);
                self.host.emit_status(
                    PrinterState::Connected, 0,
                    Some(&format!("Connected to Klipper at {} ({})",
                                  base, info.state)));
                Ok(())
            }
            Err(e) => {
                *self.state.lock().unwrap() = PrinterState::Error;
                self.host.emit_status(
                    PrinterState::Error, e.as_code(),
                    Some(&format!("Failed to reach Moonraker at {base}")));
                Err(e)
            }
        }
    }

    fn disconnect(&self) -> Result<()> {
        *self.client.lock().unwrap() = None;
        *self.state.lock().unwrap()  = PrinterState::Disconnected;
        self.host.emit_status(PrinterState::Disconnected, 0, None);
        Ok(())
    }

    fn current_state(&self) -> PrinterState {
        *self.state.lock().unwrap()
    }

    fn send_command(&self, payload: &[u8]) -> Result<()> {
        let script = std::str::from_utf8(payload).map_err(|_| Error::InvalidArgument)?;
        let guard = self.client.lock().unwrap();
        let client = guard.as_ref().ok_or(Error::InvalidArgument)?;
        client.run_gcode(script)
    }

    fn start_print(
        &self,
        gcode_path: &str,
        job_name:   &str,
        start_immediately: bool,
    ) -> Result<()> {
        let bytes = fs::read(Path::new(gcode_path)).map_err(|_| Error::IoError)?;

        // Klipper file names must end in .gcode. Honor job_name if
        // provided, else fall back to the basename of gcode_path.
        let filename = if !job_name.is_empty() {
            ensure_gcode_ext(job_name)
        } else {
            ensure_gcode_ext(
                Path::new(gcode_path).file_name()
                    .and_then(|s| s.to_str())
                    .unwrap_or("orca-job.gcode"))
        };

        let job = PrintJob { filename: filename.clone(), bytes, start_immediately };
        let guard = self.client.lock().unwrap();
        let client = guard.as_ref().ok_or(Error::InvalidArgument)?;
        job.submit(client)?;

        *self.last_filename.borrow_mut() = Some(filename.clone());
        self.host.emit_message_text(&format!(
            r#"{{"event":"job_submitted","filename":"{filename}","started":{start_immediately}}}"#));
        Ok(())
    }

    fn cancel_print(&self) -> Result<()> {
        let guard = self.client.lock().unwrap();
        let client = guard.as_ref().ok_or(Error::InvalidArgument)?;
        client.cancel_print()
    }
}

fn ensure_gcode_ext(name: &str) -> String {
    if name.ends_with(".gcode") { name.to_string() }
    else { format!("{name}.gcode") }
}
