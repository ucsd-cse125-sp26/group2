#!/usr/bin/env python3
"""Global server directory and UDP punch-assist service.

This mirrors src/directory/network/GlobalDirectoryServer.cpp but runs with
only the Python standard library, which makes it suitable for cse125.ucsd.edu
without building the full game tree there.
"""

from __future__ import annotations

import argparse
import selectors
import socket
import struct
import time
from dataclasses import dataclass


MAGIC = 0x32444747  # "GGD2"
VERSION = 1
SERVER_TTL_SECONDS = 15.0
CLIENT_TTL_SECONDS = 10.0

MSG_REGISTER = 1
MSG_REGISTER_ACK = 2
MSG_HEARTBEAT = 3
MSG_LIST_REQUEST = 4
MSG_LIST_RESPONSE = 5
MSG_PUNCH_REQUEST = 6
MSG_PUNCH_RESPONSE = 7

UDP_HELLO = 1
UDP_PUNCH_PEER = 2
UDP_ROLE_SERVER = 1
UDP_ROLE_CLIENT = 2

G2_MAGIC = 0x3247
G2_VERSION = 1
G2_KIND_KEEPALIVE = 4
G2_CHANNEL_UNRELIABLE = 0


@dataclass
class ServerRecord:
    server_id: int
    name: str
    host: str
    game_port: int
    udp_host: str = ""
    udp_port: int = 0
    current_players: int = 0
    max_players: int = 0
    last_seen: float = 0.0
    nat_ready: bool = False


@dataclass
class ClientUdpEndpoint:
    host: str
    port: int
    last_seen: float


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
    return struct.pack("<IHB", MAGIC, VERSION, kind) + payload


def parse_envelope(data: bytes) -> tuple[int, bytes]:
    if len(data) < 7:
        raise ValueError("short envelope")
    magic, version, kind = struct.unpack_from("<IHB", data, 0)
    if magic != MAGIC or version != VERSION:
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
    payload = struct.pack("<H", min(len(records), 65535))
    for record in records[:65535]:
        payload += pack_server(record, int((now - record.last_seen) * 1000))
    return envelope(MSG_LIST_RESPONSE, payload)


def pack_punch_response(record: ServerRecord | None, message: str) -> bytes:
    if record is None:
        empty = ServerRecord(0, "", "", 0)
        payload = struct.pack("<B", 0) + pack_server(empty, 0) + pack_string(message)
    else:
        payload = struct.pack("<B", 1) + pack_server(record, int((time.monotonic() - record.last_seen) * 1000))
        payload += pack_string(message)
    return envelope(MSG_PUNCH_RESPONSE, payload)


def pack_g2_udp(connection_id: int, payload: bytes) -> bytes:
    header = struct.pack("<HBBI HBBHH", G2_MAGIC, G2_VERSION, G2_KIND_KEEPALIVE, connection_id, 0, G2_CHANNEL_UNRELIABLE, 0, 0, 0)
    return header + payload


def pack_udp_punch_peer(nonce: int, host: str, port: int) -> bytes:
    return struct.pack("<BI", UDP_PUNCH_PEER, nonce) + pack_string(host) + struct.pack("<H", port)


