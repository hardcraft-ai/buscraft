// BusCraft desktop app: pick an adapter over USB, the network or Bluetooth LE, then send lines to it.
#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use std::time::{Duration, Instant};

use buscraft::{list_usb, scan_ble, Connection, Found, Target};
use serde::Serialize;
use tokio::sync::Mutex;

#[derive(Default)]
struct App {
    conn: Mutex<Option<Connection>>,
}

#[derive(Serialize)]
struct Device {
    id: String,
    name: String,
    detail: String,
}

impl From<Found> for Device {
    fn from(f: Found) -> Self {
        Device { id: f.id, name: f.name, detail: f.detail }
    }
}

#[derive(Serialize)]
struct Reply {
    text: String,
    ms: f64,
}

fn err(e: anyhow::Error) -> String {
    format!("{e:#}")
}

#[tauri::command]
fn usb_devices() -> Result<Vec<Device>, String> {
    Ok(list_usb().map_err(err)?.into_iter().map(Device::from).collect())
}

#[tauri::command]
async fn ble_devices() -> Result<Vec<Device>, String> {
    Ok(scan_ble(Duration::from_secs(3)).await.map_err(err)?.into_iter().map(Device::from).collect())
}

#[tauri::command]
async fn connect(state: tauri::State<'_, App>, kind: String, target: String) -> Result<String, String> {
    let target = match kind.as_str() {
        "usb" => Target::Usb(target),
        "net" => Target::Tcp(if target.is_empty() { "buscraft.local".into() } else { target }),
        "ble" => Target::Ble(target),
        _ => return Err(format!("unknown connection type {kind}")),
    };
    let mut slot = state.conn.lock().await;
    if let Some(old) = slot.take() {
        old.close().await;
    }
    let c = Connection::open(&target).await.map_err(err)?;
    let description = c.description.clone();
    *slot = Some(c);
    Ok(description)
}

#[tauri::command]
async fn disconnect(state: tauri::State<'_, App>) -> Result<(), String> {
    if let Some(c) = state.conn.lock().await.take() {
        c.close().await;
    }
    Ok(())
}

/// Sends a line and returns the reply with the round-trip time. `quiet_ms` is how long the link must stay silent
/// before the reply is considered complete (short for adapter commands, longer for instrument reads).
#[tauri::command]
async fn query(state: tauri::State<'_, App>, line: String, quiet_ms: Option<u64>) -> Result<Reply, String> {
    let mut slot = state.conn.lock().await;
    let c = slot.as_mut().ok_or("not connected")?;
    let t0 = Instant::now();
    let quiet = Duration::from_millis(quiet_ms.unwrap_or(150));
    let text = c.query(&line, quiet, Duration::from_secs(5)).await.map_err(err)?;
    // Round trip without the trailing quiet period
    let ms = (t0.elapsed().saturating_sub(if text.is_empty() { Duration::ZERO } else { quiet })).as_secs_f64() * 1000.0;
    Ok(Reply { text, ms })
}

fn main() {
    tauri::Builder::default()
        .manage(App::default())
        .invoke_handler(tauri::generate_handler![usb_devices, ble_devices, connect, disconnect, query])
        .run(tauri::generate_context!())
        .expect("error while running BusCraft");
}
