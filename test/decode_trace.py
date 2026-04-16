#!/usr/bin/env python3
"""
decode_trace.py

Decode a nexus .nxs trace file to print human-readable protocol events.

The trace uses bincode v2 standard config (varint integers, LE).
"""
import struct
import sys

# Protocol constants
NODE_NAMES = []
CHANNEL_NAMES = []

# Channel index -> CDMA code reverse map (built from hash_cdma_code)
def build_cdma_reverse_map(channel_names):
    """Build channel_index -> cdma_code mapping."""
    sf_to_idx = {}
    for i, name in enumerate(channel_names):
        sf_num = int(name.replace("lora_sf", ""))
        sf_to_idx[sf_num] = i

    idx_to_cdma = {}
    for cdma_code in range(7):
        sf = cdma_code % 7 + 6
        if sf in sf_to_idx:
            idx_to_cdma[sf_to_idx[sf]] = cdma_code
    return idx_to_cdma

# Message types
MSG_TYPES = {
    0: "OpenCluster",
    1: "Follow",
    2: "Accept",
    3: "CloseCluster",
    4: "SendAcks",
    5: "Datagram",
    6: "Heartbeat",
    7: "Unknown",
}

EVENT_TYPES = {
    0: "MessageSent",
    1: "MessageRecv",
    2: "MessageDropped",
    3: "PositionUpdate",
    4: "EnergyUpdate",
    5: "MotionUpdate",
}

DROP_REASONS = {
    0: "BelowSensitivity",
    1: "PacketLoss",
    2: "TtlExpired",
    3: "BufferFull",
}


class BincodeReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def read_bytes(self, n):
        if self.pos + n > len(self.data):
            raise EOFError()
        result = self.data[self.pos : self.pos + n]
        self.pos += n
        return result

    def read_u8(self):
        return self.read_bytes(1)[0]

    def read_u16_le(self):
        return struct.unpack("<H", self.read_bytes(2))[0]

    def read_u32_le(self):
        return struct.unpack("<I", self.read_bytes(4))[0]

    def read_u64_le(self):
        return struct.unpack("<Q", self.read_bytes(8))[0]

    def read_varint(self):
        """Read a bincode v2 standard varint."""
        first = self.read_u8()
        if first < 251:
            return first
        elif first == 251:  # u16 follows
            return self.read_u16_le()
        elif first == 252:  # u32 follows
            return self.read_u32_le()
        elif first == 253:  # u64 follows
            return self.read_u64_le()
        else:
            raise ValueError(f"Invalid varint marker: {first}")

    def read_vec_u8(self):
        length = self.read_varint()
        return self.read_bytes(length)

    def read_string(self):
        return self.read_vec_u8().decode("utf-8")

    def eof(self):
        return self.pos >= len(self.data)


def decode_frame_header(data):
    """Decode a FrameHeader from raw bytes.
    struct FrameHeader {
        checksum: u16,
        payload_bytes: u16,
        sender_time: u32,
        sender_address: u8,
        message_count: u8,
        _pad: [u8; 2],
    }
    """
    if len(data) < 12:
        return None, None
    checksum, payload_bytes, sender_time, sender_addr, msg_count = struct.unpack_from(
        "<HHIBBxx", data
    )
    return {
        "checksum": checksum,
        "payload_bytes": payload_bytes,
        "sender_time": sender_time,
        "sender_address": sender_addr,
        "message_count": msg_count,
    }, 12


def decode_messages(data, frame_hdr):
    """Decode length-prefixed messages from frame payload."""
    msgs = []
    offset = 0
    for _ in range(frame_hdr["message_count"]):
        if offset >= len(data):
            break
        msg_len = data[offset]
        offset += 1
        if offset + msg_len > len(data):
            break
        msg_data = data[offset : offset + msg_len]
        offset += msg_len
        msg = decode_protocol_message(msg_data)
        if msg:
            msgs.append(msg)
    return msgs