class DirectoryServer:
    def __init__(self, tcp_port: int, udp_port: int):
        self.tcp_port = tcp_port
        self.udp_port = udp_port
        self.selector = selectors.DefaultSelector()
        self.servers: dict[int, ServerRecord] = {}
        self.clients_by_nonce: dict[int, ClientUdpEndpoint] = {}
        self.next_server_id = 1

    def start(self):
        tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        tcp.bind(("0.0.0.0", self.tcp_port))
        tcp.listen()
        tcp.setblocking(False)
        self.selector.register(tcp, selectors.EVENT_READ, self.accept)

        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.bind(("0.0.0.0", self.udp_port))
        udp.setblocking(False)
        self.selector.register(udp, selectors.EVENT_READ, self.read_udp)

        print(f"[directory] listening on TCP {self.tcp_port} / UDP {self.udp_port}", flush=True)
        try:
            while True:
                for key, _ in self.selector.select(timeout=0.25):
                    if callable(key.data):
                        key.data(key.fileobj)
                    else:
                        callback, *state = key.data
                        callback(key.fileobj, state)
                self.prune()
        except KeyboardInterrupt:
            print("[directory] stopping", flush=True)
        finally:
            self.selector.close()

    def accept(self, sock: socket.socket):
        conn, addr = sock.accept()
        conn.setblocking(False)
        print(f"[directory] accepted TCP from {addr[0]}:{addr[1]}", flush=True)
        self.selector.register(conn, selectors.EVENT_READ, (self.read_tcp, bytearray(), addr[0]))

    def read_tcp(self, conn: socket.socket, state):
        buffer, host = state
        try:
            chunk = conn.recv(65536)
        except BlockingIOError:
            return
        except OSError as exc:
            print(f"[directory] TCP read error from {host}: {exc}", flush=True)
            chunk = b""
        if not chunk:
            self.close(conn)
            return
        buffer.extend(chunk)
        while len(buffer) >= 4:
            (length,) = struct.unpack_from("<I", buffer, 0)
            if len(buffer) < 4 + length:
                break
            payload = bytes(buffer[4 : 4 + length])
            del buffer[: 4 + length]
            response = self.handle_tcp(host, payload)
            if response:
                try:
                    conn.sendall(struct.pack("<I", len(response)) + response)
                except OSError as exc:
                    print(f"[directory] TCP write error to {host}: {exc}", flush=True)
                    self.close(conn)
                    return

    def close(self, conn: socket.socket):
        try:
            self.selector.unregister(conn)
        except Exception:
            pass
        conn.close()

    def handle_tcp(self, host: str, data: bytes) -> bytes | None:
        try:
            kind, payload = parse_envelope(data)
            if kind in (MSG_REGISTER, MSG_HEARTBEAT):
                server_id, name, game_port, current_players, max_players = parse_registration(payload)
                if server_id == 0:
                    server_id = self.next_server_id
                    self.next_server_id += 1
                old = self.servers.get(server_id)
                self.servers[server_id] = ServerRecord(
                    server_id=server_id,
                    name=name or "Unnamed Server",
                    host=host,
                    game_port=game_port,
                    udp_host=old.udp_host if old else "",
                    udp_port=old.udp_port if old else 0,
                    current_players=current_players,
                    max_players=max_players,
                    last_seen=time.monotonic(),
                    nat_ready=old.nat_ready if old else False,
                )
                if kind == MSG_REGISTER:
                    print(f"[directory] registered {server_id} {name!r} at {host}:{game_port}", flush=True)
                return pack_register_ack(server_id, host, "registered")
            if kind == MSG_LIST_REQUEST:
                return pack_list_response(list(self.servers.values()))
            if kind == MSG_PUNCH_REQUEST:
                server_id, nonce = parse_punch_request(payload)
                record = self.servers.get(server_id)
                if record is None:
                    return pack_punch_response(None, "server not found")
                client = self.clients_by_nonce.get(nonce)
                if client and record.udp_host and record.udp_port:
                    self.send_udp_peer(record.udp_host, record.udp_port, record.server_id, nonce, client.host, client.port)
                    self.send_udp_peer(client.host, client.port, nonce, nonce, record.udp_host, record.udp_port)
                    print(
                        f"[directory] punch assist server={server_id} client={client.host}:{client.port} serverUdp={record.udp_host}:{record.udp_port}",
                        flush=True,
                    )
                return pack_punch_response(record, "ok")
        except Exception as exc:
            print(f"[directory] dropped malformed TCP message: {exc}", flush=True)
        return None

    def read_udp(self, sock: socket.socket):
        try:
            data, addr = sock.recvfrom(2048)
        except OSError:
            return
        if len(data) < 16:
            return
        magic, version, _kind, connection_id, _seq, _channel, _flags, _frag, _pad = struct.unpack_from("<HBBI HBBHH", data, 0)
        if magic != G2_MAGIC or version != G2_VERSION:
            return
        payload = data[16:]
        if len(payload) < 8 or payload[0] != UDP_HELLO:
            return
        role = payload[1]
        ident = struct.unpack_from("<I", payload, 2)[0]
        _game_port = struct.unpack_from("<H", payload, 6)[0]
        now = time.monotonic()
        if role == UDP_ROLE_SERVER and ident in self.servers:
            record = self.servers[ident]
            record.udp_host = addr[0]
            record.udp_port = addr[1]
            record.nat_ready = True
            record.last_seen = now
        elif role == UDP_ROLE_CLIENT:
            self.clients_by_nonce[ident] = ClientUdpEndpoint(addr[0], addr[1], now)

    def send_udp_peer(self, host: str, port: int, connection_id: int, nonce: int, peer_host: str, peer_port: int):
        payload = pack_udp_punch_peer(nonce, peer_host, peer_port)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.sendto(pack_g2_udp(connection_id, payload), (host, port))

    def prune(self):
        now = time.monotonic()
        for server_id in list(self.servers):
            if now - self.servers[server_id].last_seen > SERVER_TTL_SECONDS:
                print(f"[directory] expiring server {server_id}", flush=True)
                del self.servers[server_id]
        for nonce in list(self.clients_by_nonce):
            if now - self.clients_by_nonce[nonce].last_seen > CLIENT_TTL_SECONDS:
                del self.clients_by_nonce[nonce]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tcp-port", type=int, default=10080)
    parser.add_argument("--udp-port", type=int, default=10081)
    args = parser.parse_args()
    DirectoryServer(args.tcp_port, args.udp_port).start()


if __name__ == "__main__":
    main()
