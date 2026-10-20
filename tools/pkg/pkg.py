#!/usr/bin/env python3
"""GLOS packages on the build host (M10 step E, PRD D58): keys, packages, a source's signed index.

  pkg.py keygen KEY [COMMENT]       an Ed25519 key: KEY (the private seed, base64; keep it offline) and KEY.PUB
                                    ("glos-pkg-ed25519 BASE64 COMMENT", for C:\\GLOS\\KEYS\\PKG\\ on GLOS)
  pkg.py make DIR OUT.ZIP           a package: DIR's files (DOS 8.3 names) with DIR/GLOSPKG.INI, which names it
  pkg.py index REPO KEY             REPO/INDEX.TXT for every REPO/*.ZIP (each one's GLOSPKG.INI read), and
                                    REPO/INDEX.SIG, the signature over INDEX.TXT's bytes made with KEY
  pkg.py verify REPO KEY.PUB        the signature checked, and each zip's size and SHA-256

INDEX.TXT is "GLOS-PKG-INDEX 1", then a line a package: NAME, VERSION, SIZE, SHA256, FILE, SUMMARY, tab-separated.
INDEX.SIG is "glos-pkg-ed25519 BASE64 KEYCOMMENT". GLOSPKG.INI:

  [package]
  name = NOTES                      (8 letters or digits at most)
  version = 1.2
  summary = A small notes app
  dir = C:\\APPS\\NOTES              (where it goes; C:\\APPS\\NAME if not given)
  [program NOTES.EXE]               (profiles, as GLOS.CFG has them: set when installed, taken out when removed)
  title = Notes
  menu = 1
"""
import base64
import configparser
import hashlib
import os
import re
import sys
import zipfile

NAME_83 = re.compile(r"^[A-Z0-9_$~!#%&\-{}()@'^]{1,8}(\.[A-Z0-9_$~!#%&\-{}()@'^]{1,3})?$")


def keys():
    from cryptography.hazmat.primitives.asymmetric import ed25519
    from cryptography.hazmat.primitives import serialization
    return ed25519, serialization


def load_private(path):
    ed25519, _ = keys()
    seed = base64.b64decode(open(path).read().split()[0])
    return ed25519.Ed25519PrivateKey.from_private_bytes(seed)


def public_line(pk_bytes, comment):
    return "glos-pkg-ed25519 %s %s\n" % (base64.b64encode(pk_bytes).decode(), comment)


def cmd_keygen(path, comment="glos-packages"):
    ed25519, serialization = keys()
    k = ed25519.Ed25519PrivateKey.generate()
    seed = k.private_bytes(serialization.Encoding.Raw, serialization.PrivateFormat.Raw, serialization.NoEncryption())
    pk = k.public_key().public_bytes(serialization.Encoding.Raw, serialization.PublicFormat.Raw)
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    os.write(fd, (base64.b64encode(seed).decode() + "\n").encode())
    os.close(fd)
    open(path + ".PUB", "w").write(public_line(pk, comment))
    print("pkg: %s and %s.PUB" % (path, path))


def manifest(text):
    c = configparser.ConfigParser(interpolation=None, strict=False)
    c.optionxform = str
    c.read_string(text)
    if not c.has_section("package"):
        raise SystemExit("pkg: GLOSPKG.INI has no [package]")
    p = c["package"]
    name, version = p.get("name", "").strip().upper(), p.get("version", "").strip()
    if not re.match(r"^[A-Z0-9_]{1,8}$", name) or not version or "\t" in version:
        raise SystemExit("pkg: GLOSPKG.INI: a name of 8 letters or digits, and a version")
    return name, version, p.get("summary", "").strip().replace("\t", " ")


def cmd_make(src, out):
    files = []
    for root, _, names in os.walk(src):
        for n in sorted(names):
            full = os.path.join(root, n)
            rel = os.path.relpath(full, src).replace(os.sep, "/")
            if any(not NAME_83.match(part.upper()) for part in rel.split("/")):
                raise SystemExit("pkg: %s isn't a DOS (8.3) name" % rel)
            files.append((rel.upper(), full))
    if not any(r == "GLOSPKG.INI" for r, _ in files):
        raise SystemExit("pkg: no GLOSPKG.INI in " + src)
    manifest(open([f for r, f in files if r == "GLOSPKG.INI"][0]).read())
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for rel, full in sorted(files):
            z.write(full, rel)
    print("pkg: %s, %d files" % (out, len(files)))


def index_text(repo):
    lines = ["GLOS-PKG-INDEX 1"]
    for f in sorted(os.listdir(repo)):
        if not f.upper().endswith(".ZIP"):
            continue
        data = open(os.path.join(repo, f), "rb").read()
        with zipfile.ZipFile(os.path.join(repo, f)) as z:
            name, version, summary = manifest(z.read("GLOSPKG.INI").decode("latin-1"))
        lines.append("\t".join([name, version, str(len(data)), hashlib.sha256(data).hexdigest(), f, summary]))
    return ("\n".join(lines) + "\n").encode("latin-1")


def cmd_index(repo, key):
    text = index_text(repo)
    k = load_private(key)
    sig = k.sign(text)
    comment = open(key + ".PUB").read().split(None, 2)[2].strip() if os.path.exists(key + ".PUB") else "key"
    open(os.path.join(repo, "INDEX.TXT"), "wb").write(text)
    open(os.path.join(repo, "INDEX.SIG"), "w").write("glos-pkg-ed25519 %s %s\n" % (base64.b64encode(sig).decode(),
                                                                                      comment))
    print("pkg: %s/INDEX.TXT (%d packages), INDEX.SIG" % (repo, text.count(b"\n") - 1))


def cmd_verify(repo, pub):
    ed25519, _ = keys()
    pk = ed25519.Ed25519PublicKey.from_public_bytes(base64.b64decode(open(pub).read().split()[1]))
    from cryptography.exceptions import InvalidSignature
    text = open(os.path.join(repo, "INDEX.TXT"), "rb").read()
    try:
        pk.verify(base64.b64decode(open(os.path.join(repo, "INDEX.SIG")).read().split()[1]), text)
    except InvalidSignature:
        raise SystemExit("pkg: %s: INDEX.SIG isn't the signature of this INDEX.TXT by %s" % (repo, pub))
    for line in text.decode("latin-1").splitlines()[1:]:
        name, version, size, sha, f = line.split("\t")[:5]
        data = open(os.path.join(repo, f), "rb").read()
        if len(data) != int(size) or hashlib.sha256(data).hexdigest() != sha:
            raise SystemExit("pkg: %s doesn't match the index" % f)
    print("pkg: %s verified" % repo)


def main(argv):
    if len(argv) >= 2 and argv[0] == "keygen":
        cmd_keygen(*argv[1:3])
    elif len(argv) == 3 and argv[0] == "make":
        cmd_make(argv[1], argv[2])
    elif len(argv) == 3 and argv[0] == "index":
        cmd_index(argv[1], argv[2])
    elif len(argv) == 3 and argv[0] == "verify":
        cmd_verify(argv[1], argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
