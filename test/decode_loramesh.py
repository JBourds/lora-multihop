#!/usr/bin/env python3
"""
decode_loramesh.py -- Decode LoRaMesher protocol traces.

Usage:
    cd ~/repos/nexus-trace-parser
    just cli parse <trace.nxs> --output json-lines | python3 decode_loramesh.py

Reads JSON-lines from stdin (nexus-trace-parser output) and decodes
LoRaMesher's wire-format packets.

Wire format (all packed, little-endian):
  PacketHeader (7 bytes):
    dst:        u16
    src:        u16
    type:       u8  (bitmask)
    id:         u8
    packetSize: u8  (total packet size in bytes)

  For DATA_P (type & 0x02):
    via:     u16  (RouteDataPacket extends header)
    payload: [u8] (packetSize - 9 bytes)

  For HELLO_P (type == 0x04):
    nodeRole: u8
    NetworkNode[] (each 4 bytes: address u16, metric u8, role u8)

  For NEED_ACK_P / ACK_P / XL_DATA_P / SYNC_P (ControlPacket):
    via:     u16
    seq_id:  u8
    number:  u16
    payload: [u8]
"""
import json
import struct
import sys

# Packet type bitmask flags
NEED_ACK_P = 0b00000011
DATA_P     = 0b00000010
HELLO_P    = 0b00000100
ACK_P      = 0b00001010
XL_DATA_P  = 0b00010010
LOST_P     = 0b00100010
SYNC_P     = 0b01000010


def type_name(t: int) -> str:
    names = []
    if t == HELLO_P:
        return "HELLO"
    if t & SYNC_P == SYNC_P:
        names.append("SYNC")
    if t & LOST_P == LOST_P:
        names.append("LOST")
    if t & XL_DATA_P == XL_DATA_P:
        names.append("XL_DATA")
    if t & ACK_P == ACK_P:
        names.append("ACK")
    if t & NEED_ACK_P == NEED_ACK_P:
        names.append("NEED_ACK")
    if t & DATA_P == DATA_P and not names:
        names.append("DATA")
    return "|".join(names) if names else f"0x{t:02x}"


def decode_packet(raw: bytes):
    """Decode a LoRaMesher packet from raw bytes."""
    if len(raw) < 7:
        return None
    dst, src, ptype, pid, pkt_size = struct.unpack_from("<HHBBB", raw)
    info = {
        "dst": f"0x{dst:04X}",
        "src": f"0x{src:04X}",
        "type": type_name(ptype),
        "type_raw": ptype,
        "id": pid,
        "size": pkt_size,
    }

    body = raw[7:]

    if ptype == HELLO_P:
        # RoutePacket: nodeRole(1) + NetworkNode[](4 each)
        if len(body) >= 1:
            info["role"] = body[0]
            nodes = []
            offset = 1
            while offset + 4 <= len(body):
                addr, metric, role = struct.unpack_from("<HBB", body, offset)
                nodes.append({"addr": f"0x{addr:04X}", "metric": metric, "role": role})
                offset += 4
            info["routes"] = nodes
    elif ptype & DATA_P:
        # RouteDataPacket: via(2) + payload
        if len(body) >= 2:
            via = struct.unpack_from("<H", body, 0)[0]
            info["via"] = f"0x{via:04X}"
            payload = body[2:]

            # Check if this is a ControlPacket (has NEED_ACK, ACK, XL_DATA, SYNC flags)
            if ptype & (NEED_ACK_P | ACK_P | XL_DATA_P | SYNC_P) and len(payload) >= 3:
                seq_id = payload[0]
                number = struct.unpack_from("<H", payload, 1)[0]
                info["seq_id"] = seq_id
                info["number"] = number
                payload = payload[3:]

            if payload:
                try:
                    text = payload.decode("ascii", errors="replace")
                    if all(32 <= c < 127 or c in (10, 13) for c in payload):
                        info["payload"] = text
                    else:
                        info["payload"] = payload.hex()
                except Exception:
                    info["payload"] = payload.hex()

    return info


def format_time(ms):
    return f"{ms / 1000:.3f}s"


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line or not line.startswith("{"):
            continue
        rec = json.loads(line)
        event = rec.get("event", "")
        if "Message" not in event:
            continue

        raw = bytes.fromhex(rec["data_hex"])
        pkt = decode_packet(raw)
        if not pkt:
            continue

        ts = format_time(rec["timestep"])
        ev = "TX" if "Sent" in event else "RX" if "Recv" in event else "DROP"
        node = rec["node"]

        # Build display fields
        fields = []
        fields.append(f"dst={pkt['dst']}")
        fields.append(f"src={pkt['src']}")
        if "via" in pkt and pkt["via"] != pkt["src"]:
            fields.append(f"via={pkt['via']}")
        fields.append(f"id={pkt['id']}")

        if "routes" in pkt:
            route_strs = [
                f"{r['addr']}(m={r['metric']})" for r in pkt["routes"]
            ]
            fields.append(f"routes=[{', '.join(route_strs)}]")
        if "seq_id" in pkt:
            fields.append(f"seq={pkt['seq_id']}")
            fields.append(f"num={pkt['number']}")
        if "payload" in pkt:
            fields.append(f"payload={pkt['payload'][:40]}")
        if "role" in pkt:
            fields.append(f"role={pkt['role']}")

        bit_err = " BIT_ERR" if rec.get("bit_errors") else ""
        print(
            f"  {ts} {ev:4s} {node:12s} "
            f"{pkt['type']:10s} {' '.join(fields)}{bit_err}"
        )


if __name__ == "__main__":
    main()
