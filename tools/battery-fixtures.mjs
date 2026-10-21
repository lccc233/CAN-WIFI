// Synthetic protocol vectors; never transmitted to CAN hardware.
export function batteryFrame(id, data, overrides = {}) {
  return { id, data, t: 9950, dlc: 8, ext: true, seq: 0, ...overrides };
}
export function batteryPacket(overrides = {}) {
  const battery = [
    batteryFrame('0x1884EFF3', 'C8 FA F8 2A 70 17 00 10'), // 80%, +100 A, 600 V
    batteryFrame('0x1886EFF3', '4B 3C 44 01 02 03 04 00'), // 35 / 20 / 28 deg C
    batteryFrame('0x1881EFF3', '00 00 00 00 00 00 00 00'),
    batteryFrame('0x1882EFF3', '00 00 00 00 00 00 00 00')
  ];
  return { boot: 'battery-test', clk: { boot: 10000, sync: false, ep: 0 },
    total: 4, freqs: [], messages: structuredClone(battery), battery, ...overrides };
}
