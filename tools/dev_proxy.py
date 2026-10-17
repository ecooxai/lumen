#!/usr/bin/env python3
"""Minimal HTTP proxy for giving a USB-attached phone this host's network.

Supports CONNECT tunnels (https, wss) and absolute-form plain http. Binds to
loopback only; expose it to the phone with `adb reverse tcp:PORT tcp:PORT`
and launch Lumen with `--es org.lumen.browser.PROXY 127.0.0.1:PORT`.
Logs only connection counts and errors, never hosts, URLs or payloads.
"""
import argparse
import asyncio
import sys
import time

stats = {"open": 0, "total": 0, "failed": 0, "up": 0, "down": 0}


async def pipe(reader, writer, key):
    try:
        while data := await reader.read(65536):
            stats[key] += len(data)
            writer.write(data)
            await writer.drain()
    except (ConnectionError, asyncio.CancelledError, OSError):
        pass
    finally:
        try:
            writer.close()
        except Exception:
            pass


def split_hostport(text, default_port):
    if text.startswith("["):
        host, _, rest = text[1:].partition("]")
        port = rest[1:] if rest.startswith(":") else ""
    else:
        host, _, port = text.rpartition(":") if ":" in text else (text, "", "")
    return host, int(port) if port else default_port


async def handle(client_reader, client_writer):
    stats["open"] += 1
    stats["total"] += 1
    upstream_writer = None
    try:
        head = await asyncio.wait_for(client_reader.readuntil(b"\r\n\r\n"), 30)
        line, _, rest = head.partition(b"\r\n")
        method, target, version = line.decode("latin-1").split(" ", 2)
        if method.upper() == "CONNECT":
            host, port = split_hostport(target, 443)
            upstream_reader, upstream_writer = await asyncio.wait_for(asyncio.open_connection(host, port, happy_eyeballs_delay=0.25), 15)
            client_writer.write(b"HTTP/1.1 200 Connection Established\r\n\r\n")
            await client_writer.drain()
        else:
            if not target.lower().startswith("http://"):
                client_writer.write(b"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
                return
            hostport, _, path = target[7:].partition("/")
            host, port = split_hostport(hostport, 80)
            upstream_reader, upstream_writer = await asyncio.wait_for(asyncio.open_connection(host, port, happy_eyeballs_delay=0.25), 15)
            # first request in origin-form; later keep-alive requests stay absolute-form, which origins must accept
            headers = b"\r\n".join(h for h in rest.split(b"\r\n") if not h.lower().startswith(b"proxy-"))
            upstream_writer.write(f"{method} /{path} {version}\r\n".encode("latin-1") + headers)
        await asyncio.gather(pipe(client_reader, upstream_writer, "up"), pipe(upstream_reader, client_writer, "down"))
    except Exception as error:
        stats["failed"] += 1
        if not isinstance(error, (asyncio.IncompleteReadError, ConnectionError)):
            print(f"proxy: {type(error).__name__}", file=sys.stderr, flush=True)
        try:
            client_writer.write(b"HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n")
        except Exception:
            pass
    finally:
        stats["open"] -= 1
        for w in (client_writer, upstream_writer):
            if w:
                try:
                    w.close()
                except Exception:
                    pass


async def report(interval):
    while True:
        await asyncio.sleep(interval)
        print(f"{time.strftime('%H:%M:%S')} open={stats['open']} total={stats['total']} failed={stats['failed']} "
              f"up={stats['up'] >> 10}KiB down={stats['down'] >> 10}KiB", flush=True)


async def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", type=int, default=8888)
    parser.add_argument("--report", type=float, default=30, help="seconds between stats lines (0 = never)")
    args = parser.parse_args()
    server = await asyncio.start_server(handle, "127.0.0.1", args.port, limit=1 << 16)
    print(f"proxy listening on 127.0.0.1:{args.port}", flush=True)
    if args.report > 0:
        asyncio.create_task(report(args.report))
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
