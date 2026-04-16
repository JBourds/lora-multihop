#!/usr/bin/env python3
"""
decode_ring.py -- Decode ring routing protocol traces.

Usage:
    cd ~/repos/nexus-trace-parser
    just cli parse <trace.nxs> --output json-lines | python3 decode_ring.py

Reads JSON-lines from stdin (nexus-trace-parser output) and decodes the
ring routing protocol's link-layer frames and multihop messages.
"""
import json
import struct
import sys

# Protocol message types (MessageType enum)
MSG_TYPES = {
    0: "OpenCluster",
    1: "Follow",
    2: "Accept",
    3: "CloseCluster",
    4: "SendAcks",
    5: "Datagram",
    6: "Heartbeat",
}


def decode_frame(raw: bytes):
    """Decode link-layer FrameHeader (12 bytes) + length-prefixed messages."""
    if len(raw) < 12:
        return None, []
    cksum, payload_bytes, sender_time, sender_addr, msg_count = struct.unpack_from(
        "<HHIBBxx", raw
    )
    hdr = {
        "checksum": cksum,
        "payload_bytes": payload_bytes,
        "sender_time": sender_time,
        "sender_address": sender_addr,
        "message_count": msg_count,
    }

    msgs = []
    offset = 12
    for _ in range(msg_count):
        if offset >= len(raw):
            break
        msg_len = raw[offset]
        offset += 1
        if offset + msg_len > len(raw):
            break
        msg = decode_protocol_msg(raw[offset : offset + msg_len])
        if msg:
            msgs.append(msg)
        offset += msg_len
    return hdr, msgs


def decode_protocol_msg(data: bytes):
    """Decode a multihop protocol message (Header + Body)."""
    if len(data) < 6:
        return None
    seq, src, msg_type = struct.unpack_from("<IBB", data)
    type_name = MSG_TYPES.get(msg_type, f"Unknown({msg_type})")
    body = data[6:]
    info = {}

    if 6 == msg_type:  # Heartbeat
        if len(body) >= 1:
            info["ring"] = "UNKNOWN" if 255 == body[0] else body[0]
    elif 0 == msg_type:  # OpenCluster
        if len(body) >= 2:
            info["ring"] = body[0]
            info["node"] = body[1]
    elif 1 == msg_type:  # Follow
        if len(body) >= 2:
            info["clusterhead"] = body[0]
            info["follower"] = body[1]
    elif 2 == msg_type:  # Accept
        if len(body) >= 8:
            info["accepted"] = bool(body[0])
            info["follower"] = body[1]
            info["start_slot"] = body[2]
            info["rel_slot"] = body[3]
            info["remote_time_s"] = struct.unpack_from("<I", body, 4)[0]
    elif 3 == msg_type:  # CloseCluster
        if len(body) >= 1:
            info["nomination"] = body[0]
    elif 4 == msg_type:  # SendAcks
        if len(body) >= 2:
            start, end = body[0], body[1]
            n_acks = end - start
            acks = []
            for i in range(n_acks):
                off = 2 + i * 4
                if off + 4 <= len(body):
                    acks.append(struct.unpack_from("<I", body, off)[0])
            info["range"] = f"[{start},{end})"
            info["acks"] = acks
    elif 5 == msg_type:  # Datagram
        if len(body) >= 4:
            pkt_bytes, count, part = struct.unpack_from("<HBB", body)
            payload = body[4 : 4 + pkt_bytes]
            try:
                info["payload"] = payload.decode("ascii", errors="replace")
            except Exception:
                info["payload"] = payload.hex()
            info["part"] = f"{part}/{count}"
            info["bytes"] = pkt_bytes

    return {"type": type_name, "src": src, "seq": seq, **info}


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
        hdr, msgs = decode_frame(raw)
        if not hdr:
            continue

        ts = format_time(rec["timestep"])
        ev = "TX" if "Sent" in event else "RX" if "Recv" in event else "DROP"
        node = rec["node"]
        ch = rec["channel"]

        for msg in msgs:
            fields = " ".join(
                f"{k}={v}" for k, v in msg.items() if k not in ("type", "src", "seq")
            )
            bit_err = " BIT_ERR" if rec.get("bit_errors") else ""
            print(
                f"  {ts} {ev:4s} {node:12s} ch={ch:10s} "
                f"from=addr{msg['src']:<4d} {msg['type']:14s} {fields}{bit_err}"
            )


if __name__ == "__main__":
    main()
