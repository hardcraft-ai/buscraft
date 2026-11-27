//! Talk to a BusCraft adapter over USB serial, TCP (Wi-Fi) or Bluetooth LE. All three carry the same line protocol:
//! lines starting with `++` configure the adapter, other lines go to the instrument.

use std::io::{Read, Write};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use anyhow::{anyhow, bail, Context, Result};
use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter, WriteType};
use btleplug::platform::{Manager, Peripheral};
use futures::StreamExt;
use tokio::io::AsyncWriteExt;
use tokio::sync::mpsc;
use uuid::Uuid;

pub const TCP_PORT: u16 = 5025;
pub const SERVICE: Uuid = Uuid::from_u128(0xB72B0001_CE61_48A9_8A39_81A476335192);
pub const CHAR_COMMAND: Uuid = Uuid::from_u128(0xB72B0002_CE61_48A9_8A39_81A476335192);
pub const CHAR_RESPONSE: Uuid = Uuid::from_u128(0xB72B0003_CE61_48A9_8A39_81A476335192);
const USB_VID: u16 = 0x2e8a;

/// A way to reach an adapter.
#[derive(Clone, Debug)]
pub enum Target {
    /// Serial port path, e.g. /dev/cu.usbmodem1101 or COM5
    Usb(String),
    /// Host name or address, optionally with :port (default 5025)
    Tcp(String),
    /// BLE peripheral id from [`scan_ble`]; empty = first adapter found
    Ble(String),
}

/// An adapter found by [`list_usb`] or [`scan_ble`].
#[derive(Clone, Debug)]
pub struct Found {
    pub id: String,
    pub name: String,
    pub detail: String,
}

/// Serial ports that look like BusCraft adapters (Raspberry Pi vendor id, "BusCraft" product name).
pub fn list_usb() -> Result<Vec<Found>> {
    let mut out = Vec::new();
    for p in serialport::available_ports()? {
        // macOS lists every port twice; the call-out device (cu.*) is the one to open
        if p.port_name.starts_with("/dev/tty.") {
            continue;
        }
        if let serialport::SerialPortType::UsbPort(u) = &p.port_type {
            let product = u.product.clone().unwrap_or_default();
            if u.vid == USB_VID && product.contains("BusCraft") {
                out.push(Found {
                    id: p.port_name.clone(),
                    name: product,
                    detail: u.serial_number.clone().unwrap_or_default(),
                });
            }
        }
    }
    Ok(out)
}

async fn ble_central() -> Result<btleplug::platform::Adapter> {
    let manager = Manager::new().await?;
    manager.adapters().await?.into_iter().next().ok_or_else(|| anyhow!("no Bluetooth adapter"))
}

async fn ble_find(scan: Duration) -> Result<Vec<(Peripheral, Found)>> {
    let central = ble_central().await?;
    central.start_scan(ScanFilter { services: vec![SERVICE] }).await?;
    tokio::time::sleep(scan).await;
    central.stop_scan().await.ok();
    let mut out = Vec::new();
    for p in central.peripherals().await? {
        let Some(props) = p.properties().await? else { continue };
        let name = props.local_name.unwrap_or_default();
        if !props.services.contains(&SERVICE) && !name.starts_with("BusCraft") {
            continue;
        }
        let rssi = props.rssi.map(|r| format!("{r} dBm")).unwrap_or_default();
        out.push((p.clone(), Found { id: p.id().to_string(), name, detail: rssi }));
    }
    Ok(out)
}

/// Bluetooth LE adapters advertising the BusCraft service.
pub async fn scan_ble(scan: Duration) -> Result<Vec<Found>> {
    Ok(ble_find(scan).await?.into_iter().map(|(_, f)| f).collect())
}

enum Writer {
    Serial(Arc<Mutex<Box<dyn serialport::SerialPort>>>),
    Tcp(tokio::net::tcp::OwnedWriteHalf),
    Ble(Peripheral, btleplug::api::Characteristic),
}

/// An open connection. Replies arrive as raw bytes; [`Connection::query`] collects them.
pub struct Connection {
    writer: Writer,
    rx: mpsc::UnboundedReceiver<Vec<u8>>,
    pub description: String,
}

