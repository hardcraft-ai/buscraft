// Android transports: USB host serial through tauri-plugin-serialplugin, Bluetooth LE through tauri-plugin-blec.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use anyhow::{anyhow, Result};
use buscraft::{CHAR_COMMAND, CHAR_RESPONSE, SERVICE};
use tauri::Manager;
use tauri_plugin_blec::models::{ScanFilter, WriteType};
use tauri_plugin_blec::OnDisconnectHandler;
use tauri_plugin_serialplugin::api::serial::SerialPort;
use tokio::sync::mpsc;

use crate::Device;

type Serial = SerialPort<tauri::Wry>;

pub fn usb_devices(app: &tauri::AppHandle) -> Result<Vec<Device>> {
    let serial = app.state::<Serial>();
    let ports = serial.available_ports(true).map_err(|e| anyhow!("{e}"))?;
    let mut out: Vec<Device> = ports
        .into_iter()
        .map(|(path, info)| {
            let name = info.get("product").cloned().unwrap_or_else(|| "USB serial".into());
            let detail = info.get("serial_number").cloned().unwrap_or_default();
            Device { id: path, name, detail }
        })
        .collect();
    // BusCraft adapters first
    out.sort_by_key(|d| !d.name.contains("BusCraft"));
    Ok(out)
}

/// Android 11 and older only deliver BLE scan results to apps holding the location permission. The plugin asks for
/// it only together with iBeacon support, so turn that on for those versions.
fn old_android() -> bool {
    std::process::Command::new("getprop")
        .arg("ro.build.version.sdk")
        .output()
        .ok()
        .and_then(|o| String::from_utf8_lossy(&o.stdout).trim().parse::<u32>().ok())
        .is_some_and(|sdk| sdk < 31)
}

async fn ble_permissions() -> Result<()> {
    let location = old_android();
    tauri_plugin_blec::ALLOW_IBEACONS.store(location, std::sync::atomic::Ordering::Release);
    if !tauri_plugin_blec::check_permissions(true).await.map_err(|e| anyhow!("{e}"))? {
        return Err(anyhow!(if location {
            "Bluetooth needs the location permission on this Android version"
        } else {
            "Bluetooth permission denied"
        }));
    }
    Ok(())
}

pub async fn ble_devices() -> Result<Vec<Device>> {
    ble_permissions().await?;
    let handler = tauri_plugin_blec::get_handler().map_err(|e| anyhow!("{e}"))?;
    let (tx, mut rx) = mpsc::channel(16);
    handler.discover(Some(tx), 3000, ScanFilter::Service(SERVICE), old_android()).await.map_err(|e| anyhow!("{e}"))?;
    let mut latest = Vec::new();
    while let Ok(Some(found)) = tokio::time::timeout(Duration::from_millis(3500), rx.recv()).await {
        latest = found;
    }
    Ok(latest
        .into_iter()
        .map(|d| Device {
            id: d.address,
            name: if d.name.is_empty() { "BusCraft".into() } else { d.name },
            detail: d.rssi.map(|r| format!("{r} dBm")).unwrap_or_default(),
        })
        .collect())
}

enum Writer {
    Usb { app: tauri::AppHandle, path: String, stop: Arc<AtomicBool> },
    Ble,
}

pub struct MobileConn {
    writer: Writer,
    rx: mpsc::UnboundedReceiver<Vec<u8>>,
    pub description: String,
}

impl MobileConn {
    pub fn open_usb(app: &tauri::AppHandle, path: &str) -> Result<MobileConn> {
        let serial = app.state::<Serial>();
        serial.open(path.to_string(), 115_200, None, None, None, None, Some(50)).map_err(|e| anyhow!("{e}"))?;
        let (tx, rx) = mpsc::unbounded_channel();
        let stop = Arc::new(AtomicBool::new(false));
        let (app2, path2, stop2) = (app.clone(), path.to_string(), stop.clone());
        std::thread::spawn(move || {
            let serial = app2.state::<Serial>();
            while !stop2.load(Ordering::Relaxed) {
                match serial.read_binary(path2.clone(), Some(50), Some(512)) {
                    Ok(data) if !data.is_empty() => {
                        if tx.send(data).is_err() {
                            break;
                        }
                    }
                    Ok(_) => {}
                    // a read timeout is reported as an error; anything else ends the loop when the port goes away
                    Err(e) if e.to_string().to_lowercase().contains("time") => {}
                    Err(_) => std::thread::sleep(Duration::from_millis(50)),
                }
            }
        });
        Ok(MobileConn {
            writer: Writer::Usb { app: app.clone(), path: path.to_string(), stop },
            rx,
            description: format!("USB {path}"),
        })
    }

    pub async fn open_ble(address: &str) -> Result<MobileConn> {
        ble_permissions().await?;
        let handler = tauri_plugin_blec::get_handler().map_err(|e| anyhow!("{e}"))?;
        let address = if address.is_empty() {
            ble_devices().await?.into_iter().next().map(|d| d.id).ok_or_else(|| anyhow!("BusCraft not found over Bluetooth"))?
        } else {
            address.to_string()
        };
        handler
            .connect(&address, OnDisconnectHandler::from_sync(|| {}), old_android())
            .await
            .map_err(|e| anyhow!("{e}"))?;
        let (tx, rx) = mpsc::unbounded_channel();
        handler
            .subscribe(CHAR_RESPONSE, Some(SERVICE), move |data: Vec<u8>| {
                let _ = tx.send(data);
            })
            .await
            .map_err(|e| anyhow!("{e}"))?;
        Ok(MobileConn { writer: Writer::Ble, rx, description: format!("Bluetooth {address}") })
    }

    async fn send_line(&mut self, line: &str) -> Result<()> {
        match &self.writer {
            Writer::Usb { app, path, .. } => {
                let serial = app.state::<Serial>();
                serial.write_binary(path.clone(), format!("{line}\n").into_bytes()).map_err(|e| anyhow!("{e}"))?;
            }
            Writer::Ble => {
                let handler = tauri_plugin_blec::get_handler().map_err(|e| anyhow!("{e}"))?;
                // every write ends a line on the adapter, so no newline here
                handler
                    .send_data(CHAR_COMMAND, Some(SERVICE), line.as_bytes(), WriteType::WithResponse)
                    .await
                    .map_err(|e| anyhow!("{e}"))?;
            }
        }
        Ok(())
    }

    pub async fn query(&mut self, line: &str, quiet: Duration, max: Duration) -> Result<String> {
        while self.rx.try_recv().is_ok() {}
        self.send_line(line).await?;
        buscraft::read_reply(&mut self.rx, quiet, max).await
    }

    pub async fn close(self) {
        match self.writer {
            Writer::Usb { app, path, stop } => {
                stop.store(true, Ordering::Relaxed);
                let _ = app.state::<Serial>().close(path);
            }
            Writer::Ble => {
                if let Ok(h) = tauri_plugin_blec::get_handler() {
                    let _ = h.disconnect().await;
                }
            }
        }
    }
}
