# Face relay

`wss://face.debimarlene.com/socket` connects a Kasa Studio receiver and one Kasa Face sender across networks. Only face parameters travel through the service. Video and audio are not sent or stored.

The receiver creates a persistent room and a 256-bit pairing key. The phone registers that key once through the desktop QR or the local pairing exchange and stores it in Keychain. The relay authenticates both endpoints, forwards phone frames and receiver acknowledgements, and closes the phone connection when the receiver disconnects. It does not expose a room list or put credentials in URLs or access logs. Connections, messages, handshake time and frame rate are bounded.

The iPhone prefers the direct LAN path while acknowledgements arrive, then sends through the Seoul connection when the direct path is lost. The desktop accepts the newest received frame and ignores out-of-order sequences. Returning to the LAN restores the direct path.

## Seoul service

The dedicated systemd units are `kasa-face-relay` and `kasa-face-edge`. The Python service listens on `127.0.0.1:19444`; its dedicated Caddy TLS listener uses `127.0.0.1:19443`. The existing nginx SNI map routes `face.debimarlene.com` to that listener. The kasaterm gateway and iroh relay binaries and services are independent.

Deployment sources are in `deploy/`. Copy `server.py`, `requirements.txt`, `deploy/Caddyfile`, both units and `deploy/install.sh` to a staging directory, then run the install script there as root. Ubuntu needs `python3.12-venv`. The installer backs up the original SNI file before adding the route, validates nginx configuration and performs a graceful nginx reload. Caddy obtains and renews the public certificate through TLS-ALPN. Health: `https://face.debimarlene.com/health`.

To remove this service, first restore the saved `/etc/nginx/stream.d/kasa-sni.conf`, validate and reload nginx, then disable the two dedicated units. Keep the gateway and iroh units running.

## Verification

```sh
python3 -m unittest -v test_server.py
../native/build/studio_phone_link_probe wss://face.debimarlene.com/socket
cd ../iphone
KASA_FACE_TEST_RELAY_URL=wss://face.debimarlene.com/socket swift test
```

The native probe uses the production Foundation WebSocket transport. The Swift test uses the phone's actual transport implementation, confirms relay traffic, switches to a local UDP responder, then removes that responder and checks relay recovery. These tests do not activate a camera. Real iPhone camera and cellular behavior require an on-device session.