impl Connection {
    pub async fn open(target: &Target) -> Result<Connection> {
        let (tx, rx) = mpsc::unbounded_channel();
        match target {
            Target::Usb(path) => {
                let port = serialport::new(path, 115_200)
                    .timeout(Duration::from_millis(50))
                    .open()
                    .with_context(|| format!("cannot open {path}"))?;
                let mut reader = port.try_clone()?;
                std::thread::spawn(move || {
                    let mut buf = [0u8; 512];
                    loop {
                        match reader.read(&mut buf) {
                            Ok(0) => {}
                            Ok(n) => {
                                if tx.send(buf[..n].to_vec()).is_err() {
                                    break;
                                }
                            }
                            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                                if tx.is_closed() {
                                    break;
                                }
                            }
                            Err(_) => break,
                        }
                    }
                });
                Ok(Connection {
                    writer: Writer::Serial(Arc::new(Mutex::new(port))),
                    rx,
                    description: format!("USB {path}"),
                })
            }
            Target::Tcp(host) => {
                let addr = if host.contains(':') { host.clone() } else { format!("{host}:{TCP_PORT}") };
                // The adapter has no IPv6, while mDNS names often resolve to a link-local IPv6 address first.
                let mut addrs: Vec<std::net::SocketAddr> =
                    tokio::net::lookup_host(&addr).await.with_context(|| format!("cannot resolve {addr}"))?.collect();
                addrs.sort_by_key(|a| a.is_ipv6());
                let mut stream = None;
                for a in &addrs {
                    if let Ok(Ok(s)) = tokio::time::timeout(Duration::from_secs(3), tokio::net::TcpStream::connect(a)).await {
                        stream = Some(s);
                        break;
                    }
                }
                let stream = stream.ok_or_else(|| anyhow!("{addr}: no answer"))?;
                stream.set_nodelay(true)?;
                let (mut read, write) = stream.into_split();
                tokio::spawn(async move {
                    use tokio::io::AsyncReadExt;
                    let mut buf = [0u8; 1024];
                    while let Ok(n) = read.read(&mut buf).await {
                        if n == 0 || tx.send(buf[..n].to_vec()).is_err() {
                            break;
                        }
                    }
                });
                Ok(Connection { writer: Writer::Tcp(write), rx, description: format!("network {addr}") })
            }
            Target::Ble(id) => {
                let found = ble_find(Duration::from_secs(3)).await?;
                let (p, f) = found
                    .into_iter()
                    .find(|(_, f)| id.is_empty() || &f.id == id)
                    .ok_or_else(|| anyhow!("BusCraft not found over Bluetooth"))?;
                p.connect().await?;
                p.discover_services().await?;
                let chars = p.characteristics();
                let cmd = chars.iter().find(|c| c.uuid == CHAR_COMMAND).cloned().ok_or_else(|| anyhow!("no Command characteristic"))?;
                let resp = chars.iter().find(|c| c.uuid == CHAR_RESPONSE).cloned().ok_or_else(|| anyhow!("no Response characteristic"))?;
                p.subscribe(&resp).await?;
                let mut notes = p.notifications().await?;
                tokio::spawn(async move {
                    while let Some(n) = notes.next().await {
                        if n.uuid == CHAR_RESPONSE && tx.send(n.value).is_err() {
                            break;
                        }
                    }
                });
                Ok(Connection { writer: Writer::Ble(p, cmd), rx, description: format!("Bluetooth {}", f.name) })
            }
        }
    }

    /// Sends one line. Over Bluetooth every write ends a line, so the newline is left out there.
    pub async fn send_line(&mut self, line: &str) -> Result<()> {
        match &mut self.writer {
            Writer::Serial(port) => {
                let mut p = port.lock().map_err(|_| anyhow!("serial port lock"))?;
                p.write_all(format!("{line}\n").as_bytes())?;
                p.flush()?;
            }
            Writer::Tcp(w) => w.write_all(format!("{line}\n").as_bytes()).await?,
            Writer::Ble(p, c) => p.write(c, line.as_bytes(), WriteType::WithResponse).await?,
        }
        Ok(())
    }

    /// Sends a line and returns what comes back until the link has been quiet for `quiet` (at most `max`).
    pub async fn query(&mut self, line: &str, quiet: Duration, max: Duration) -> Result<String> {
        while self.rx.try_recv().is_ok() {}
        self.send_line(line).await?;
        let mut out = Vec::new();
        let end = tokio::time::Instant::now() + max;
        loop {
            let left = end.saturating_duration_since(tokio::time::Instant::now());
            if left.is_zero() {
                break;
            }
            match tokio::time::timeout(quiet.min(left), self.rx.recv()).await {
                Ok(Some(chunk)) => out.extend(chunk),
                Ok(None) => bail!("connection closed"),
                Err(_) => break,
            }
        }
        Ok(String::from_utf8_lossy(&out).into_owned())
    }

    pub async fn close(self) {
        if let Writer::Ble(p, _) = &self.writer {
            p.disconnect().await.ok();
        }
    }
}
