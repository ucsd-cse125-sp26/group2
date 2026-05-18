#!/usr/bin/env python3
"""UDP-only global server directory and relay.

This mirrors src/directory/network/GlobalDirectoryServer.cpp closely enough
to run on cse125.ucsd.edu with only the Python standard library installed.
It listens on one public UDP port, accepts game-server advertisements,
answers browser list/punch requests, and forwards opaque relay envelopes.
"""

from __future__ import annotations

import argparse
import hashlib
import hmac
import os
import secrets
import selectors
import socket
import struct
import time
from dataclasses import dataclass, field


DIR_MAGIC = 0x32444747  # "GGD2"
DIR_VERSION = 1
SERVER_TTL_SECONDS = 15.0
CLIENT_TTL_SECONDS = 10.0
FRAGMENT_TTL_SECONDS = 3.0
RELAY_TOKEN_TTL_MS = 30_000
RELAY_TOKEN_MAC_BYTES = 32
MAX_ADVERTISED_SERVERS = 256
MAX_RELAY_CLIENTS = 4096
MAX_SERVERS_PER_LIST = 5
MAX_SERVER_NAME_BYTES = 64
MAX_FRAGMENT_SETS = 2048

MSG_REGISTER = 1
MSG_REGISTER_ACK = 2
MSG_HEARTBEAT = 3
MSG_LIST_REQUEST = 4
MSG_LIST_RESPONSE = 5
MSG_PUNCH_REQUEST = 6
MSG_PUNCH_RESPONSE = 7
MSG_PUNCH_PEER = 8

UDP_PUNCH_PEER = 2

G2_MAGIC = 0x3247
G2_VERSION = 2
G2_HEADER_FMT = "<HBBQIIIHBBHHI"
G2_HEADER_SIZE = struct.calcsize(G2_HEADER_FMT)
G2_MAX_PACKET_BYTES = 1200
G2_MAX_PAYLOAD_BYTES = G2_MAX_PACKET_BYTES - G2_HEADER_SIZE

KIND_RELAY_PAYLOAD = 5
KIND_DIRECTORY_CONTROL = 6
CHANNEL_CONTROL_RELIABLE_ORDERED = 2
FLAG_FRAGMENTED = 0x01


@dataclass
class PacketHeader:
    kind: int
    connection_id: int = 0
    sequence: int = 0
    ack: int = 0
    ack_bits: int = 0
    route_id: int = 0
    channel: int = CHANNEL_CONTROL_RELIABLE_ORDERED
    flags: int = 0
    fragment_info: int = 0
    fragment_group: int = 0


@dataclass
class ServerRecord:
    server_id: int
    name: str
    host: str
    game_port: int
    endpoint: tuple[str, int]
    udp_host: str = ""
    udp_port: int = 0
    current_players: int = 0
    max_players: int = 0
    last_seen: float = 0.0
    nat_ready: bool = True


@dataclass
class ClientEndpoint:
    endpoint: tuple[str, int]
    relay_token_expires_at_ms: int
    relay_token_mac: bytes
    last_seen: float
    relay_authorized: bool = False


@dataclass
class FragmentSet:
    count: int
    first_seen: float
    parts: dict[int, bytes] = field(default_factory=dict)


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def take(self, fmt: str):
        size = struct.calcsize(fmt)
        if self.offset + size > len(self.data):
            raise ValueError("short packet")
        value = struct.unpack_from(fmt, self.data, self.offset)
        self.offset += size
        return value[0] if len(value) == 1 else value

    def string(self) -> str:
        size = self.take("<H")
        if self.offset + size > len(self.data):
            raise ValueError("short string")
        raw = self.data[self.offset : self.offset + size]
        self.offset += size
        return raw.decode("utf-8", errors="replace")

    def finished(self) -> bool:
        return self.offset == len(self.data)


def pack_string(value: str) -> bytes:
    raw = value.encode("utf-8")[:65535]
    return struct.pack("<H", len(raw)) + raw


def envelope(kind: int, payload: bytes = b"") -> bytes:
    return struct.pack("<IHB", DIR_MAGIC, DIR_VERSION, kind) + payload


def parse_envelope(data: bytes) -> tuple[int, bytes]:
    if len(data) < 7:
        raise ValueError("short envelope")
    magic, version, kind = struct.unpack_from("<IHB", data, 0)
    if magic != DIR_MAGIC or version != DIR_VERSION:
        raise ValueError("bad envelope")
    return kind, data[7:]


def parse_registration(payload: bytes):
    reader = Reader(payload)
    server_id = reader.take("<I")
    name = reader.string()
    game_port = reader.take("<H")
    current_players = reader.take("<B")
    max_players = reader.take("<B")
    if not reader.finished():
        raise ValueError("trailing registration bytes")
    return server_id, name, game_port, current_players, max_players


