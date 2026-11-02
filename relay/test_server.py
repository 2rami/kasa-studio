import asyncio
import unittest

from aiohttp import ClientSession, web, WSMsgType
from server import application


class RelayTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.runner = web.AppRunner(application())
        await self.runner.setup()
        site = web.TCPSite(self.runner, "127.0.0.1", 0)
        await site.start()
        self.url = "http://127.0.0.1:" + str(site._server.sockets[0].getsockname()[1])
        self.client = ClientSession()

    async def asyncTearDown(self):
        await self.client.close()
        await self.runner.cleanup()

    async def join(self, role, key="b" * 64):
        socket = await self.client.ws_connect(self.url + "/socket")
        await socket.send_json({"room": "a" * 32, "key": key, "role": role})
        return socket

    async def test_pairing_and_disconnect(self):
        host = await self.join("host")
        self.assertEqual((await host.receive_json())["type"], "ready")
        denied = await self.join("phone", "c" * 64)
        self.assertEqual((await denied.receive()).type, WSMsgType.CLOSE)
        self.assertEqual(denied.close_code, 4403)
        phone = await self.join("phone")
        self.assertEqual((await phone.receive_json())["role"], "phone")
        self.assertTrue((await host.receive_json())["connected"])
        frame = {"version": 1, "sequence": 1, "type": "frame", "tracked": True}
        await phone.send_json(frame)
        self.assertEqual(await host.receive_json(), frame)
        await host.send_json({"type": "ack", "sequence": 1})
        self.assertEqual((await phone.receive_json())["sequence"], 1)
        await phone.close()
        self.assertFalse((await host.receive_json())["connected"])
        await host.close()

    async def test_offline_and_second_sender(self):
        offline = await self.join("phone")
        await offline.receive()
        self.assertEqual(offline.close_code, 4404)
        host = await self.join("host")
        await host.receive_json()
        first = await self.join("phone")
        await first.receive_json()
        await host.receive_json()
        second = await self.join("phone")
        await second.receive()
        self.assertEqual(second.close_code, 4409)
        await host.close()
        self.assertEqual((await first.receive()).type, WSMsgType.CLOSE)
        self.assertEqual(first.close_code, 4404)


if __name__ == "__main__":
    unittest.main()
