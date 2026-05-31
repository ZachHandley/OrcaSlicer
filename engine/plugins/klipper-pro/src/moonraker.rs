//! Moonraker JSON-RPC client. Uses ureq's blocking HTTP because the
//! agent's connect/send_command path runs on the engine's UI thread
//! (and we want predictable error mapping).
//!
//! Moonraker exposes both HTTP REST (`/printer/...`) and a JSON-RPC
//! transport (`/websocket` or `/server/jsonrpc`). For Phase 6.1 we
//! use the REST surface where it exists and fall back to the HTTP
//! JSON-RPC endpoint for `printer.gcode.script` and similar.

use orca_plugin_sdk::Error;
use orca_plugin_sdk::error::Result;
use serde::{Deserialize, Serialize};
use std::time::Duration;

pub struct MoonrakerClient {
    base_url: String,
    api_key:  Option<String>,
    agent:    ureq::Agent,
}

impl MoonrakerClient {
    pub fn new(base_url: impl Into<String>, api_key: Option<String>) -> Self {
        let config = ureq::Agent::config_builder()
            .timeout_global(Some(Duration::from_secs(10)))
            .build();
        Self {
            base_url: base_url.into().trim_end_matches('/').to_string(),
            api_key,
            agent: ureq::Agent::new_with_config(config),
        }
    }

    fn auth_header(&self, mut req: ureq::RequestBuilder<ureq::typestate::WithoutBody>)
        -> ureq::RequestBuilder<ureq::typestate::WithoutBody>
    {
        if let Some(k) = self.api_key.as_deref() {
            req = req.header("X-Api-Key", k);
        }
        req
    }

    fn auth_header_body(&self, mut req: ureq::RequestBuilder<ureq::typestate::WithBody>)
        -> ureq::RequestBuilder<ureq::typestate::WithBody>
    {
        if let Some(k) = self.api_key.as_deref() {
            req = req.header("X-Api-Key", k);
        }
        req
    }

    /// GET /printer/info — returns the Klipper version + state. Used
    /// as the connect probe to verify reachability.
    pub fn info(&self) -> Result<PrinterInfo> {
        let url = format!("{}/printer/info", self.base_url);
        let resp = self.auth_header(self.agent.get(&url))
            .call()
            .map_err(map_ureq_err)?;
        parse_json::<MoonrakerResp<PrinterInfo>>(resp).map(|r| r.result)
    }

    /// GET /printer/objects/query?heater_bed&extruder&print_stats — snapshot
    /// of the per-object state Klipper publishes.
    pub fn query_status(&self) -> Result<StatusSnapshot> {
        let url = format!(
            "{}/printer/objects/query?heater_bed&extruder&print_stats",
            self.base_url);
        let resp = self.auth_header(self.agent.get(&url))
            .call()
            .map_err(map_ureq_err)?;
        parse_json::<MoonrakerResp<QueryEnvelope>>(resp).map(|r| r.result.status)
    }

    /// POST /printer/gcode/script — run an arbitrary G-code line.
    pub fn run_gcode(&self, script: &str) -> Result<()> {
        let url = format!("{}/printer/gcode/script", self.base_url);
        let _ = self.auth_header_body(self.agent.post(&url))
            .content_type("application/json")
            .send(serde_json::to_string(&GcodeReq { script: script.to_string() })
                  .map_err(|_| Error::InvalidArgument)?)
            .map_err(map_ureq_err)?;
        Ok(())
    }

    /// POST /server/files/upload — uploads a file to the Klipper
    /// gcodes directory. Used by jobs.rs's upload-then-start flow.
    pub fn upload_gcode(&self, filename: &str, body: Vec<u8>) -> Result<()> {
        let url = format!("{}/server/files/upload?root=gcodes", self.base_url);
        let boundary = "----orca-klipper-pro";
        let mut payload = Vec::new();
        payload.extend(format!(
            "--{boundary}\r\n\
             Content-Disposition: form-data; name=\"file\"; filename=\"{filename}\"\r\n\
             Content-Type: application/octet-stream\r\n\r\n").into_bytes());
        payload.extend(body);
        payload.extend(format!("\r\n--{boundary}--\r\n").into_bytes());

        let _ = self.auth_header_body(self.agent.post(&url))
            .content_type(&*format!("multipart/form-data; boundary={boundary}"))
            .send(payload)
            .map_err(map_ureq_err)?;
        Ok(())
    }

    /// POST /printer/print/start?filename=<name> — start a previously
    /// uploaded file.
    pub fn start_print(&self, filename: &str) -> Result<()> {
        let url = format!(
            "{}/printer/print/start?filename={}",
            self.base_url,
            urlencode(filename));
        let _ = self.auth_header_body(self.agent.post(&url))
            .send("")
            .map_err(map_ureq_err)?;
        Ok(())
    }

    /// POST /printer/print/cancel — cancels the active print.
    pub fn cancel_print(&self) -> Result<()> {
        let url = format!("{}/printer/print/cancel", self.base_url);
        let _ = self.auth_header_body(self.agent.post(&url))
            .send("")
            .map_err(map_ureq_err)?;
        Ok(())
    }
}

fn parse_json<T: for<'de> Deserialize<'de>>(resp: ureq::http::Response<ureq::Body>) -> Result<T> {
    let body = resp.into_body().read_to_string().map_err(|_| Error::IoError)?;
    serde_json::from_str(&body).map_err(|_| Error::ParseError)
}

fn map_ureq_err(e: ureq::Error) -> Error {
    use ureq::Error as E;
    match e {
        E::StatusCode(c) if c == 401 || c == 403 => Error::PermissionDenied,
        E::StatusCode(c) if c == 404 => Error::NotFound,
        E::StatusCode(_) => Error::IoError,
        _                => Error::IoError,
    }
}

fn urlencode(s: &str) -> String {
    s.bytes().flat_map(|b| match b {
        b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'.' | b'_' | b'~'
            => vec![b],
        _   => format!("%{:02X}", b).into_bytes(),
    }).map(char::from).collect()
}

// ---------- Moonraker envelope ---------------------------------------

#[derive(Deserialize)]
struct MoonrakerResp<T> {
    result: T,
}

#[derive(Debug, Deserialize)]
pub struct PrinterInfo {
    pub state:           String,
    pub state_message:   Option<String>,
    pub software_version: Option<String>,
    pub hostname:         Option<String>,
}

#[derive(Deserialize)]
struct QueryEnvelope {
    status: StatusSnapshot,
}

#[derive(Debug, Deserialize, Default)]
pub struct StatusSnapshot {
    pub heater_bed: Option<HeaterState>,
    pub extruder:   Option<HeaterState>,
    pub print_stats: Option<PrintStats>,
}

#[derive(Debug, Deserialize, Default)]
pub struct HeaterState {
    pub temperature: Option<f64>,
    pub target:      Option<f64>,
}

#[derive(Debug, Deserialize, Default)]
pub struct PrintStats {
    pub state:        Option<String>,
    pub filename:     Option<String>,
    pub print_duration: Option<f64>,
    pub total_duration: Option<f64>,
    pub filament_used:  Option<f64>,
}

#[derive(Serialize)]
struct GcodeReq {
    script: String,
}
