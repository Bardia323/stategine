"""Bounded opaque ICE signaling relay; never receives Stategine world data.

Requires websockets >= 10.4. For Internet use --cert/--key, or put this
loopback service behind a TLS reverse proxy. Peers authenticate every frame
with their pinned application keys; the relay has no committee role.
"""
import argparse
import asyncio
import collections
import hashlib
import ssl
from websockets.legacy.server import serve
from websockets.exceptions import ConnectionClosed


async def run(host, port, tls=None, ready=None):
    clients = {}
    retained = collections.OrderedDict()

    async def writer(ws, queue):
        while True:
            await ws.send(await queue.get())

    async def client(ws, path):
        if len(clients) >= 16:
            await ws.close(1013, 'signaling connection bound')
            return
        queue = asyncio.Queue(256)
        clients[ws] = queue
        for frame in retained.values():
            queue.put_nowait(frame)
        task = asyncio.create_task(writer(ws, queue))
        try:
            async for frame in ws:
                if not isinstance(frame, bytes):
                    await ws.close(1003, 'binary signaling only')
                    return
                identity = hashlib.sha256(frame).digest()
                if identity not in retained:
                    if len(retained) == 256:
                        retained.popitem(last=False)
                    retained[identity] = frame
                for other, pending in tuple(clients.items()):
                    if other == ws:
                        continue
                    try:
                        pending.put_nowait(frame)
                    except asyncio.QueueFull:
                        await other.close(1013, 'signaling backpressure')
        except ConnectionClosed:
            pass
        finally:
            clients.pop(ws, None)
            task.cancel()
            await asyncio.gather(task, return_exceptions=True)

    async with serve(client, host, port, ssl=tls, max_size=128*1024,
                     max_queue=16, write_limit=128*1024, compression=None) as server:
        actual = server.sockets[0].getsockname()[1]
        print(f'READY {actual}', flush=True)
        if ready:
            ready(actual)
        await asyncio.Future()


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', default='127.0.0.1')
    p.add_argument('--port', type=int, default=49280)
    p.add_argument('--cert')
    p.add_argument('--key')
    args = p.parse_args()
    tls = None
    if args.cert and args.key:
        tls = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        tls.load_cert_chain(args.cert, args.key)
    elif args.host not in ('127.0.0.1', 'localhost', '::1'):
        p.error('non-loopback rendezvous requires --cert and --key')
    try:
        asyncio.run(run(args.host, args.port, tls))
    except KeyboardInterrupt:
        pass
