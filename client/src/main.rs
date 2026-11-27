// BusCraft command line client.
//   buscraft list                         adapters on USB and Bluetooth
//   buscraft usb [PORT] LINE...           send lines over USB serial (first adapter if PORT is left out)
//   buscraft net [HOST] LINE...           over the network (default buscraft.local:5025)
//   buscraft ble LINE...                  over Bluetooth LE (first adapter found)

use std::time::Duration;

use anyhow::{bail, Result};
use buscraft::{list_usb, scan_ble, Connection, Target};

#[tokio::main]
async fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().skip(1).collect();
    let Some(cmd) = args.first() else {
        println!("usage: buscraft list | usb [PORT] LINE... | net [HOST] LINE... | ble LINE...  (buscraft {})",
                 env!("CARGO_PKG_VERSION"));
        return Ok(());
    };
    if cmd == "--version" || cmd == "-V" {
        println!("buscraft {}", env!("CARGO_PKG_VERSION"));
        return Ok(());
    }
    let rest = &args[1..];
    // A first argument that is not a protocol line names the port or host.
    let named = rest.first().filter(|a| !a.starts_with("++") && !a.contains('?') && !a.contains(' ')).cloned();
    let lines = if named.is_some() { &rest[1..] } else { rest };
    let target = match cmd.as_str() {
        "list" => {
            for f in list_usb()? {
                println!("usb  {}  {} {}", f.id, f.name, f.detail);
            }
            for f in scan_ble(Duration::from_secs(3)).await? {
                println!("ble  {}  {} {}", f.id, f.name, f.detail);
            }
            return Ok(());
        }
        "usb" => Target::Usb(match named {
            Some(p) => p,
            None => match list_usb()?.into_iter().next() {
                Some(f) => f.id,
                None => bail!("no BusCraft adapter on USB"),
            },
        }),
        "net" => Target::Tcp(named.unwrap_or_else(|| "buscraft.local".into())),
        "ble" => Target::Ble(String::new()),
        other => bail!("unknown command {other}"),
    };
    let mut c = Connection::open(&target).await?;
    eprintln!("connected: {}", c.description);
    for line in lines {
        let reply = c.query(line, Duration::from_millis(300), Duration::from_secs(5)).await?;
        println!("> {line}");
        print!("{reply}");
    }
    c.close().await;
    Ok(())
}