def decode_protocol_message(data):
    """Decode a protocol message (Header + Body)."""
    if len(data) < 6:
        return None
    # Header: sequence(u32) + src(u8) + type(u8)
    seq, src, msg_type = struct.unpack_from("<IBB", data)
    type_name = MSG_TYPES.get(msg_type, f"Unknown({msg_type})")
    body_data = data[6:]
    body = {}

    if msg_type == 6:  # Heartbeat
        if len(body_data) >= 1:
            body["ring"] = body_data[0]
    elif msg_type == 0:  # OpenCluster
        if len(body_data) >= 11:
            body["ring"] = body_data[0]
            body["node"] = body_data[1]
            body["ring_start"] = body_data[2]
            body["counts"] = list(body_data[3:11])
    elif msg_type == 1:  # Follow
        if len(body_data) >= 2:
            body["clusterhead"] = body_data[0]
            body["follower"] = body_data[1]
    elif msg_type == 2:  # Accept
        if len(body_data) >= 8:
            body["accepted"] = bool(body_data[0])
            body["follower"] = body_data[1]
            body["start_slot"] = body_data[2]
            body["relative_slot"] = body_data[3]
            body["remote_time_s"] = struct.unpack_from("<I", body_data, 4)[0]
    elif msg_type == 3:  # CloseCluster
        if len(body_data) >= 10:
            body["nomination"] = body_data[0]
            body["ring_start"] = body_data[1]
            body["counts"] = list(body_data[2:10])
    elif msg_type == 4:  # SendAcks
        if len(body_data) >= 2:
            body["start"] = body_data[0]
            body["end"] = body_data[1]
            n_acks = body["end"] - body["start"]
            acks = []
            for i in range(n_acks):
                off = 2 + i * 4
                if off + 4 <= len(body_data):
                    acks.append(struct.unpack_from("<I", body_data, off)[0])
            body["acks"] = acks
    elif msg_type == 5:  # Datagram
        if len(body_data) >= 4:
            body["packet_bytes"], body["count"], body["part"] = struct.unpack_from(
                "<HBB", body_data
            )
            payload = body_data[4 : 4 + body["packet_bytes"]]
            try:
                body["payload"] = payload.decode("ascii", errors="replace")
            except Exception:
                body["payload"] = payload.hex()

    return {
        "type": type_name,
        "sequence": seq,
        "src": src,
        "body": body,
    }


def format_time(ms):
    """Format milliseconds as seconds with 3 decimal places."""
    return f"{ms / 1000:.3f}s"


