#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Checks that a signed Windows program is exactly the unsigned one plus an
Authenticode signature (release.yml, docs/CODE_SIGNING.md).

Authenticode signing (SignTool, SignPath) changes a PE file in three
places only: it pads the file with zeros to a multiple of 8 bytes and
appends the signature (the certificate table, WIN_CERTIFICATE entries),
points the Certificate Table entry of the optional header's data
directories at it, and updates the optional header's CheckSum. So with
those two header fields zeroed, the signed file up to its certificate
table must be the unsigned file followed by at most 7 zero bytes, and the
certificate table must end the file. Anything else means the signing
service returned a different or modified program.

Usage (MSYS2's or any Python 3):
  pe-signature.py compare <unsigned> <signed>  exit 0 if <signed> is <unsigned> plus a signature
  pe-signature.py info <file>...               where each file's certificate table is, if any
  pe-signature.py self-test <unsigned>         compare() on made-up signed copies of a real
                                               program: one must pass, each damaged one fail
"""

import os
import struct
import sys

WIN_CERT_TYPE_PKCS_SIGNED_DATA = 2


class PeError(Exception):
    pass


def layout(data):
    """Offsets of the optional header's CheckSum and of its Certificate
    Table data directory entry (index 4: file offset and size)."""
    if len(data) < 0x40 or data[:2] != b"MZ":
        raise PeError("not a Windows program (no MZ header)")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if pe + 24 > len(data) or data[pe:pe + 4] != b"PE\0\0":
        raise PeError("not a PE file (no PE signature)")
    optional = pe + 24
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    if optional + 2 > len(data):
        raise PeError("truncated optional header")
    magic = struct.unpack_from("<H", data, optional)[0]
    if magic == 0x20B:  # PE32+ (64-bit)
        directories = optional + 112
    elif magic == 0x10B:  # PE32
        directories = optional + 96
    else:
        raise PeError(f"unknown optional header magic {magic:#x}")
    count = struct.unpack_from("<I", data, directories - 4)[0]
    entry = directories + 4 * 8
    if count < 5 or entry + 8 > optional + optional_size or entry + 8 > len(data):
        raise PeError("the optional header has no Certificate Table entry")
    return optional + 64, entry


def certificate_table(data):
    """(file offset, size) of the certificate table; (0, 0) if unsigned."""
    _, entry = layout(data)
    return struct.unpack_from("<II", data, entry)


CHUNK = 1 << 20


def first_difference(a, b, end, skip):
    """The first offset below `end` at which a and b differ, not counting
    the byte ranges in `skip` ((start, stop) pairs); None if there is none.
    Compares 1 MiB slices at a time, never a whole copy of a file (release
    programs are tens of megabytes, a debug OpenShape.exe over 100)."""
    start = 0
    for lo, hi in sorted(skip) + [(end, end)]:
        for s in range(start, lo, CHUNK):
            e = min(s + CHUNK, lo)
            if a[s:e] != b[s:e]:
                return next(i for i in range(s, e) if a[i] != b[i])
        start = max(start, hi)
    return None


def compare(unsigned, signed):
    """None if `signed` is `unsigned` plus an appended Authenticode
    signature and nothing else; otherwise the reason in plain words."""
    try:
        if certificate_table(unsigned) != (0, 0):
            return "the unsigned file already carries a signature"
        offset, size = certificate_table(signed)
    except PeError as e:
        return str(e)
    if offset == 0 or size == 0:
        return "the signed file has no signature (its Certificate Table entry is empty)"
    if offset % 8:
        return f"the signature does not start at a multiple of 8 bytes (offset {offset})"
    if offset + size != len(signed):
        return (f"the signature ({size} bytes at {offset}) does not end the file "
                f"({len(signed)} bytes): something else was added")
    if size < 8:
        return f"the certificate table is too small ({size} bytes)"
    length, _revision, kind = struct.unpack_from("<IHH", signed, offset)
    if kind != WIN_CERT_TYPE_PKCS_SIGNED_DATA:
        return f"the certificate is of type {kind}, not an Authenticode (PKCS #7) signature"
    if length < 8 or length > size:
        return f"the certificate entry's length ({length}) does not fit the table ({size} bytes)"
    if not len(unsigned) <= offset < len(unsigned) + 8:
        return (f"the signature starts at byte {offset} but the unsigned file has "
                f"{len(unsigned)} bytes: content was added or removed")
    if any(signed[len(unsigned):offset]):
        return "the padding before the signature is not zeros"
    # Everything else byte for byte. The two header fields are where the
    # unsigned file's layout puts them: the bytes that layout is read from
    # are compared too, so a signed file with another layout differs there.
    checksum, entry = layout(unsigned)
    first = first_difference(unsigned, signed, len(unsigned),
                             [(checksum, checksum + 4), (entry, entry + 8)])
    if first is not None:
        return f"the files differ at byte {first} (not only by the signature): a different program"
    return None


def read(path):
    with open(path, "rb") as f:
        return f.read()


def fake_signed(unsigned, payload=b"\x30\x82" + bytes(300), checksum=0x1234ABCD):
    """What signing does, with a made-up signature: zero padding to 8
    bytes, a WIN_CERTIFICATE entry (padded to 8), the header fields."""
    checksum_at, entry = layout(unsigned)
    out = bytearray(unsigned)
    out += bytes(-len(out) % 8)
    offset = len(out)
    cert = struct.pack("<IHH", 8 + len(payload), 0x0200, WIN_CERT_TYPE_PKCS_SIGNED_DATA) + payload
    cert += bytes(-len(cert) % 8)
    out += cert
    struct.pack_into("<I", out, checksum_at, checksum)
    struct.pack_into("<II", out, entry, offset, len(cert))
    return out


def patched(data, fmt, at, *values):
    """A copy of data with struct.pack(fmt, *values) written at `at`."""
    out = bytearray(data)
    struct.pack_into(fmt, out, at, *values)
    return out


def flipped(data, at):
    """A copy of data with the lowest bit of byte `at` inverted."""
    out = bytearray(data)
    out[at] ^= 0x01
    return out


def self_test(path):
    """compare() on made-up signed copies of a real program. Each copy is
    made when its check runs, so at most a few copies of the file are in
    memory at once (ctest runs this on the debug OpenShape.exe)."""
    unsigned = read(path)
    good = fake_signed(unsigned)
    offset, size = certificate_table(good)
    checksum, entry = layout(unsigned)
    padding = offset - len(unsigned)
    # (name, whether compare() must accept it, how to make it)
    checks = [
        ("signed copy (only a signature added)", True, lambda: good),
        ("one byte of the program changed", False, lambda: flipped(good, len(unsigned) // 2)),
        ("the last byte of the program changed", False, lambda: flipped(good, len(unsigned) - 1)),
        # The two header fields may differ, but not a byte next to them.
        ("the byte before the CheckSum changed", False, lambda: flipped(good, checksum - 1)),
        ("the byte after the CheckSum changed", False, lambda: flipped(good, checksum + 4)),
        ("the byte after the Certificate Table entry changed", False, lambda: flipped(good, entry + 8)),
        ("a byte appended after the signature", False, lambda: good + b"\0"),
        ("the program's last bytes cut off", False, lambda: fake_signed(unsigned[:-8])),
        ("16 bytes added to the program", False, lambda: fake_signed(unsigned + b"\x90" * 16)),
        ("Certificate Table entry cleared", False, lambda: patched(good, "<II", entry, 0, 0)),
        ("certificate of another type (X.509)", False, lambda: patched(good, "<H", offset + 6, 1)),
        ("the unsigned file compared with itself", False, lambda: unsigned),
    ]
    if padding:
        checks.append(("padding before the signature not zeros", False,
                       lambda: patched(good, "<B", len(unsigned), 1)))

    failures = 0
    for name, should_pass, make in checks:
        reason = compare(unsigned, make())
        ok = (reason is None) == should_pass
        failures += not ok
        verdict = "accepted" if reason is None else f"refused: {reason}"
        print(f"[{'PASS' if ok else 'FAIL'}] {name}: {verdict}")
    twice = compare(good, fake_signed(unsigned))
    ok = twice is not None
    failures += not ok
    print(f"[{'PASS' if ok else 'FAIL'}] an already signed file as the unsigned one: "
          f"{'refused: ' + twice if twice else 'accepted'}")
    print(f"pe-signature self-test: {failures} failed of {len(checks) + 1} "
          f"({os.path.basename(path)}, {len(unsigned)} bytes, made-up signature "
          f"{size} bytes at {offset}, {padding} bytes of padding)")
    return failures


def main(argv):
    if len(argv) >= 2 and argv[1] == "compare" and len(argv) == 4:
        reason = compare(read(argv[2]), read(argv[3]))
        if reason:
            print(f"pe-signature: {argv[3]} is not {argv[2]} plus a signature: {reason}")
            return 1
        offset, size = certificate_table(read(argv[3]))
        print(f"pe-signature: {argv[3]} is {argv[2]} plus a {size}-byte signature, nothing else")
        return 0
    if len(argv) >= 3 and argv[1] == "info":
        status = 0
        for path in argv[2:]:
            data = read(path)
            try:
                offset, size = certificate_table(data)
            except PeError as e:
                print(f"{path}: {e}")
                status = 1
                continue
            if (offset, size) == (0, 0):
                print(f"{path}: {len(data)} bytes, not signed")
            else:
                print(f"{path}: {len(data)} bytes, certificate table {size} bytes at {offset} "
                      f"(8-byte aligned: {offset % 8 == 0}, ends the file: {offset + size == len(data)})")
        return status
    if len(argv) == 3 and argv[1] == "self-test":
        return 1 if self_test(argv[2]) else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
