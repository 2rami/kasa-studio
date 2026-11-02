import asyncio
from collections import Counter, deque
from dataclasses import dataclass
import hashlib
import hmac
import json
import re
import time

from aiohttp import web, WSMsgType


@dataclass
class Room:
    digest: bytes
    host: web.WebSocketResponse
    phone: web.WebSocketResponse | None = None


class FaceRelay:
    def __init__(self):
        self.rooms = {}
        self.connections = Counter()
        self.failures = {}

    async def socket(self, request):
        address = request.headers.get("X-Forwarded-For", request.remote or "unknown").split(",")[0].strip()
        failures = self.failures.setdefault(address, deque(maxlen=20))
        while failures and time.monotonic() - failures[0] > 60:
            failures.popleft()
        if self.connections[address] >= 8 or sum(self.connections.values()) >= 128 or len(failures) >= 12:
            raise web.HTTPTooManyRequests()
        self.connections[address] += 1
        socket = web.WebSocketResponse(max_msg_size=16384, heartbeat=15, compress=False)
        room = None
        role = ""
        room_id = ""
        try:
            await socket.prepare(request)
            hello = await asyncio.wait_for(socket.receive_json(), 5)
            room_id, key, role = hello.get("room", ""), hello.get("key", ""), hello.get("role", "")
            if not re.fullmatch(r"[a-f0-9]{32}", room_id) or not re.fullmatch(r"[a-f0-9]{64}", key) or role not in ("host", "phone"):
                raise ValueError("invalid pairing")
            digest = hashlib.sha256(key.encode()).digest()
            room = self.rooms.get(room_id)
            if room and not hmac.compare_digest(room.digest, digest):
                raise ValueError("invalid pairing")
            if role == "host":
                if room and not room.host.closed:
                    await socket.close(code=4409, message=b"receiver already connected")
                    return socket
                if len(self.rooms) >= 64:
                    await socket.close(code=4429, message=b"capacity")
                    return socket
                room = Room(digest, socket)
                self.rooms[room_id] = room
            elif not room or room.host.closed:
                await socket.close(code=4404, message=b"receiver offline")
                return socket
            elif room.phone and not room.phone.closed:
                await socket.close(code=4409, message=b"phone already connected")
                return socket
            else:
                room.phone = socket
                await room.host.send_json({"type": "peer", "connected": True})
            await socket.send_json({"type": "ready", "role": role})
            recent = deque()
            async for message in socket:
                if message.type != WSMsgType.TEXT:
                    if message.type == WSMsgType.ERROR:
                        break
                    continue
                at = time.monotonic()
                while recent and at - recent[0] > 1:
                    recent.popleft()
                recent.append(at)
                if len(recent) > 90:
                    await socket.close(code=4429, message=b"rate limit")
                    break
                packet = json.loads(message.data)
                kind = packet.get("type", "frame")
                if role == "phone" and kind in ("frame", "stop"):
                    if packet.get("version") != 1 or not isinstance(packet.get("sequence"), int) or len(message.data) > 8192:
                        raise ValueError("invalid frame")
                    peer = room.host
                elif role == "host" and kind == "ack":
                    peer = room.phone
                else:
                    raise ValueError("invalid message")
                if peer and not peer.closed:
                    await asyncio.wait_for(peer.send_str(message.data), 1)
        except (ValueError, TypeError, KeyError, asyncio.TimeoutError):
            failures.append(time.monotonic())
            await socket.close(code=4403, message=b"invalid connection")
        except (ConnectionError, RuntimeError):
            pass
        finally:
            self.connections[address] -= 1
            if not self.connections[address]:
                self.connections.pop(address, None)
            if room:
                if role == "host" and room.host is socket:
                    if self.rooms.get(room_id) is room:
                        self.rooms.pop(room_id)
                    if room.phone:
                        await room.phone.close(code=4404, message=b"receiver offline")
                elif role == "phone" and room.phone is socket:
                    room.phone = None
                    if not room.host.closed:
                        await room.host.send_json({"type": "peer", "connected": False})
            if not failures:
                self.failures.pop(address, None)
        return socket


def application():
    relay = FaceRelay()
    app = web.Application(client_max_size=16384)
    app.router.add_get("/socket", relay.socket)

    async def health(_):
        return web.json_response({"ok": True, "service": "kasa-face-relay", "version": 1})

    app.router.add_get("/health", health)
    return app


if __name__ == "__main__":
    web.run_app(application(), host="127.0.0.1", port=19444, access_log=None, print=None)