def pack_server(record: ServerRecord, age_ms: int) -> bytes:
    payload = struct.pack("<I", record.server_id)
    payload += pack_string(record.name)
    payload += pack_string(record.host)
    payload += struct.pack("<H", record.game_port)
    payload += pack_string(record.udp_host)
    payload += struct.pack("<HBBQB", record.udp_port, record.current_players, record.max_players, age_ms, int(record.nat_ready))
    return payload


def parse_punch_request(payload: bytes) -> tuple[int, int]:
    if len(payload) != 8:
        raise ValueError("bad punch request")
    return struct.unpack("<II", payload)


def pack_register_ack(server_id: int, public_host: str, message: str, accepted: bool = True) -> bytes:
    payload = struct.pack("<BI", int(accepted), server_id)
    payload += pack_string(public_host)
    payload += pack_string(message)
    return envelope(MSG_REGISTER_ACK, payload)


def pack_list_response(records: list[ServerRecord]) -> bytes:
    now = time.monotonic()
    active = records[:MAX_SERVERS_PER_LIST]
    payload = struct.pack("<H", len(active))
    for record in active:
        payload += pack_server(record, int((now - record.last_seen) * 1000))
    return envelope(MSG_LIST_RESPONSE, payload)


def pack_punch_response(record: ServerRecord | None, token_expires_at_ms: int, token_mac: bytes, message: str) -> bytes:
    if record is None:
        empty = ServerRecord(0, "", "", 0, ("", 0), nat_ready=False)
        payload = struct.pack("<B", 0) + pack_server(empty, 0) + struct.pack("<Q", 0)
        payload += bytes(RELAY_TOKEN_MAC_BYTES) + pack_string(message)
    else:
        payload = struct.pack("<B", 1) + pack_server(record, int((time.monotonic() - record.last_seen) * 1000))
        payload += struct.pack("<Q", token_expires_at_ms)
        payload += token_mac
        payload += pack_string(message)
    return envelope(MSG_PUNCH_RESPONSE, payload)


def pack_udp_punch_peer(nonce: int, host: str, port: int) -> bytes:
    return struct.pack("<BI", UDP_PUNCH_PEER, nonce) + pack_string(host) + struct.pack("<H", port)


def parse_packet(data: bytes) -> tuple[PacketHeader, bytes]:
    if len(data) < G2_HEADER_SIZE:
        raise ValueError("short g2 packet")
    if len(data) > G2_MAX_PACKET_BYTES:
        raise ValueError("oversize g2 packet")
    fields = struct.unpack_from(G2_HEADER_FMT, data, 0)
    magic, version, kind, conn_id, sequence, ack, ack_bits, route_id, channel, flags, frag_info, frag_group, _pad = fields
    if magic != G2_MAGIC or version != G2_VERSION:
        raise ValueError("bad g2 header")
    return (
        PacketHeader(
            kind=kind,
            connection_id=conn_id,
            sequence=sequence,
            ack=ack,
            ack_bits=ack_bits,
            route_id=route_id,
            channel=channel,
            flags=flags,
            fragment_info=frag_info,
            fragment_group=frag_group,
        ),
        data[G2_HEADER_SIZE:],
    )


def pack_header(header: PacketHeader) -> bytes:
    return struct.pack(
        G2_HEADER_FMT,
        G2_MAGIC,
        G2_VERSION,
        header.kind,
        header.connection_id,
        header.sequence,
        header.ack,
        header.ack_bits,
        header.route_id,
        header.channel,
        header.flags,
        header.fragment_info,
        header.fragment_group,
        0,
    )


