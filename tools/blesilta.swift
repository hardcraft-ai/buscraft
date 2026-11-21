// BLE-putki GPIB-pulikan rt0s-ominaisuuteen (B72B0006-…): stdin-rivit pulikalle, pulikan JSON-rivit stdoutiin.
// Yhteyden tila stderriin ja stdoutiin {"silta":"yhdistetty"|"irti",...}. Yhdistää uudelleen, kun yhteys katkeaa.
//   swiftc -O tools/blesilta.swift -o tools/build/blesilta && tools/build/blesilta [nimen alku, oletus p2pS]
//   echo '{"mid":1,"req":["ping",{}]}' | tools/build/blesilta
import CoreBluetooth
import Foundation

let base = "-CE61-48A9-8A39-81A476335192"
let palvelu = CBUUID(string: "B72B0001" + base)
let cRt0s = CBUUID(string: "B72B0006" + base)
let nimiAlku = CommandLine.arguments.count > 1 ? CommandLine.arguments[1] : "p2pS"
setvbuf(stdout, nil, _IOLBF, 0)

func loki(_ s: String) { FileHandle.standardError.write((s + "\n").data(using: .utf8)!) }
func tila(_ t: String, _ lisa: String = "") { print("{\"silta\":\"\(t)\"\(lisa)}") }

class Silta: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
  var cm: CBCentralManager!
  var p: CBPeripheral?
  var ch: CBCharacteristic?
  var jono: [Data] = []
  var tulo = Data()
  override init() { super.init(); cm = CBCentralManager(delegate: self, queue: nil) }

  func centralManagerDidUpdateState(_ c: CBCentralManager) {
    loki("bt tila \(c.state.rawValue)")
    if c.state == .poweredOn { etsi() }
  }
  func etsi() { cm.scanForPeripherals(withServices: nil) }
  func centralManager(_ c: CBCentralManager, didDiscover p: CBPeripheral, advertisementData a: [String: Any], rssi: NSNumber) {
    guard let nimi = a[CBAdvertisementDataLocalNameKey] as? String ?? p.name, nimi.hasPrefix(nimiAlku) else { return }
    loki("löytyi \(nimi) rssi \(rssi)")
    c.stopScan(); self.p = p; p.delegate = self; c.connect(p)
  }
  func centralManager(_ c: CBCentralManager, didConnect p: CBPeripheral) { p.discoverServices([palvelu]) }
  func centralManager(_ c: CBCentralManager, didFailToConnect p: CBPeripheral, error: Error?) { loki("yhteys epäonnistui"); etsi() }
  func centralManager(_ c: CBCentralManager, didDisconnectPeripheral p: CBPeripheral, error: Error?) {
    loki("irti \(error?.localizedDescription ?? "")"); ch = nil; tulo = Data(); tila("irti"); etsi()
  }
  func peripheral(_ p: CBPeripheral, didDiscoverServices e: Error?) {
    guard let s = p.services?.first else { loki("palvelua ei löytynyt"); cm.cancelPeripheralConnection(p); return }
    p.discoverCharacteristics([cRt0s], for: s)
  }
  func peripheral(_ p: CBPeripheral, didDiscoverCharacteristicsFor s: CBService, error: Error?) {
    guard let c = s.characteristics?.first(where: { $0.uuid == cRt0s }) else { loki("rt0s-ominaisuutta ei löytynyt"); return }
    p.setNotifyValue(true, for: c)
  }
  func peripheral(_ p: CBPeripheral, didUpdateNotificationStateFor c: CBCharacteristic, error: Error?) {
    guard c.isNotifying else { return }
    ch = c
    tila("yhdistetty", ",\"mtu\":\(p.maximumWriteValueLength(for: .withResponse) + 3)")
    tyhjenna()
  }
  func peripheral(_ p: CBPeripheral, didUpdateValueFor c: CBCharacteristic, error: Error?) {
    tulo.append(c.value ?? Data())
    while let i = tulo.firstIndex(of: 0x0A) {
      let rivi = tulo[tulo.startIndex..<i]
      tulo = Data(tulo[(i + 1)...])
      if let t = String(data: rivi, encoding: .utf8) { print(t) }
    }
  }
  func laheta(_ rivi: String) { jono.append((rivi + "\n").data(using: .utf8)!); tyhjenna() }
  func tyhjenna() {
    guard let p = p, let c = ch else { return }
    let koko = p.maximumWriteValueLength(for: .withResponse)
    while !jono.isEmpty {
      let d = jono.removeFirst()
      var i = 0
      while i < d.count { p.writeValue(d.subdata(in: i..<min(i + koko, d.count)), for: c, type: .withResponse); i += koko }
    }
  }
}

let silta = Silta()
DispatchQueue.global().async {
  while let rivi = readLine() {
    DispatchQueue.main.async { silta.laheta(rivi) }
  }
  // stdin loppui: odotetaan vastaukset ja lopetetaan (putkikäyttö)
  DispatchQueue.main.asyncAfter(deadline: .now() + 3) { exit(0) }
}
RunLoop.main.run()
