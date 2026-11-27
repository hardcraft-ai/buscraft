// BusCraft app: pick an adapter over USB, the network or Bluetooth LE, then send lines to it.
// Desktop uses the buscraft library directly; Android reaches USB and Bluetooth through Tauri plugins.

use std::time::{Duration, Instant};

use serde::Serialize;
use tokio::sync::Mutex;

#[cfg(target_os = "android")]
mod mobile;

/// One open link to an adapter.
enum Conn {
    Lib(buscraft::Connection),
    #[cfg(target_os = "android")]
    Mobile(mobile::MobileConn),
}

impl Conn {
    fn description(&self) -> String {
        match self {
            Conn::Lib(c) => c.description.clone(),
            #[cfg(target_os = "android")]
            Conn::Mobile(c) => c.description.clone(),
        }
    }

    async fn query(&mut self, line: &str, quiet: Duration, max: Duration) -> anyhow::Result<String> {
        match self {
            Conn::Lib(c) => c.query(line, quiet, max).await,
            #[cfg(target_os = "android")]
            Conn::Mobile(c) => c.query(line, quiet, max).await,
        }
    }

    async fn close(self) {
        match self {
            Conn::Lib(c) => c.close().await,
            #[cfg(target_os = "android")]
            Conn::Mobile(c) => c.close().await,
        }
    }
}

#[derive(Default)]
struct App {
    conn: Mutex<Option<Conn>>,
}

#[derive(Serialize)]
pub(crate) struct Device {
    pub id: String,
    pub name: String,
    pub detail: String,
}

impl From<buscraft::Found> for Device {
    fn from(f: buscraft::Found) -> Self {
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
fn usb_devices(app: tauri::AppHandle) -> Result<Vec<Device>, String> {
    #[cfg(target_os = "android")]
    return mobile::usb_devices(&app).map_err(err);
    #[cfg(not(target_os = "android"))]
    {
        let _ = app;
        Ok(buscraft::list_usb().map_err(err)?.into_iter().map(Device::from).collect())
    }
}

#[tauri::command]
async fn ble_devices() -> Result<Vec<Device>, String> {
    #[cfg(target_os = "android")]
    return mobile::ble_devices().await.map_err(err);
    #[cfg(not(target_os = "android"))]
    Ok(buscraft::scan_ble(Duration::from_secs(3)).await.map_err(err)?.into_iter().map(Device::from).collect())
}

#[tauri::command]
async fn connect(app: tauri::AppHandle, state: tauri::State<'_, App>, kind: String, target: String) -> Result<String, String> {
    let mut slot = state.conn.lock().await;
    if let Some(old) = slot.take() {
        old.close().await;
    }
    let conn = match kind.as_str() {
        "net" => {
            let host = if target.is_empty() { "buscraft.local".into() } else { target };
            Conn::Lib(buscraft::Connection::open(&buscraft::Target::Tcp(host)).await.map_err(err)?)
        }
        #[cfg(target_os = "android")]
        "usb" => Conn::Mobile(mobile::MobileConn::open_usb(&app, &target).map_err(err)?),
        #[cfg(target_os = "android")]
        "ble" => Conn::Mobile(mobile::MobileConn::open_ble(&target).await.map_err(err)?),
        #[cfg(not(target_os = "android"))]
        "usb" => Conn::Lib(buscraft::Connection::open(&buscraft::Target::Usb(target)).await.map_err(err)?),
        #[cfg(not(target_os = "android"))]
        "ble" => Conn::Lib(buscraft::Connection::open(&buscraft::Target::Ble(target)).await.map_err(err)?),
        _ => return Err(format!("unknown connection type {kind}")),
    };
    let _ = &app;
    let description = conn.description();
    *slot = Some(conn);
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

/// Stores the Wi-Fi network on the adapter. A separate command so the password never passes through the console log.
#[tauri::command]
async fn set_wifi(state: tauri::State<'_, App>, ssid: String, password: String) -> Result<String, String> {
    if ssid.contains('"') {
        return Err("the network name cannot contain a double quote".into());
    }
    let mut slot = state.conn.lock().await;
    let c = slot.as_mut().ok_or("not connected")?;
    let line = if ssid.is_empty() { "++wifi off".to_string() } else { format!("++wifi \"{ssid}\" {password}") };
    let reply = c.query(&line, Duration::from_millis(300), Duration::from_secs(5)).await.map_err(err)?;
    Ok(reply.trim().to_string())
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    let builder = tauri::Builder::default().manage(App::default());
    #[cfg(target_os = "android")]
    let builder = builder.plugin(tauri_plugin_blec::init()).plugin(tauri_plugin_serialplugin::init());
    builder
        .invoke_handler(tauri::generate_handler![usb_devices, ble_devices, connect, disconnect, query, set_wifi])
        .run(tauri::generate_context!())
        .expect("error while running BusCraft");
}
