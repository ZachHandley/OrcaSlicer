//! Print-job orchestration. Uploads a sliced G-code file to Moonraker
//! and either starts the print immediately or leaves it queued.

use crate::moonraker::MoonrakerClient;
use orca_plugin_sdk::error::Result;

pub struct PrintJob {
    pub filename: String,
    pub bytes:    Vec<u8>,
    pub start_immediately: bool,
}

impl PrintJob {
    /// Upload the file to Moonraker and (optionally) start the print.
    pub fn submit(&self, client: &MoonrakerClient) -> Result<()> {
        client.upload_gcode(&self.filename, self.bytes.clone())?;
        if self.start_immediately {
            client.start_print(&self.filename)?;
        }
        Ok(())
    }
}
