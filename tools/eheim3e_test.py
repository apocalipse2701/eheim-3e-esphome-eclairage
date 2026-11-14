#!/usr/bin/env python3
"""
Test LECTURE SEULE du protocole EHEIM professionel 3e
(reconstruit depuis Eheim.Filter.Driver.dll, ControlCenter v2.3.0.0).

Usage : pip install pyserial
        python eheim3e_test.py COM3        (Windows)
        python eheim3e_test.py /dev/ttyUSB0 (Linux)

N'envoie que des commandes de lecture (0x01, 0x16, 0x13).
"""
import struct, sys, time
import serial

START = 0x55
DEV_PUMP = 0x60      # adresse utilisée pour toutes les commandes normales
DEV_BCAST = 0xFF     # adresse utilisée pour CMD_GET_DEVICEID (firmware)

CMD_GET_DEVICEID   = 0x01  # -> réponse cmd 0xFF, data[0].data[1] = version firmware
CMD_GET_ACTUALDATA = 0x13  # -> réponse cmd 0x10, 60 octets
CMD_GET_PUMPVERSION= 0x16  # -> réponse cmd 0x90 (ACK), data[1] + 2000 = type (2074...)

PUMP_MODES = {1:"NORMAL", 2:"AUTOOFF", 4:"NACHT (Bio)", 8:"PULS", 16:"REGLOFF (manuel)",
              32:"PROGMODE", 64:"EXTERNAL", 128:"HIDE_DSP", 256:"ROTOR_ERR",
              512:"AIR_ERR", 1024:"TEMP_ERR", 8192:"CAL_RECALC", 16384:"CALIBRATION",
              32768:"RESET"}

# Débit (l/h) par palier, index = SollStep + 1 (FilterParametersHelper.CalculateFlow)
FLOW_TABLES = {
    2074: [585, 400, 440, 480, 515, 550, 585, 620, 650, 680, 710, 740, 770, 800, 830, 860],
    2076: [650, 400, 460, 515, 565, 610, 650, 690, 730, 770, 805, 840, 875, 910, 945, 980],
    2078: [700, 400, 470, 540, 600, 650, 700, 745, 785, 825, 865, 905, 945, 985, 1025, 1065],
}
pump_type = 2076

# FilterData : struct Pack=1, little-endian, 51 octets (h=int16, B=byte, i=int32)
FIELDS = [("Frequenz","h"),("Frequenz_Soll","h"),("Current","h"),("Peak_Current","h"),
          ("CalcCurrent","h"),("DFSFaktor","h"),("DFS","h"),("Drehzahl","B"),
          ("Drehrichtung","B"),("Pulsbreite","B"),("PumpeOn","B"),("KeyState","B"),
          ("Sensor","B"),("LED_Data","h"),("PumpeMode","h"),("MenuMode","B"),
          ("Status","B"),("SollStep","B"),("DFSCalcDiff","B"),("Regler_DFSSoll","h"),
          ("Regler_FreqCalc","h"),("Regler_FreqSet","h"),("DFS_Stunde","h"),
          ("DFS_Tag","h"),("Winkel","h"),("RunTime","i"),("ActualTime","i"),
          ("WartungHours","h"),("WartungMonth","B")]
FMT = "<" + "".join(t for _, t in FIELDS)   # 51 octets

def frame(dev, cmd, data=b"\x00", length=5):
    """Trame = 0x55, adresse, longueur totale, commande, données. Pas de checksum."""
    f = bytes([START, dev, length, cmd]) + data
    return f[:length].ljust(length, b"\x00")

def read_frames(ser, tx, timeout=0.6):
    """Lit tout ce qui arrive pendant 'timeout' s, découpe en trames
    (0x55, adresse 0x60/0xFF, longueur) et ignore l'écho de notre propre trame
    (bus RS485 half-duplex : l'adaptateur relit ce qu'il envoie)."""
    t0 = time.time(); raw = bytearray()
    while time.time() - t0 < timeout:
        raw += ser.read(256)
    frames, i = [], 0
    while i + 3 <= len(raw):
        if raw[i] == START and raw[i+1] in (DEV_PUMP, DEV_BCAST) and raw[i+2] >= 4:
            n = raw[i+2]
            if i + n > len(raw):
                break
            frames.append(bytes(raw[i:i+n])); i += n
        else:
            i += 1
    replies = [f for f in frames if f != tx]
    return bytes(raw), replies

def xfer(ser, dev, cmd, retries=3):
    tx = frame(dev, cmd)
    for _ in range(retries):
        ser.reset_input_buffer()
        ser.write(tx)
        raw, replies = read_frames(ser, tx)
        print(f"TX  {tx.hex(' ')}")
        print(f"RAW {raw.hex(' ') if raw else '(rien)'}")
        if replies:
            print(f"RX  {replies[0].hex(' ')}")
            return replies[0]
        print("RX  (aucune réponse de la pompe, seulement l'écho)")
        time.sleep(0.2)
    return None

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else "COM3"
    ser = serial.Serial(port, 115200, bytesize=8, parity="N", stopbits=1, timeout=0.05)

    print("\n== Firmware (CMD_GET_DEVICEID)")
    rx = xfer(ser, DEV_BCAST, CMD_GET_DEVICEID)
    if rx and rx[3] == 0xFF and len(rx) >= 6:
        print(f"Firmware : {rx[4]}.{rx[5]}")

    print("\n== Type de pompe (CMD_GET_PUMPVERSION)")
    rx = xfer(ser, DEV_PUMP, CMD_GET_PUMPVERSION)
    global pump_type
    if rx and rx[3] == 0x90 and len(rx) >= 6:
        pump_type = 2000 + rx[5]
        print(f"Type : {pump_type}")

    print("\n== Données temps réel (CMD_GET_ACTUALDATA), Ctrl+C pour arrêter")
    while True:
        rx = xfer(ser, DEV_PUMP, CMD_GET_ACTUALDATA)
        if rx and rx[3] == 0x10 and len(rx) >= 4 + 51:
            vals = dict(zip([n for n, _ in FIELDS], struct.unpack_from(FMT, rx, 4)))
            mode = vals["PumpeMode"] & 0xFFFF
            names = [v for k, v in PUMP_MODES.items() if mode & k] or ["?"]
            for k, v in vals.items():
                print(f"  {k:16} {v}")
            # Conversions reprises de ControlCenter (Eheim.Filter.Control / Common)
            table = FLOW_TABLES.get(pump_type, FLOW_TABLES[2076])
            step = vals["SollStep"]
            consigne = table[step + 1] if 0 <= step + 1 < len(table) else None
            since_h = vals["ActualTime"] // 60
            until_h = vals["WartungHours"]
            pollution = since_h * 100 // (since_h + until_h) if since_h + until_h > 0 else 0
            print("  ---- valeurs lisibles ----")
            print(f"  Débit de consigne    : {consigne} l/h (palier {step})")
            print(f"  Débit mesuré         : {vals['DFS'] * 60 // 100} l/h")
            print(f"  Vitesse rotor        : {vals['Frequenz'] // 100 * 60} tr/min")
            print(f"  Temps fonctionnement : {vals['RunTime'] // 60} h")
            print(f"  Depuis entretien     : {since_h} h | avant entretien : {until_h} h")
            print(f"  Encrassement         : {pollution} %")
            print(f"  Mode (bits bruts)    : {mode} -> {', '.join(names)}")
        time.sleep(2)

if __name__ == "__main__":
    main()