def main():
    trace_path = sys.argv[1] if len(sys.argv) > 1 else "/home/jordan/simulations/2026-03-12_23:45:04/trace.nxs"

    with open(trace_path, "rb") as f:
        data = f.read()

    reader = BincodeReader(data)

    # Read magic
    magic = reader.read_bytes(4)
    assert magic == b"NXTR", f"Bad magic: {magic}"

    # Read version
    version = reader.read_u16_le()
    assert version in (2, 3), f"Unsupported version: {version}"

    # Read header length
    header_len = reader.read_u32_le()

    # Read header (bincode-encoded TraceHeader)
    header_start = reader.pos
    # TraceHeader: node_names: Vec<String>, channel_names: Vec<String>,
    #              timestep_count: u64, node_max_nj: Vec<Option<u64>>
    n_nodes = reader.read_varint()
    node_names = [reader.read_string() for _ in range(n_nodes)]
    n_channels = reader.read_varint()
    channel_names = [reader.read_string() for _ in range(n_channels)]
    timestep_count = reader.read_varint()
    n_max_nj = reader.read_varint()
    node_max_nj = []
    for _ in range(n_max_nj):
        is_some = reader.read_u8()
        if is_some:
            node_max_nj.append(reader.read_varint())
        else:
            node_max_nj.append(None)

    # Skip to end of header
    reader.pos = header_start + header_len

    print(f"Trace: {trace_path}")
    print(f"Nodes ({n_nodes}): {node_names}")
    print(f"Channels ({n_channels}): {channel_names}")
    print(f"Timesteps: {timestep_count} ({timestep_count/1000:.1f}s)")
    print()

    cdma_map = build_cdma_reverse_map(channel_names)

    # Superframe params
    SLOT_S = 3
    SLOT_COUNT = 16
    FREQ_S = SLOT_S * SLOT_COUNT + 5  # 53s
    VTDMA_WINDOW = 5
    REUSE_DISTANCE = 3

    # Read records
    records = []
    while not reader.eof():
        try:
            timestep = reader.read_varint()
            event_disc = reader.read_varint()

            if event_disc == 0:  # MessageSent
                src_node = reader.read_varint()
                channel = reader.read_varint()
                data = reader.read_vec_u8()
                records.append({
                    "ts": timestep,
                    "event": "TX",
                    "node": node_names[src_node],
                    "node_idx": src_node,
                    "channel": channel_names[channel],
                    "channel_idx": channel,
                    "cdma": cdma_map.get(channel, "?"),
                    "data": data,
                })
            elif event_disc == 1:  # MessageRecv
                dst_node = reader.read_varint()
                channel = reader.read_varint()
                data = reader.read_vec_u8()
                bit_errors = reader.read_u8() if version >= 3 else 0
                records.append({
                    "ts": timestep,
                    "event": "RX",
                    "node": node_names[dst_node],
                    "node_idx": dst_node,
                    "channel": channel_names[channel],
                    "channel_idx": channel,
                    "cdma": cdma_map.get(channel, "?"),
                    "data": data,
                    "bit_errors": bool(bit_errors),
                })
            elif event_disc == 2:  # MessageDropped
                src_node = reader.read_varint()
                channel = reader.read_varint()
                reason = reader.read_varint()
                records.append({
                    "ts": timestep,
                    "event": "DROP",
                    "node": node_names[src_node],
                    "node_idx": src_node,
                    "channel": channel_names[channel],
                    "channel_idx": channel,
                    "cdma": cdma_map.get(channel, "?"),
                    "reason": DROP_REASONS.get(reason, f"Unknown({reason})"),
                    "data": None,
                })
            else:
                # Skip other event types
                break
        except EOFError:
            break

    print(f"Total records: {len(records)}")
    print()

    # Derive sim_start_epoch: the Unix epoch time corresponding to sim-time 0.
    # The link layer aligns windows to multiples of FREQ_S from Unix epoch 0
    # via next_time_interval(now, 53).  sender_time is the integer epoch second
    # when the frame was sent; sim-time (ts) gives the elapsed ms.  So:
    #   sim_start_epoch = sender_time - int(sim_time_s)
    # Then for any record:
    #   epoch_time = sim_start_epoch + int(sim_time_s)
    #   window_start = epoch_time - (epoch_time % FREQ_S)
    #   slot = (epoch_time - window_start) / SLOT_S
    sim_start_epoch = None
    for rec in records:
        if rec["event"] in ("TX", "RX") and rec.get("data") is not None:
            hdr, _ = decode_frame_header(rec["data"])
            if hdr:
                sim_s = int(rec["ts"] / 1000)
                sim_start_epoch = hdr["sender_time"] - sim_s
                first_epoch_offset = sim_start_epoch % FREQ_S
                print(f"Epoch calibration: sender_time={hdr['sender_time']}  "
                      f"sim_time={rec['ts']/1000:.3f}s  sim_start_epoch={sim_start_epoch}  "
                      f"epoch_phase={first_epoch_offset}")
                break

    if sim_start_epoch is None:
        print("WARNING: No TX/RX frames found; slot numbers will be simulation-relative")
        sim_start_epoch = 0

    def epoch_for_record(rec):
        """Convert sim-time to approximate epoch seconds."""
        return sim_start_epoch + int(rec["ts"] / 1000)

    def slot_for_record(rec):
        """Compute the link-layer slot index for a record, epoch-aligned."""
        epoch = epoch_for_record(rec)
        window_start = epoch - (epoch % FREQ_S)
        return (epoch - window_start) // SLOT_S

    def superframe_for_record(rec):
        """Compute superframe number (epoch-aligned window index)."""
        epoch = epoch_for_record(rec)
        return epoch // FREQ_S

    # Compute the first superframe number so we can display zero-indexed labels
    first_sf_num = None

    # Decode and print each event
    prev_superframe = -1
    for rec in records:
        sf_num = superframe_for_record(rec)
        slot_idx = slot_for_record(rec)

        if first_sf_num is None:
            first_sf_num = sf_num

        if sf_num != prev_superframe:
            sf_rel = sf_num - first_sf_num
            sf_start_epoch = sf_num * FREQ_S
            sf_start_sim = sf_start_epoch - sim_start_epoch
            sf_end_sim = sf_start_sim + FREQ_S
            print(f"\n{'='*80}")
            print(f"SUPERFRAME {sf_rel} [epoch={sf_num}] (sim={sf_start_sim}s - {sf_end_sim}s)")
            print(f"{'='*80}")
            prev_superframe = sf_num

        # Decode frame
        if rec.get("data") is None:
            # Drop event
            print(f"  {format_time(rec['ts'])} slot={slot_idx:2d} {rec['event']:4s} "
                  f"{rec['node']:12s} ch={rec['channel']:10s} cdma={rec['cdma']} "
                  f"reason={rec.get('reason', '?')}")
            continue

        frame_data = rec["data"]
        frame_hdr, hdr_sz = decode_frame_header(frame_data)
        if not frame_hdr:
            print(f"  {format_time(rec['ts'])} slot={slot_idx:2d} {rec['event']:4s} "
                  f"{rec['node']:12s} ch={rec['channel']:10s} cdma={rec['cdma']} "
                  f"[bad frame]")
            continue

        payload = frame_data[hdr_sz:]
        msgs = decode_messages(payload, frame_hdr)

        sender_name = node_names[frame_hdr["sender_address"]] if frame_hdr["sender_address"] < len(node_names) else f"addr={frame_hdr['sender_address']}"

        for msg in msgs:
            body_str = ""
            b = msg["body"]
            if msg["type"] == "Heartbeat":
                ring = b.get("ring", "?")
                ring_str = "UNKNOWN" if ring == 255 else str(ring)
                body_str = f"ring={ring_str}"
            elif msg["type"] == "OpenCluster":
                body_str = f"ring={b.get('ring','?')} node={b.get('node','?')}"
            elif msg["type"] == "Follow":
                body_str = f"ch={b.get('clusterhead','?')} follower={b.get('follower','?')}"
            elif msg["type"] == "Accept":
                body_str = (f"accepted={b.get('accepted','?')} follower={b.get('follower','?')} "
                           f"start_slot={b.get('start_slot','?')} rel_slot={b.get('relative_slot','?')}")
            elif msg["type"] == "CloseCluster":
                body_str = f"nomination={b.get('nomination','?')}"
            elif msg["type"] == "SendAcks":
                body_str = f"range=[{b.get('start','?')},{b.get('end','?')}) acks={b.get('acks','?')}"
            elif msg["type"] == "Datagram":
                body_str = f"part={b.get('part','?')}/{b.get('count','?')} bytes={b.get('packet_bytes','?')} payload={b.get('payload','?')[:40]}"

            err_flag = " BIT_ERR" if rec.get("bit_errors") else ""
            print(f"  {format_time(rec['ts'])} slot={slot_idx:2d} {rec['event']:4s} "
                  f"{rec['node']:12s} ch={rec['channel']:10s} cdma={rec['cdma']} "
                  f"from={sender_name:12s} {msg['type']:14s} {body_str}{err_flag}")


if __name__ == "__main__":
    main()
