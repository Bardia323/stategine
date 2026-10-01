"""Actual encrypted peers, authenticated WSS rendezvous, ordinary DSL worlds."""
import json
import os
import pathlib
import shutil
import subprocess
import sys
import uuid

verified, keys, pong, rendezvous, openssl, output = map(pathlib.Path, sys.argv[1:])
output.mkdir(parents=True, exist_ok=True)
if os.name == 'nt':
    import ctypes
    ctypes.windll.kernel32.SetErrorMode(3)
cert, private = output / 'test-ca.pem', output / 'test-key.pem'
subprocess.run([str(openssl), 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                '-days', '2', '-subj', '/CN=localhost',
                '-addext', 'subjectAltName=DNS:localhost,IP:127.0.0.1',
                '-keyout', str(private), '-out', str(cert)], check=True, capture_output=True)
server = subprocess.Popen([sys.executable, str(rendezvous), '--port', '0',
                           '--cert', str(cert), '--key', str(private)],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
children = []

def peers(exe, arguments, label):
    group = []
    paths = []
    try:
        for rank, args in enumerate(arguments):
            path = output / f'{label}-{rank}.json'
            paths.append(path)
            child = subprocess.Popen([str(exe), *args(path)], stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True)
            group.append(child)
            children.append(child)
        for rank, child in enumerate(group):
            log, _ = child.communicate(timeout=80)
            (output / f'{label}-{rank}.log').write_text(log)
            if child.returncode not in (0, 77):
                raise AssertionError((label, rank, child.returncode, log))
        if all(child.returncode == 77 for child in group):
            print('SKIP CUDA encrypted processes: unavailable')
            return None
        assert all(child.returncode == 0 for child in group)
        return [json.loads(path.read_text()) for path in paths]
    finally:
        for child in group:
            if child.poll() is None:
                child.kill()
            child.wait()

try:
    line = server.stdout.readline()
    assert line.startswith('READY '), line
    url = 'wss://localhost:' + line.split()[1]
    master = output / ('identities-' + uuid.uuid4().hex)
    subprocess.run([str(keys), str(master), '4', '3', '1', 'region'], check=True, capture_output=True)
    receipts = []
    for transport in ('udp', 'ice'):
        credentials = output / ('session-' + uuid.uuid4().hex)
        shutil.copytree(master, credentials)
        args = [lambda path, rank=rank: [str(rank), '53140', str(credentials), 'cpu', str(path)]
                + ([url, '-', str(cert)] if transport == 'ice' else []) for rank in range(3)]
        states = peers(verified, args, transport)
        assert all(state == states[0] for state in states), states
        assert states[0]['a'] == states[0]['b'] == 6
        assert states[0]['forged_rejected'] == 4 and len(states[0]['receipts']) == 4
        receipts.append(states[0])
    assert receipts[0] == receipts[1], receipts
    print('ok UDP and independently authenticated encrypted peers finalize identical epoch/receipt chains')
    for modes in (('cuda', 'cuda', 'cuda'), ('cpu', 'cuda', 'cpu')):
        # A fresh external rendezvous avoids retaining old session-0 signaling
        # when this test deliberately reuses the exact same pinned identities.
        server.terminate(); server.communicate(timeout=10)
        server = subprocess.Popen([sys.executable, str(rendezvous), '--port', '0', '--cert', str(cert), '--key', str(private)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        line = server.stdout.readline(); assert line.startswith('READY '), line
        url = 'wss://localhost:' + line.split()[1]
        credentials = output / ('session-' + uuid.uuid4().hex); shutil.copytree(master, credentials)
        args = [lambda path, rank=rank, mode=mode: [str(rank), '53140', str(credentials), mode, str(path), url, '-', str(cert)] for rank, mode in enumerate(modes)]
        states = peers(verified, args, 'ice-' + '-'.join(modes))
        if states is None:
            break
        assert all(state == receipts[0] for state in states), states
        print('ok encrypted CPU/CUDA executors finalize the exact same canonical problem and receipt chain')
    credentials = output / ('pong-' + uuid.uuid4().hex)
    subprocess.run([str(keys), str(credentials), '2', '2', '0', 'pong'], check=True, capture_output=True)
    args = [lambda path, rank=rank: ['--player', str(rank), '--backend', 'cpu',
            '--credentials', str(credentials), '--headless', '--steps', '120', '--out', str(path),
            '--signaling', url, '--ca-file', str(cert), '--delay-ms', str(5 if rank else 0)] for rank in range(2)]
    states = peers(pong, args, 'pong-ice')
    # Presentation/prediction metrics may differ; finalized reality and receipts may not.
    world_keys = ('epoch', 'left_score', 'right_score', 'hits', 'receipt') + tuple(
        f'{entity}_{parameter}' for entity in ('ball', 'left', 'right') for parameter in ('x', 'y', 'vx', 'vy'))
    assert all(states[0][key] == states[1][key] for key in world_keys), states
    print('ok separate Pong processes predict and finalize the same ordinary world through encrypted peer sessions')
finally:
    for child in children:
        if child.poll() is None:
            child.kill()
        child.wait()
    server.terminate()
    server.communicate(timeout=10)
