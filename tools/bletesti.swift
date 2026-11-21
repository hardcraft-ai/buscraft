// GPIB-pulikan GATT-testi Macilta: etsii palvelun B72B0001-…, tilaa Response/Eventin ja kirjoittaa komennot.
//   swift tools/bletesti.swift "++ver" "++addr 22" "++addr" "*IDN?"
import CoreBluetooth
import Foundation

let base = "-CE61-48A9-8A39-81A476335192"
let palvelu = CBUUID(string: "B72B0001" + base)
let cKomento = CBUUID(string: "B72B0002" + base)
let cVastaus = CBUUID(string: "B72B0003" + base)
let cTapahtuma = CBUUID(string: "B72B0004" + base)
let cTila = CBUUID(string: "B72B0005" + base)
var komennot = Array(CommandLine.arguments.dropFirst())
if komennot.isEmpty { komennot = ["++ver", "++addr 22", "++addr", "*IDN?", "++spoll", "++xyz"] }

class Testi: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
  var cm: CBCentralManager!
  var p: CBPeripheral?
  var kom: CBCharacteristic?
  override init() { super.init(); cm = CBCentralManager(delegate: self, queue: nil) }

  func centralManagerDidUpdateState(_ c: CBCentralManager) {
    print("bt tila \(c.state.rawValue)")
    if c.state == .poweredOn { c.scanForPeripherals(withServices: nil) }
  }
  func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData a: [String: Any], rssi: NSNumber) {
    guard let nimi = a[CBAdvertisementDataLocalNameKey] as? String ?? p.name, nimi.hasPrefix("p2pS") || nimi.hasPrefix("GPIB") else { return }
    print("löytyi \(nimi) rssi \(rssi)")
    c.stopScan(); self.p = p; p.delegate = self; c.connect(p)
  }
  func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) { print("yhdistetty, mtu \(p.maximumWriteValueLength(for: .withoutResponse) + 3)"); p.discoverServices([palvelu]) }
  func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) { print("yhteys epäonnistui \(String(describing: error))"); exit(1) }
  func peripheral(_ p: CBPeripheral, didDiscoverServices e: Error?) {
    guard let s = p.services?.first else { print("palvelua ei löytynyt"); exit(1) }
    p.discoverCharacteristics(nil, for: s)
  }
  func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
    for c in s.characteristics ?? [] {
      if c.uuid == cKomento { kom = c }
      if c.uuid == cVastaus || c.uuid == cTapahtuma { p.setNotifyValue(true, for: c) }
      if c.uuid == cTila { p.readValue(for: c) }
    }
    DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.seuraava() }
  }
  func seuraava() {
    guard let k = kom, let p = p else { return }
    if komennot.isEmpty {
      DispatchQueue.main.asyncAfter(deadline: .now() + 1) { self.cm.cancelPeripheralConnection(p); exit(0) }
      return
    }
    let rivi = komennot.removeFirst()
    print("> \(rivi)")
    p.writeValue(rivi.data(using: .utf8)!, for: k, type: .withResponse)
    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) { self.seuraava() }
  }
  func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
    let t = String(data: c.value ?? Data(), encoding: .utf8) ?? "?"
    let nimi = c.uuid == cVastaus ? "vastaus" : c.uuid == cTapahtuma ? "tapahtuma" : "tila"
    print("< \(nimi): \(t.replacingOccurrences(of: "\n", with: "\\n"))")
  }
}

let t = Testi()
DispatchQueue.main.asyncAfter(deadline: .now() + 30) { print("aikaraja"); exit(2) }
RunLoop.main.run()
