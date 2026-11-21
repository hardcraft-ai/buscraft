// BusCraft host client. Talks to the adapter over BLE and exposes the
// instrument to the host as a raw TCP socket (port 5025) for VISA tools.

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.iter().any(|a| a == "--version" || a == "-V") {
        println!("buscraft {}", env!("CARGO_PKG_VERSION"));
        return;
    }
    println!("buscraft {}: client not implemented yet", env!("CARGO_PKG_VERSION"));
}
