def encode(value):
    if isinstance(value, bool):
        return b"i1e" if value else b"i0e"
    if isinstance(value, int):
        return b"i%de" % value
    if isinstance(value, str):
        value = value.encode("utf-8")
    if isinstance(value, (bytes, bytearray)):
        return b"%d:%s" % (len(value), bytes(value))
    if isinstance(value, (list, tuple)):
        return b"l" + b"".join(encode(v) for v in value) + b"e"
    if isinstance(value, dict):
        pairs = sorted(
            (k.encode("utf-8") if isinstance(k, str) else k, v) for k, v in value.items()
        )
        return b"d" + b"".join(encode(k) + encode(v) for k, v in pairs) + b"e"
    raise TypeError("cannot bencode %r" % (type(value),))


def decode(data):
    value, end = _decode(data, 0)
    if end != len(data):
        raise ValueError("trailing data")
    return value


def _decode(data, pos):
    c = data[pos : pos + 1]
    if c == b"i":
        end = data.index(b"e", pos)
        return int(data[pos + 1 : end]), end + 1
    if c == b"l":
        pos += 1
        out = []
        while data[pos : pos + 1] != b"e":
            v, pos = _decode(data, pos)
            out.append(v)
        return out, pos + 1
    if c == b"d":
        pos += 1
        out = {}
        while data[pos : pos + 1] != b"e":
            k, pos = _decode(data, pos)
            v, pos = _decode(data, pos)
            out[k] = v
        return out, pos + 1
    colon = data.index(b":", pos)
    length = int(data[pos:colon])
    start = colon + 1
    return data[start : start + length], start + length
