#!/usr/bin/env python3
"""Naprawa T-polaczen w STL: dzieli trojkaty, na ktorych krawedziach leza
wierzcholki sasiadow, az kazda krawedz nalezy do dokladnie 2 trojkatow."""
import collections
import struct
import sys

import numpy as np

EPS_WELD = 0.002
EPS_LINE = 0.003


def load(path):
    data = open(path, 'rb').read()
    count = struct.unpack('<I', data[80:84])[0]
    rec = np.frombuffer(data[84:84 + count * 50],
                        dtype=np.dtype([('n', '<3f4'), ('v', '<9f4'), ('a', '<u2')]))
    return rec['v'].reshape(-1, 3, 3).astype(float)


def weld(tris):
    pts = tris.reshape(-1, 3)
    keys = np.round(pts / EPS_WELD).astype(np.int64)
    uniq, first, inv = np.unique(keys, axis=0, return_index=True, return_inverse=True)
    verts = pts[first]
    faces = inv.reshape(-1, 3)
    keep = (faces[:, 0] != faces[:, 1]) & (faces[:, 1] != faces[:, 2]) & (faces[:, 0] != faces[:, 2])
    return verts, [tuple(f) for f in faces[keep]]


def edge_counts(faces):
    counts = collections.Counter()
    for a, b, c in faces:
        for e in ((a, b), (b, c), (c, a)):
            counts[tuple(sorted(e))] += 1
    return counts


def on_segment(verts, a, b, v):
    pa, pb, pv = verts[a], verts[b], verts[v]
    ab = pb - pa
    length2 = ab @ ab
    if length2 < 1e-12:
        return None
    t = ((pv - pa) @ ab) / length2
    if t <= 1e-6 or t >= 1 - 1e-6:
        return None
    dist = np.linalg.norm(pa + t * ab - pv)
    return t if dist < EPS_LINE else None


def repair(verts, faces, max_rounds=20):
    for _ in range(max_rounds):
        counts = edge_counts(faces)
        open_edges = [e for e, c in counts.items() if c == 1]
        if not open_edges:
            return faces, 0
        open_verts = sorted({v for e in open_edges for v in e})
        ov = verts[open_verts]
        splits = {}
        for a, b in open_edges:
            lo = np.minimum(verts[a], verts[b]) - EPS_LINE
            hi = np.maximum(verts[a], verts[b]) + EPS_LINE
            mask = np.all((ov >= lo) & (ov <= hi), axis=1)
            found = []
            for idx in np.nonzero(mask)[0]:
                v = open_verts[idx]
                if v in (a, b):
                    continue
                t = on_segment(verts, a, b, v)
                if t is not None:
                    found.append((t, v))
            if found:
                splits[(a, b)] = [v for _, v in sorted(found)]
        if not splits:
            break
        new_faces = []
        for face in faces:
            done = False
            for i in range(3):
                a, b, c = face[i], face[(i + 1) % 3], face[(i + 2) % 3]
                key = (a, b) if (a, b) in splits else ((b, a) if (b, a) in splits else None)
                if key is None:
                    continue
                inner = splits[key] if key == (a, b) else list(reversed(splits[key]))
                chain = [a] + inner + [b]
                for p, q in zip(chain, chain[1:]):
                    new_faces.append((p, q, c))
                done = True
                break
            if not done:
                new_faces.append(face)
        faces = new_faces
    counts = edge_counts(faces)
    return faces, sum(1 for c in counts.values() if c != 2)


def save(path, verts, faces):
    out = bytearray(80)
    out[:20] = b'obudowa panelu LED  '
    out += struct.pack('<I', len(faces))
    for a, b, c in faces:
        pa, pb, pc = verts[a], verts[b], verts[c]
        n = np.cross(pb - pa, pc - pa)
        n = n / (np.linalg.norm(n) or 1)
        out += struct.pack('<12fH', *n, *pa, *pb, *pc, 0)
    open(path, 'wb').write(out)


for path in sys.argv[1:]:
    verts, faces = weld(load(path))
    faces, bad = repair(verts, faces)
    save(path, verts, faces)
    tris = np.array([[verts[a], verts[b], verts[c]] for a, b, c in faces])
    volume = np.sum(np.einsum('ij,ij->i', tris[:, 0], np.cross(tris[:, 1], tris[:, 2]))) / 6
    print(f'{path}: {len(faces)} trojkatow, wadliwych krawedzi: {bad}, objetosc {volume / 1000:.1f} cm3')
