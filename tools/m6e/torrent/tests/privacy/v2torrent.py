"""Synthetic BitTorrent v2 (BEP 52) single-file torrents for the tests."""

import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "fixtures"))

import bencode  # noqa: E402

BLOCK = 16384


def _sha(data):
    return hashlib.sha256(data).digest()


def _root(leaves):
    while len(leaves) > 1:
        leaves = [_sha(leaves[i] + leaves[i + 1]) for i in range(0, len(leaves), 2)]
    return leaves[0]


class V2Fixture:
    def __init__(self, name, data, piece_length=32768):
        self.name = name
        self.data = data
        self.piece_length = piece_length
        blocks = [_sha(data[i : i + BLOCK]) for i in range(0, len(data), BLOCK)]
        count = 1
        while count < len(blocks):
            count *= 2
        leaves = blocks + [b"\0" * 32] * (count - len(blocks))
        self.pieces_root = _root(leaves)
        per_piece = piece_length // BLOCK
        layers = b""
        if len(data) > piece_length:
            pieces = (len(blocks) + per_piece - 1) // per_piece
            for p in range(pieces):
                layers += _root(leaves[p * per_piece : (p + 1) * per_piece])
        self.layers = layers
        self.info = {
            "name": name,
            "piece length": piece_length,
            "meta version": 2,
            "file tree": {name: {"": {"length": len(data), "pieces root": self.pieces_root}}},
        }
        self.info_bytes = bencode.encode(self.info)
        self.tid = hashlib.sha256(self.info_bytes).hexdigest()[:40]

    def torrent_bytes(self, announce=None):
        top = {"info": self.info, "created by": "m6e-test"}
        if self.layers:
            top["piece layers"] = {self.pieces_root: self.layers}
        if announce:
            top["announce"] = announce
        return bencode.encode(top)

    def write_content(self, save):
        with open(os.path.join(save, self.name), "wb") as f:
            f.write(self.data)