class DirectoryServer:
    def __init__(self, udp_port: int):
        self.udp_port = udp_port
        self.selector = selectors.DefaultSelector()
        self.relay_secret = self.load_relay_secret()
        self.servers: dict[int, ServerRecord] = {}
        self.clients_by_relay_session: dict[tuple[int, int], ClientEndpoint] = {}
        self.fragments: dict[tuple[str, int, int, int, int, int, int], FragmentSet] = {}
        self.last_malformed_log = 0.0
        self.malformed_drops = 0
        self.next_server_id = 1
        self.sock: socket.socket | None = None

    @staticmethod
    def load_relay_secret() -> bytes:
        secret = os.environ.get("GROUP2_RELAY_SECRET")
        if secret and len(secret.encode("utf-8")) >= 32:
            return secret.encode("utf-8")
        if secret:
            print("[directory] GROUP2_RELAY_SECRET is shorter than 32 bytes; using process-random fallback", flush=True)
        else:
            print("[directory] GROUP2_RELAY_SECRET not set; relay tokens will be valid only for this process", flush=True)
        return secrets.token_bytes(32)

    def sign_relay_token(self, server_id: int, client_nonce: int, expires_at_ms: int) -> bytes:
        material = b"group2-relay-token-v1" + struct.pack("<IIQ", server_id, client_nonce, expires_at_ms)
        return hmac.new(self.relay_secret, material, hashlib.sha256).digest()

    def verify_relay_token(self, server_id: int, client_nonce: int, expires_at_ms: int, mac: bytes) -> bool:
        if expires_at_ms < int(time.monotonic() * 1000) or len(mac) != RELAY_TOKEN_MAC_BYTES:
            return False
        expected = self.sign_relay_token(server_id, client_nonce, expires_at_ms)
        return hmac.compare_digest(mac, expected)

    def start(self):
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("0.0.0.0", self.udp_port))
        udp.setblocking(False)
        self.sock = udp
        self.selector.register(udp, selectors.EVENT_READ, self.read_udp)

        print(f"[directory] UDP directory/relay listening on {self.udp_port}", flush=True)
        try:
            while True:
                for key, _ in self.selector.select(timeout=0.25):
                    key.data(key.fileobj)
                self.prune()
        except KeyboardInterrupt:
            print("[directory] stopping", flush=True)
        finally:
            self.selector.close()
            udp.close()

    def read_udp(self, sock: socket.socket):
        try:
            data, addr = sock.recvfrom(4096)
        except OSError:
            return
        try:
            header, payload = parse_packet(data)
            payload = self.reassemble_if_needed(addr, header, payload)
            if payload is None:
                return
            if header.kind == KIND_DIRECTORY_CONTROL:
                self.handle_directory_control(addr, payload)
            elif header.kind == KIND_RELAY_PAYLOAD:
                self.handle_relay_payload(addr, payload)
        except Exception as exc:
            self.malformed_drops += 1
            now = time.monotonic()
            if now - self.last_malformed_log >= 2.0:
                print(
                    f"[directory] dropped malformed UDP from {addr[0]}:{addr[1]}: {exc} "
                    f"(recent drops={self.malformed_drops})",
                    flush=True,
                )
                self.last_malformed_log = now
                self.malformed_drops = 0

    def reassemble_if_needed(self, addr: tuple[str, int], header: PacketHeader, payload: bytes) -> bytes | None:
        if (header.flags & FLAG_FRAGMENTED) == 0:
            return payload

        frag_idx = header.fragment_info >> 8
        frag_count = header.fragment_info & 0xFF
        if frag_count == 0 or frag_idx >= frag_count:
            raise ValueError("bad fragment metadata")

        key = (
            addr[0],
            addr[1],
            header.kind,
            header.connection_id,
            header.channel,
            header.sequence,
            header.fragment_group,
        )
        frag_set = self.fragments.get(key)
        if frag_set is None or frag_set.count != frag_count:
            if key not in self.fragments and len(self.fragments) >= MAX_FRAGMENT_SETS:
                raise ValueError("fragment table full")
            frag_set = FragmentSet(count=frag_count, first_seen=time.monotonic())
            self.fragments[key] = frag_set
        frag_set.parts.setdefault(frag_idx, payload)
        if len(frag_set.parts) != frag_count:
            return None

        del self.fragments[key]
        return b"".join(frag_set.parts[i] for i in range(frag_count))

    def handle_directory_control(self, addr: tuple[str, int], data: bytes):
        kind, payload = parse_envelope(data)
        if kind in (MSG_REGISTER, MSG_HEARTBEAT):
            self.handle_registration(addr, kind, payload)
        elif kind == MSG_LIST_REQUEST:
            self.send_directory(addr, pack_list_response(list(self.servers.values())))
        elif kind == MSG_PUNCH_REQUEST:
            self.handle_punch_request(addr, payload)

    def handle_registration(self, addr: tuple[str, int], kind: int, payload: bytes):
        server_id, name, game_port, current_players, max_players = parse_registration(payload)
        name = (name or "Unnamed Server")[:MAX_SERVER_NAME_BYTES]
        if server_id == 0:
            server_id = self.next_server_id
            self.next_server_id += 1

        host = addr[0]
        record = self.servers.get(server_id)
        if server_id != 0 and record is not None and record.endpoint != addr:
            self.send_directory(addr, pack_register_ack(0, host, "server id belongs to another endpoint", False))
            return
        if record is None:
            if len(self.servers) >= MAX_ADVERTISED_SERVERS:
                self.send_directory(addr, pack_register_ack(0, host, "directory is full", False))
                return
            record = ServerRecord(server_id, name, host, game_port, addr)
            self.servers[server_id] = record

        record.name = name
        record.host = host
        record.game_port = game_port
        record.endpoint = addr
        record.udp_host = host
        record.udp_port = addr[1]
        record.current_players = current_players
        record.max_players = max_players
        record.last_seen = time.monotonic()
        record.nat_ready = True

        if kind == MSG_REGISTER:
            print(f"[directory] registered server {server_id} {record.name!r} at {host}:{addr[1]}", flush=True)
        self.send_directory(addr, pack_register_ack(server_id, host, "registered"))

    def handle_punch_request(self, addr: tuple[str, int], payload: bytes):
        server_id, nonce = parse_punch_request(payload)
        if server_id == 0 or nonce == 0:
            self.send_directory(addr, pack_punch_response(None, 0, b"", "invalid punch request"))
            return

        record = self.servers.get(server_id)
        if record is None:
            self.send_directory(addr, pack_punch_response(None, 0, b"", "server not found"))
            return

        relay_key = (server_id, nonce)
        if relay_key not in self.clients_by_relay_session and len(self.clients_by_relay_session) >= MAX_RELAY_CLIENTS:
            self.send_directory(addr, pack_punch_response(None, 0, b"", "relay is busy"))
            return

        expires_at_ms = int(time.monotonic() * 1000) + RELAY_TOKEN_TTL_MS
        token_mac = self.sign_relay_token(server_id, nonce, expires_at_ms)
        self.clients_by_relay_session[relay_key] = ClientEndpoint(addr, expires_at_ms, token_mac, time.monotonic())

        peer_payload = envelope(MSG_PUNCH_PEER, pack_udp_punch_peer(nonce, addr[0], addr[1]))
        self.send_directory(record.endpoint, peer_payload)
        self.send_directory(addr, pack_punch_response(record, expires_at_ms, token_mac, "ok"))
        print(
            f"[directory] punch assist server={server_id} client={addr[0]}:{addr[1]} serverUdp={record.udp_host}:{record.udp_port}",
            flush=True,
        )

    def handle_relay_payload(self, addr: tuple[str, int], payload: bytes):
        relay_header_len = 4 + 4 + 8 + RELAY_TOKEN_MAC_BYTES + 2
        if len(payload) < relay_header_len:
            return
        server_id, client_nonce = struct.unpack_from("<II", payload, 0)
        (token_expires_at_ms,) = struct.unpack_from("<Q", payload, 8)
        token_mac = payload[16 : 16 + RELAY_TOKEN_MAC_BYTES]
        (inner_len,) = struct.unpack_from("<H", payload, 16 + RELAY_TOKEN_MAC_BYTES)
        if server_id == 0 or client_nonce == 0 or inner_len < G2_HEADER_SIZE or len(payload) != relay_header_len + inner_len:
            return

        record = self.servers.get(server_id)
        if record is None:
            return

        relay_key = (server_id, client_nonce)
        if addr == record.endpoint:
            client = self.clients_by_relay_session.get(relay_key)
            if client is not None:
                self.send_relay(client.endpoint, payload)
        else:
            client = self.clients_by_relay_session.get(relay_key)
            needs_token = client is None or not client.relay_authorized or client.endpoint != addr
            if client is None or (needs_token and not self.verify_relay_token(server_id, client_nonce, token_expires_at_ms, token_mac)):
                return
            client.endpoint = addr
            client.last_seen = time.monotonic()
            client.relay_authorized = True
            self.send_relay(record.endpoint, payload)

    def send_directory(self, addr: tuple[str, int], payload: bytes):
        self.send_packet(addr, KIND_DIRECTORY_CONTROL, payload)

    def send_relay(self, addr: tuple[str, int], payload: bytes):
        self.send_packet(addr, KIND_RELAY_PAYLOAD, payload)

    def send_packet(self, addr: tuple[str, int], kind: int, payload: bytes):
        if self.sock is None:
            return
        if len(payload) > G2_MAX_PAYLOAD_BYTES:
            print(f"[directory] dropping oversize response ({len(payload)} bytes)", flush=True)
            return
        header = PacketHeader(kind=kind)
        self.sock.sendto(pack_header(header) + payload, addr)

    def prune(self):
        now = time.monotonic()
        for server_id in list(self.servers):
            if now - self.servers[server_id].last_seen > SERVER_TTL_SECONDS:
                print(f"[directory] expiring server {server_id}", flush=True)
                del self.servers[server_id]
        for key in list(self.clients_by_relay_session):
            if now - self.clients_by_relay_session[key].last_seen > CLIENT_TTL_SECONDS:
                del self.clients_by_relay_session[key]
        for key in list(self.fragments):
            if now - self.fragments[key].first_seen > FRAGMENT_TTL_SECONDS:
                del self.fragments[key]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--udp-port", type=int, default=10081)
    parser.add_argument("--tcp-port", type=int, default=10080, help="ignored legacy option")
    args = parser.parse_args()
    DirectoryServer(args.udp_port).start()


if __name__ == "__main__":
    main()
