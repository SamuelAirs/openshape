#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The license texts and notices the app shows (About -> Licenses), made
from the exact sources the iOS app is built from (docs/LICENSING.md).

  generate   Reads the archives pinned in scripts/ios/sources.txt (from a
             folder filled by scripts/ios/mirror-sources.sh; each archive's
             SHA-256 is checked), scripts/licenses/components.json and the
             qt_attribution.json files inside Qt, and writes
             resources/licenses/index.json and resources/licenses/texts/.
  check      Checks that the committed resources/licenses/ belong to exactly
             the pins in sources.txt and the current components.json, that
             every text is there, that the texts copied from this repository
             are current, and that ipad.yml installs the pinned Qt version.
             No downloads; ctest runs it (licenses_check).
  gate       The iOS license gate (scripts/ios/build-app.sh): maps every
             file in the app's link map to the source it is built from
             (Qt modules, OpenCASCADE, FreeType, libzip, PlaneGCS, OpenShape,
             Apple's SDK) and fails on anything unknown, on a Qt module whose
             source is not pinned, and on a library the Licenses view lacks.
  self-test  Runs generate, check and gate on small made-up archives and link
             maps (ctest: licenses_selftest).

Standard-library Python 3.8+ only. Prints [PASS]/[FAIL] lines where it
checks; the exit status is the number of failures (1 for other errors).
"""

import argparse
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import tempfile

FORMAT = 1
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
DEFAULTS = {
    "sources": os.path.join(REPO, "scripts", "ios", "sources.txt"),
    "components": os.path.join(HERE, "components.json"),
    "out": os.path.join(REPO, "resources", "licenses"),
    "workflow": os.path.join(REPO, ".github", "workflows", "ipad.yml"),
}
OFFER_FILE = "source-offer.txt"
OFFER_PLACEHOLDERS = ("@VERSION@", "@BUILD@", "@SOURCE_URL@", "@LIBRARY_SOURCES@", "@CONTACT@")

# Phrases each license text must contain (guards against a wrong file).
LICENSE_MARKERS = {
    "LGPL-3.0-only": ["GNU LESSER GENERAL PUBLIC LICENSE", "Version 3, 29 June 2007"],
    "GPL-3.0-only": ["GNU GENERAL PUBLIC LICENSE", "Version 3, 29 June 2007"],
    "LGPL-2.1": ["GNU LESSER GENERAL PUBLIC LICENSE", "Version 2.1, February 1999"],
    "MPL-2.0": ["Mozilla Public License Version 2.0"],
    "OFL-1.1": ["SIL OPEN FONT LICENSE Version 1.1"],
    "FTL": ["The FreeType Project LICENSE"],
    "OCCT-exception": ["Open CASCADE exception (version 1.0)"],
}


class Failure(Exception):
    pass


# ---------------------------------------------------------------- inputs

def read_text_file(path):
    with open(path, "rb") as f:
        return normalize(f.read())


def normalize(data):
    """Bytes or text -> text with LF line ends, no trailing blank lines."""
    if isinstance(data, bytes):
        try:
            text = data.decode("utf-8")
        except UnicodeDecodeError:
            text = data.decode("latin-1")
    else:
        text = data
    if text.startswith("﻿"):
        text = text[1:]
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    return text.rstrip() + "\n"


def sha256_text(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def read_sources(path):
    """sources.txt: '<name> <version> <sha256> <url>' lines, '#' comments."""
    sources = []
    seen = set()
    with open(path, encoding="utf-8", newline="") as f:
        for number, line in enumerate(f, 1):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split()
            if len(parts) != 4:
                raise Failure(f"{path}:{number}: expected '<name> <version> <sha256> <url>'")
            name, version, sha, url = parts
            if not re.fullmatch(r"[0-9a-f]{64}", sha):
                raise Failure(f"{path}:{number}: '{sha}' is not a SHA-256")
            if name in seen:
                raise Failure(f"{path}:{number}: '{name}' is pinned twice")
            seen.add(name)
            sources.append({"name": name, "version": version, "sha256": sha, "url": url})
    if not sources:
        raise Failure(f"{path}: no sources")
    return sources


def archive_extension(url):
    for ext in (".tar.gz", ".tar.xz", ".tar.bz2"):
        if url.endswith(ext):
            return ext
    raise Failure(f"unsupported archive type: {url}")


def archive_path(folder, source):
    """Where mirror-sources.sh / fetch-source.sh put an archive."""
    return os.path.join(folder, f"{source['name']}-{source['version']}{archive_extension(source['url'])}")


def read_components(path):
    text = read_text_file(path)
    try:
        data = json.loads(text)
    except ValueError as e:
        raise Failure(f"{path}: {e}")
    return data, sha256_text(text)


class Archive:
    """A source archive; paths inside it leave out its top folder."""

    def __init__(self, path, name):
        self.path = path
        self.name = name
        self._names = None

    def _open(self):
        return tarfile.open(self.path, "r:*")

    def names(self):
        if self._names is None:
            names = []
            with self._open() as tar:
                for member in tar:
                    if member.isfile():
                        names.append(strip_top(member.name))
            self._names = names
        return self._names

    def read(self, wanted):
        """{path: text} for the wanted paths (one pass through the archive)."""
        wanted = set(wanted)
        found = {}
        if not wanted:
            return found
        with self._open() as tar:
            for member in tar:
                if not member.isfile():
                    continue
                name = strip_top(member.name)
                if name in wanted:
                    found[name] = normalize(tar.extractfile(member).read())
                    if len(found) == len(wanted):
                        break
        missing = wanted - set(found)
        if missing:
            raise Failure(f"{self.name}: not in {os.path.basename(self.path)}: {', '.join(sorted(missing))}")
        return found


def strip_top(name):
    parts = name.split("/", 1)
    return parts[1] if len(parts) == 2 else ""


def spdx_ids(expression):
    """License and exception ids of an SPDX expression."""
    tokens = re.split(r"[\s()]+", expression or "")
    return [t for t in tokens if t and t not in ("AND", "OR", "WITH")]


def first_sentence(text):
    text = " ".join((text or "").split())
    match = re.match(r"(.+?\.)(\s|$)", text)
    return match.group(1) if match else text


# ---------------------------------------------------------------- generate

class TextStore:
    """The license texts, one file per distinct text."""

    def __init__(self):
        self.files = {}      # file name -> text
        self.by_hash = {}    # sha256 -> file name

    def add(self, stem, text):
        digest = sha256_text(text)
        if digest in self.by_hash:
            return self.by_hash[digest]
        stem = re.sub(r"\.(txt|TXT|md)$", "", stem)
        base = re.sub(r"[^A-Za-z0-9._-]+", "-", stem).strip("-.") or "text"
        name = base + ".txt"
        n = 2
        while name in self.files:
            name = f"{base}-{n}.txt"
            n += 1
        self.files[name] = text
        self.by_hash[digest] = name
        return name


def qt_attribution_entries(archive, names, texts):
    """[(folder, entry)] from every qt_attribution.json in a Qt archive."""
    entries = []
    for path in sorted(names):
        text = texts[path]
        try:
            data = json.loads(text, strict=False)  # Qt's files contain raw tabs
        except ValueError as e:
            raise Failure(f"{archive.name}: {path}: {e}")
        folder = os.path.dirname(path)
        for entry in data if isinstance(data, list) else [data]:
            entries.append((folder, entry))
    return entries


def qt_parts_libs(entry):
    parts = entry.get("QtParts")
    return parts is None or "libs" in parts


def generate(args):
    sources = read_sources(args.sources)
    by_name = {s["name"]: s for s in sources}
    components, components_sha = read_components(args.components)
    repo = args.repo

    # Every archive the components need, checked against its pin.
    needed = set()
    for c in components["components"]:
        for s in c.get("sources", []):
            if s != "repo":
                needed.add(s)
        for t in c.get("texts", []):
            if "archive" in t:
                needed.add(t["archive"])
    qt = components.get("qtThirdParty", {})
    needed.update(qt.get("archives", []))
    unknown = sorted(n for n in needed if n not in by_name)
    if unknown:
        raise Failure(f"components.json names sources that sources.txt does not pin: {', '.join(unknown)}")
    archives = {}
    for name in sorted(needed):
        path = archive_path(args.archives, by_name[name])
        if not os.path.isfile(path):
            raise Failure(f"{path} is missing (scripts/ios/mirror-sources.sh <dir> fills the folder)")
        actual = sha256_file(path)
        if actual != by_name[name]["sha256"]:
            raise Failure(f"{path}: SHA-256 {actual} is not the pinned {by_name[name]['sha256']}")
        archives[name] = Archive(path, name)

    store = TextStore()
    out_components = []

    def source_text(names):
        parts = []
        for n in names:
            if n == "repo":
                parts.append("OpenShape's repository, https://github.com/SamuelAirs/openshape")
            else:
                s = by_name[n]
                parts.append(f"{s['url']} (SHA-256 {s['sha256']})")
        return "; ".join(parts)

    # Texts from the archives, read in one pass per archive.
    wanted = {}
    for c in components["components"]:
        for t in c.get("texts", []):
            if "archive" in t:
                wanted.setdefault(t["archive"], set()).add(t["path"])
    archive_texts = {name: archives[name].read(paths) for name, paths in wanted.items()}

    for c in components["components"]:
        for key in ("id", "name", "group", "license", "usedFor", "texts"):
            if not c.get(key):
                raise Failure(f"components.json: a component lacks '{key}': {c.get('id')}")
        version = c.get("version")
        if not version:
            pinned = [by_name[s]["version"] for s in c.get("sources", []) if s != "repo"]
            version = pinned[0] if pinned else ""
        texts = []
        for t in c["texts"]:
            if "repo" in t:
                text = read_text_file(os.path.join(repo, t["repo"]))
                stem = f"{c['id']}-{os.path.basename(t['repo'])}"
            else:
                text = archive_texts[t["archive"]][t["path"]]
                stem = f"{c['id']}-{os.path.basename(t['path'])}"
            texts.append({"title": t["title"], "file": store.add(stem, text)})
        entry = {
            "id": c["id"],
            "name": c["name"],
            "group": c["group"],
            "version": version,
            "license": c["license"],
            "usedFor": c["usedFor"],
            "copyright": c.get("copyright", ""),
            "notice": c.get("notice", ""),
            "homepage": c.get("homepage", ""),
            "source": source_text(c.get("sources", [])),
            "texts": texts,
        }
        out_components.append(entry)

    # Third-party code inside Qt.
    include = set(qt.get("include", []))
    exclude = dict(qt.get("exclude", {}))
    exclude_paths = dict(qt.get("excludePaths", {}))
    undecided = []
    included = {}
    for name in qt.get("archives", []):
        archive = archives[name]
        all_names = archive.names()
        attribution_names = [n for n in all_names if n.endswith("qt_attribution.json")]
        license_dir = {n for n in all_names if n.startswith("LICENSES/")}
        entries = qt_attribution_entries(archive, attribution_names, archive.read(attribution_names))
        # The files each included entry needs.
        plan = []
        need = set()
        for folder, e in entries:
            eid = e.get("Id") or ""
            if not qt_parts_libs(e):
                continue
            path_reason = next((r for p, r in exclude_paths.items() if (folder + "/").startswith(p)), None)
            if eid in exclude or path_reason:
                continue
            if eid not in include:
                undecided.append(f"{name}:{folder} ({eid}: {e.get('Name')}, {e.get('LicenseId')})")
                continue
            if eid in included:
                raise Failure(f"Qt third-party id '{eid}' appears twice ({included[eid]} and {name}:{folder})")
            files = e.get("LicenseFiles") or e.get("LicenseFile") or []
            if isinstance(files, str):
                files = [files]
            license_files = [os.path.normpath(os.path.join(folder, f)).replace(os.sep, "/") for f in files]
            if not license_files:
                for spdx in spdx_ids(e.get("LicenseId")):
                    candidate = f"LICENSES/{spdx}.txt"
                    if candidate in license_dir:
                        license_files.append(candidate)
                    else:
                        raise Failure(f"{name}: no license text for '{spdx}' ({eid}; {candidate} missing)")
            copyright_file = e.get("CopyrightFile")
            copyright_path = os.path.normpath(os.path.join(folder, copyright_file)).replace(os.sep, "/") if copyright_file else None
            need.update(license_files)
            if copyright_path:
                need.add(copyright_path)
            plan.append((folder, e, license_files, copyright_path))
            included[eid] = f"{name}:{folder}"
        found = archive.read(need)
        for folder, e, license_files, copyright_path in plan:
            eid = e["Id"]
            copyright_value = e.get("Copyright", "")
            if isinstance(copyright_value, list):
                copyright_value = "\n".join(copyright_value)
            if copyright_path:
                copyright_value = found[copyright_path].strip()
            texts = []
            for path in license_files:
                texts.append({"title": os.path.basename(path), "file": store.add(f"qt-{eid}-{os.path.basename(path)}", found[path])})
            version = str(e.get("Version") or "")
            if re.fullmatch(r"[0-9a-f]{20,}", version):
                version = "commit " + version[:12]  # a git commit id, as some of Qt's copies give
            out_components.append({
                "id": "qt:" + eid,
                "name": e.get("Name") or eid,
                "group": "qt",
                "version": version,
                "license": e.get("LicenseId") or e.get("License") or "",
                "usedFor": first_sentence(e.get("QtUsage")),
                "copyright": copyright_value,
                "notice": "",
                "homepage": e.get("Homepage") or "",
                "source": f"part of Qt: {by_name[name]['url']} ({folder})",
                "texts": texts,
            })
    if undecided:
        raise Failure("Qt third-party code neither included nor excluded in components.json "
                      "(qtThirdParty; decide each):\n  " + "\n  ".join(undecided))
    missing = sorted(include - set(included))
    if missing:
        raise Failure(f"components.json includes Qt third-party ids the Qt archives do not have: {', '.join(missing)}")

    index = {
        "format": FORMAT,
        "comment": "Generated by scripts/licenses/licenses.py generate; do not edit (docs/LICENSING.md).",
        "componentsSha256": components_sha,
        "sources": sources,
        "components": out_components,
    }
    os.makedirs(os.path.join(args.out, "texts"), exist_ok=True)
    for old in os.listdir(os.path.join(args.out, "texts")):
        if old.endswith(".txt") and old not in store.files:
            os.remove(os.path.join(args.out, "texts", old))
    for name, text in store.files.items():
        with open(os.path.join(args.out, "texts", name), "w", encoding="utf-8", newline="") as f:
            f.write(text)
    with open(os.path.join(args.out, "index.json"), "w", encoding="utf-8", newline="") as f:
        f.write(json.dumps(index, indent=1, ensure_ascii=False) + "\n")
    size = sum(len(t.encode("utf-8")) for t in store.files.values())
    groups = {}
    for c in out_components:
        groups[c["group"]] = groups.get(c["group"], 0) + 1
    print(f"licenses.py: {len(out_components)} components ({', '.join(f'{v} {k}' for k, v in sorted(groups.items()))}), "
          f"{len(store.files)} texts ({size // 1024} KiB) in {args.out}")
    return 0


# ---------------------------------------------------------------- check

class Checks:
    def __init__(self, quiet=False):
        self.failures = 0
        self.count = 0
        self.quiet = quiet
        self.failed = []

    def __call__(self, ok, what, detail=""):
        self.count += 1
        if not ok:
            self.failures += 1
            self.failed.append(what)
        if not self.quiet or not ok:
            print(f"[{'PASS' if ok else 'FAIL'}] {what}{(': ' + detail) if detail and not ok else ''}")
        return ok


def load_index(folder):
    path = os.path.join(folder, "index.json")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def check(args, checks=None):
    checks = checks or Checks(args.quiet)
    try:
        sources = read_sources(args.sources)
        components, components_sha = read_components(args.components)
        index = load_index(args.out)
    except (OSError, ValueError, Failure) as e:
        checks(False, "licenses: the inputs and resources/licenses/index.json can be read", str(e))
        return checks.failures

    checks(index.get("format") == FORMAT, "licenses: index.json has the current format", str(index.get("format")))
    checks(index.get("sources") == sources,
           "licenses: the texts were generated from exactly the archives pinned in sources.txt",
           "regenerate: python scripts/licenses/licenses.py generate --archives <mirror dir> (BUILDING.md)")
    checks(index.get("componentsSha256") == components_sha,
           "licenses: the texts were generated from the current components.json",
           "regenerate after editing scripts/licenses/components.json")

    listed = {c["id"]: c for c in index.get("components", [])}
    for c in components["components"]:
        got = listed.get(c["id"])
        checks(got is not None and got.get("license") == c["license"] and got.get("name") == c["name"],
               f"licenses: {c['name']} is listed with its license ({c['license']})")
    for eid in components.get("qtThirdParty", {}).get("include", []):
        checks(("qt:" + eid) in listed, f"licenses: Qt's third-party part '{eid}' is listed")

    texts_dir = os.path.join(args.out, "texts")
    referenced = set()
    empty = []
    missing = []
    for c in index.get("components", []):
        if not c.get("texts"):
            empty.append(c.get("id", "?"))
        for t in c.get("texts", []):
            referenced.add(t["file"])
            path = os.path.join(texts_dir, t["file"])
            if not os.path.isfile(path) or os.path.getsize(path) < 20:
                missing.append(t["file"])
    checks(not empty, "licenses: every component has a license text", ", ".join(empty))
    checks(not missing, "licenses: every license text is there", ", ".join(missing))
    present = {f for f in os.listdir(texts_dir) if f.endswith(".txt")} if os.path.isdir(texts_dir) else set()
    stale = sorted(present - referenced)
    checks(not stale, "licenses: no text is left over from an earlier generation", ", ".join(stale))

    # Texts copied from this repository must still match it.
    by_file = {}
    for c in index.get("components", []):
        for t in c.get("texts", []):
            by_file.setdefault(c["id"], []).append(t["file"])
    for c in components["components"]:
        for n, t in enumerate(c.get("texts", [])):
            if "repo" not in t:
                continue
            files = by_file.get(c["id"], [])
            ok = n < len(files) and os.path.isfile(os.path.join(texts_dir, files[n]))
            if ok:
                ok = read_text_file(os.path.join(texts_dir, files[n])) == read_text_file(os.path.join(args.repo, t["repo"]))
            checks(ok, f"licenses: the copy of {t['repo']} is current")

    # The license texts are the right ones.
    def text_of(cid, n):
        files = by_file.get(cid, [])
        return read_text_file(os.path.join(texts_dir, files[n])) if n < len(files) and os.path.isfile(os.path.join(texts_dir, files[n])) else ""
    for cid, n, key in (("qt", 0, "LGPL-3.0-only"), ("qt", 1, "GPL-3.0-only"), ("occt", 0, "OCCT-exception"),
                        ("occt", 1, "LGPL-2.1"), ("planegcs", 0, "LGPL-2.1"), ("openshape", 0, "MPL-2.0"),
                        ("notosans", 0, "OFL-1.1"), ("freetype", 1, "FTL")):
        if cid not in listed:
            continue
        text = text_of(cid, n)
        checks(all(m in text for m in LICENSE_MARKERS[key]), f"licenses: {cid}'s text {n + 1} is the {key} text")

    offer = os.path.join(args.out, OFFER_FILE)
    offer_text = read_text_file(offer) if os.path.isfile(offer) else ""
    checks(all(p in offer_text for p in OFFER_PLACEHOLDERS),
           f"licenses: {OFFER_FILE} is there with its placeholders ({' '.join(OFFER_PLACEHOLDERS)})")

    if args.workflow:
        qt_version = next((s["version"] for s in sources if s["name"] == "qtbase"), None)
        workflow_version = None
        if os.path.isfile(args.workflow):
            with open(args.workflow, encoding="utf-8") as f:
                match = re.search(r"^\s*QT_VERSION:\s*['\"]?([0-9.]+)", f.read(), re.M)
                workflow_version = match.group(1) if match else None
        checks(qt_version is not None and qt_version == workflow_version,
               f"licenses: ipad.yml installs the pinned Qt ({workflow_version} vs. qtbase {qt_version} in sources.txt)")

    print(f"licenses check: {checks.count - checks.failures}/{checks.count} passed")
    return checks.failures


# ---------------------------------------------------------------- gate

def link_map_inputs(path):
    """The input files of an ld64 / ld-prime link map ('# Object files:')."""
    inputs = []
    section = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if line.startswith("#"):
                section = line.strip().lower().startswith("# object files")
                continue
            if not section:
                continue
            match = re.match(r"^\[\s*\d+\]\s+(.*)$", line)
            if not match:
                continue
            item = match.group(1).strip()
            if item.startswith("linker synthesized") or not item:
                continue
            # ld64: /x/libfoo.a(bar.o); ld-prime: /x/libfoo.a[12](bar.o); the
            # binary of a static framework has no extension (Qt for iOS):
            # /x/QtCore.framework/QtCore(bar.o)
            archive = re.match(r"^(.+?)(\[\d+\])?\(([^()/]+)\)$", item)
            inputs.append(archive.group(1) if archive else item)
    return inputs


def under(path, folder):
    if not folder:
        return None
    path = path.replace("\\", "/")
    folder = folder.replace("\\", "/").rstrip("/") + "/"
    return path[len(folder):] if path.startswith(folder) else None


def is_system(path):
    p = path.replace("\\", "/")
    return (p.endswith(".tbd") or p.endswith(".dylib") or ".sdk/" in p or "/Contents/Developer/" in p
            or "/Toolchains/" in p or p.startswith("/usr/lib/") or p.startswith("/System/"))


def classify(path, rules, folders):
    """(kind, component or source name) for one link-map input."""
    candidates = [path]
    real = os.path.realpath(path) if os.path.isabs(path) else path
    if real != path:
        candidates.append(real)
    for p in candidates:
        rel = under(p, folders.get("qt"))
        if rel is not None:
            for pattern, source in rules["qt"]:
                if re.search(pattern, rel):
                    return "qt", source
            return "unknown", f"a Qt file no rule covers ({rel})"
        rel = under(p, folders.get("deps"))
        if rel is not None:
            for pattern, component in rules["deps"]:
                if re.search(pattern, rel):
                    return "component", component
            return "unknown", f"a library in the iOS libraries' prefix no rule covers ({rel})"
        rel = under(p, folders.get("build"))
        if rel is not None:
            for pattern, component in rules["build"]:
                if re.search(pattern, rel):
                    return "component", component
            return "unknown", f"a build output no rule covers ({rel})"
    if is_system(path):
        return "system", "Apple SDK / Xcode toolchain"
    return "unknown", "not from Qt, the iOS libraries, the build or Apple's SDK"


def gate(args):
    checks = Checks(False)
    components, _ = read_components(args.components)
    rules = components["linkRules"]
    index = load_index(args.out)
    listed = {c["id"] for c in index.get("components", [])}
    pinned = {s["name"] for s in index.get("sources", [])}
    folders = {"qt": args.qt_dir, "deps": args.deps_dir, "build": args.build_dir}

    if not os.path.isfile(args.link_map):
        checks(False, "gate: the app's link map exists", args.link_map)
        if args.report:
            with open(args.report, "w", encoding="utf-8", newline="") as f:
                f.write(f"License gate: FAIL (no link map: {args.link_map})\n")
        return checks.failures
    inputs = link_map_inputs(args.link_map)
    checks(len(inputs) > 0, "gate: the link map lists the app's input files", args.link_map)

    per = {}        # (kind, name) -> set of basenames
    unknown = []
    bundled = set()
    for path in inputs:
        kind, name = classify(path, rules, folders)
        if kind == "unknown":
            unknown.append(f"{path}: {name}")
            continue
        per.setdefault((kind, name), set()).add(os.path.basename(path))
        match = re.search(r"Qt6?Bundled([A-Za-z0-9]+)(\.a$|\.framework/)", path)
        if match:
            bundled.add(match.group(1))

    checks(not unknown, "gate: every file linked into the app has a known origin", "\n  " + "\n  ".join(sorted(set(unknown))))
    qt_sources = sorted(name for kind, name in per if kind == "qt")
    for source in qt_sources:
        checks(source in pinned, f"gate: Qt module source '{source}' is pinned in sources.txt (the LGPL's source code)")
    if qt_sources:
        checks("qt" in listed, "gate: Qt is in the Licenses view")
    for kind, name in sorted(per):
        if kind == "component":
            checks(name in listed, f"gate: {name} is in the Licenses view")
    for name in sorted(bundled):
        ids = rules.get("qtBundled", {}).get(name)
        checks(ids is not None and all(("qt:" + i) in listed for i in ids),
               f"gate: Qt's bundled {name} is in the Licenses view", "add it to linkRules.qtBundled and qtThirdParty.include")

    lines = ["What the iOS app links, by origin (scripts/licenses/licenses.py gate):"]
    for (kind, name), files in sorted(per.items()):
        shown = ", ".join(sorted(files)[:12]) + (f", ... ({len(files)} files)" if len(files) > 12 else "")
        lines.append(f"  {kind} {name}: {shown}")
    if bundled:
        lines.append("  Qt bundled third-party libraries: " + ", ".join(sorted(bundled)))
    if unknown:
        lines.append("  UNKNOWN:")
        lines.extend("    " + u for u in sorted(set(unknown)))
    lines.extend("  FAILED: " + f for f in checks.failed)
    lines.append(f"License gate: {'PASS' if checks.failures == 0 else 'FAIL'} ({checks.count - checks.failures}/{checks.count} checks)")
    report = "\n".join(lines) + "\n"
    print(report, end="")
    if args.report:
        with open(args.report, "w", encoding="utf-8", newline="") as f:
            f.write(report)
    return checks.failures


# ---------------------------------------------------------------- self-test

def self_test(_args):
    checks = Checks(False)

    def tar_gz(path, top, files):
        with tarfile.open(path, "w:gz") as tar:
            for name, text in files.items():
                data = text.encode("utf-8")
                info = tarfile.TarInfo(f"{top}/{name}")
                info.size = len(data)
                tar.addfile(info, io.BytesIO(data))

    with tempfile.TemporaryDirectory() as tmp:
        repo = os.path.join(tmp, "repo")
        mirror = os.path.join(tmp, "mirror")
        out = os.path.join(repo, "resources", "licenses")
        os.makedirs(os.path.join(repo, "third_party"))
        os.makedirs(mirror)
        os.makedirs(out)
        with open(os.path.join(repo, "LICENSE"), "w", encoding="utf-8", newline="") as f:
            f.write("Mozilla Public License Version 2.0\r\n==================\r\n")  # CRLF: normalized
        with open(os.path.join(out, OFFER_FILE), "w", encoding="utf-8", newline="") as f:
            f.write(" ".join(OFFER_PLACEHOLDERS) + "\n")
        attribution = [
            {"Id": "kept", "Name": "Kept Library", "LicenseId": "MIT", "LicenseFile": "COPYING",
             "QtUsage": "Used in Qt Core. More words.", "Copyright": ["Copyright 1 A", "Copyright 2 B"], "Version": "1.0"},
            {"Id": "spdxonly", "Name": "SPDX only", "LicenseId": "(CC0-1.0 OR Apache-2.0)", "QtUsage": "Used.", "Copyright": "C",
             "Version": "ed1974ea83433eba7b2d95c5dcd9ac33cb847913"},
            {"Id": "dropped", "Name": "Dropped", "LicenseId": "MIT", "LicenseFile": "COPYING", "Copyright": "D"},
            {"Id": "tool", "Name": "A tool", "QtParts": ["tools"], "LicenseId": "GPL-3.0-only", "Copyright": "T"},
        ]
        qt_files = {
            "LICENSES/LGPL-3.0-only.txt": "GNU LESSER GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n",
            "LICENSES/GPL-3.0-only.txt": "GNU GENERAL PUBLIC LICENSE\nVersion 3, 29 June 2007\n",
            "LICENSES/CC0-1.0.txt": "CC0 1.0 Universal text\n",
            "LICENSES/Apache-2.0.txt": "Apache License Version 2.0 text\n",
            # Qt's attribution files contain raw tabs inside strings.
            "src/3rdparty/kept/qt_attribution.json": json.dumps(attribution).replace("More words.", "More\twords."),
            "src/3rdparty/kept/COPYING": "MIT license of the kept library\n",
            "src/3rdparty/wayland/qt_attribution.json": json.dumps({"Id": "wl", "Name": "W", "LicenseId": "MIT", "Copyright": "W"}),
        }
        tar_gz(os.path.join(mirror, "qtbase-1.0.tar.gz"), "qtbase-src", qt_files)
        tar_gz(os.path.join(mirror, "lib-2.0.tar.gz"), "lib-2.0", {"LICENSE": "BSD 3-Clause license of the lib library\n"})
        sha = {n: sha256_file(os.path.join(mirror, n)) for n in os.listdir(mirror)}
        sources_path = os.path.join(tmp, "sources.txt")
        with open(sources_path, "w", encoding="utf-8", newline="") as f:
            f.write("# test pins\n")
            f.write(f"qtbase 1.0 {sha['qtbase-1.0.tar.gz']} https://example.org/qtbase-1.0.tar.gz\n")
            f.write(f"lib 2.0 {sha['lib-2.0.tar.gz']} https://example.org/lib-2.0.tar.gz\n")
        components = {
            "components": [
                {"id": "openshape", "name": "OpenShape", "group": "app", "license": "MPL-2.0", "usedFor": "app",
                 "sources": ["repo"], "texts": [{"title": "MPL", "repo": "LICENSE"}]},
                {"id": "qt", "name": "Qt", "group": "library", "license": "LGPL-3.0-only", "usedFor": "UI",
                 "sources": ["qtbase"], "texts": [{"title": "LGPL", "archive": "qtbase", "path": "LICENSES/LGPL-3.0-only.txt"},
                                                  {"title": "GPL", "archive": "qtbase", "path": "LICENSES/GPL-3.0-only.txt"}]},
                {"id": "lib", "name": "Lib", "group": "library", "license": "BSD-3-Clause", "usedFor": "files",
                 "sources": ["lib"], "texts": [{"title": "BSD", "archive": "lib", "path": "LICENSE"}]},
            ],
            "qtThirdParty": {"archives": ["qtbase"], "include": ["kept", "spdxonly"], "exclude": {"dropped": "not linked"},
                             "excludePaths": {"src/3rdparty/wayland/": "Linux"}},
            "linkRules": {
                "qt": [["^lib/(lib)?Qt6(Core|Gui|Bundled[A-Za-z0-9]+)[._]", "qtbase"], ["^lib/Qt(Core|Gui)\\.framework/", "qtbase"],
                       ["^qml/", "qtdeclarative"]],
                "qtBundled": {"Kept": ["kept"]},
                "deps": [["^lib/liblib\\.a$", "lib"]],
                "build": [["(^|/)libopenshape[A-Za-z0-9_]*\\.a$", "openshape"], ["\\.o$", "openshape"]],
            },
        }
        components_path = os.path.join(tmp, "components.json")

        def write_components():
            with open(components_path, "w", encoding="utf-8", newline="") as f:
                json.dump(components, f)

        write_components()
        ns = argparse.Namespace(sources=sources_path, components=components_path, archives=mirror, out=out,
                                repo=repo, workflow=None, quiet=True)

        def run(fn):
            try:
                return fn(ns), ""
            except Failure as e:
                return None, str(e)

        def check_failures():
            """check()'s failure count, its output hidden (failures are expected here)."""
            stdout = sys.stdout
            with open(os.devnull, "w") as devnull:
                sys.stdout = devnull
                try:
                    return check(ns, Checks(True))
                finally:
                    sys.stdout = stdout

        status, error = run(generate)
        checks(status == 0, "self-test: generate succeeds", error)
        index = load_index(out) if status == 0 else {"components": []}
        by_id = {c["id"]: c for c in index["components"]}
        checks(set(by_id) == {"openshape", "qt", "lib", "qt:kept", "qt:spdxonly"},
               "self-test: the components and the included Qt parts are listed (excluded, tools-only and excluded paths are not)",
               ", ".join(sorted(by_id)))
        kept = by_id.get("qt:kept", {})
        checks(kept.get("copyright") == "Copyright 1 A\nCopyright 2 B" and kept.get("usedFor") == "Used in Qt Core."
               and kept.get("version") == "1.0", "self-test: a Qt part keeps its copyright lines, first usage sentence and version")
        spdx_titles = [t["title"] for t in by_id.get("qt:spdxonly", {}).get("texts", [])]
        checks(by_id.get("qt:spdxonly", {}).get("version") == "commit ed1974ea8343",
               "self-test: a Qt part's commit-id version is shortened", by_id.get("qt:spdxonly", {}).get("version", ""))
        checks(spdx_titles == ["CC0-1.0.txt", "Apache-2.0.txt"], "self-test: a Qt part without a license file gets its SPDX texts",
               str(spdx_titles))
        mpl = by_id.get("openshape", {}).get("texts", [{}])[0].get("file", "")
        text = read_text_file(os.path.join(out, "texts", mpl)) if mpl else ""
        checks(text == "Mozilla Public License Version 2.0\n==================\n", "self-test: repository texts are copied with LF line ends")
        checks(by_id.get("lib", {}).get("version") == "2.0" and "SHA-256 " + sha["lib-2.0.tar.gz"] in by_id.get("lib", {}).get("source", ""),
               "self-test: a library's version and source come from its pin")

        checks(check_failures() == 0, "self-test: check passes on what generate wrote")

        # A changed pin, a changed components.json, a changed repository text: check fails.
        with open(sources_path, encoding="utf-8") as f:
            pins = f.read()
        with open(sources_path, "w", encoding="utf-8", newline="") as f:
            f.write(pins.replace("lib 2.0", "lib 2.1"))
        checks(check_failures() > 0, "self-test: check fails after a version change without regenerating")
        with open(sources_path, "w", encoding="utf-8", newline="") as f:
            f.write(pins)
        with open(os.path.join(repo, "LICENSE"), "a", encoding="utf-8", newline="") as f:
            f.write("changed\n")
        checks(check_failures() > 0, "self-test: check fails when a copied repository text changed")
        with open(os.path.join(out, "texts", "stale.txt"), "w", encoding="utf-8") as f:
            f.write("left over from an earlier generation\n")
        status, error = run(generate)
        checks(status == 0 and not os.path.exists(os.path.join(out, "texts", "stale.txt")) and check_failures() == 0,
               "self-test: generating again removes left-over texts and passes the check", error)
        components["qtThirdParty"]["include"].append("nothere")
        write_components()
        checks(check_failures() > 0, "self-test: check fails after components.json changed")
        status, error = run(generate)
        checks(status is None and "nothere" in error, "self-test: generate refuses an included Qt id the archive lacks", error)
        components["qtThirdParty"]["include"] = ["kept"]
        write_components()
        status, error = run(generate)
        checks(status is None and "spdxonly" in error, "self-test: generate refuses undecided Qt third-party code", error)
        components["qtThirdParty"]["include"] = ["kept", "spdxonly"]
        write_components()

        # A tampered archive: refused.
        with open(os.path.join(mirror, "lib-2.0.tar.gz"), "ab") as f:
            f.write(b"x")
        status, error = run(generate)
        checks(status is None and "SHA-256" in error, "self-test: generate refuses an archive whose SHA-256 differs", error)
        with open(os.path.join(mirror, "lib-2.0.tar.gz"), "rb") as f:
            data = f.read()[:-1]
        with open(os.path.join(mirror, "lib-2.0.tar.gz"), "wb") as f:
            f.write(data)
        status, error = run(generate)
        checks(status == 0, "self-test: the restored archive is accepted again", error)

        # The gate, on both link-map formats.
        qt_dir = "/Users/runner/Qt/6.11.2/ios"
        deps = "/Users/runner/openshape-ios-deps"
        build = "/Users/runner/work/openshape/openshape/build/ios"
        good = [
            "# Path: /x/OpenShape.app/OpenShape", "# Arch: arm64", "# Object files:",
            "[  0] linker synthesized",
            f"[  1] {build}/DerivedData/Build/Intermediates.noindex/openshape.build/Release-iphoneos/openshape.build/Objects-normal/arm64/main.o",
            f"[  2] {build}/lib/Release-iphoneos/libopenshape_ui.a(AppController.o)",
            f"[  3] {qt_dir}/lib/libQt6Core.a(qstring.cpp.o)",
            f"[  4] {qt_dir}/lib/libQt6BundledKept.a[7](x.c.o)",
            f"[  5] {deps}/lib/liblib.a[2](a.o)",
            f"[  6] {qt_dir}/lib/QtCore.framework/QtCore(bignum-dtoa.cc.o)",
            f"[  7] {qt_dir}/lib/QtGui.framework/QtGui[3](qimage.cpp.o)",
            "[  6] /Applications/Xcode_26.3.0.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS26.2.sdk/usr/lib/libz.tbd",
            "# Sections:", "# Address Size Segment Section", "[  9] not an input",
        ]
        map_path = os.path.join(tmp, "good.map")
        with open(map_path, "w", encoding="utf-8", newline="") as f:
            f.write("\n".join(good) + "\n")
        checks(len(link_map_inputs(map_path)) == 8 and f"{qt_dir}/lib/QtGui.framework/QtGui" in link_map_inputs(map_path),
               "self-test: the link map's inputs are read (both archive formats, static frameworks, not the sections)",
               str(link_map_inputs(map_path)))
        gns = argparse.Namespace(components=components_path, out=out, link_map=map_path, qt_dir=qt_dir, deps_dir=deps,
                                 build_dir=build, report=os.path.join(tmp, "gate.txt"))
        with open(os.devnull, "w") as devnull:
            stdout = sys.stdout
            sys.stdout = devnull
            try:
                good_failures = gate(gns)
                bad = good[:8] + [f"[  7] {qt_dir}/lib/libQt6Multimedia.a(x.o)", f"[  8] {qt_dir}/qml/QtQuick/libqtquick2plugin.a(p.o)",
                                  "[  9] /opt/homebrew/lib/libavcodec.a(y.o)", f"[ 10] {qt_dir}/lib/libQt6BundledOther.a(z.o)",
                                  f"[ 11] {qt_dir}/lib/QtMultimedia.framework/QtMultimedia(m.o)"] + good[8:]
                with open(map_path, "w", encoding="utf-8", newline="") as f:
                    f.write("\n".join(bad) + "\n")
                bad_failures = gate(gns)
            finally:
                sys.stdout = stdout
        with open(gns.report, encoding="utf-8") as f:
            report = f.read()
        # The repository's own rules and Licenses list, on a link map laid out
        # as the iOS build's (scripts/ios/build-app.sh; paths as on CI).
        real_map = os.path.join(tmp, "ios.map")
        ios_inputs = [
            f"{qt_dir}/lib/libQt6Core.a(qstring.cpp.o)", f"{qt_dir}/lib/libQt6Gui.a[3](qimage.cpp.o)",
            f"{qt_dir}/lib/libQt6Network.a(x.o)", f"{qt_dir}/lib/libQt6Qml.a(x.o)", f"{qt_dir}/lib/libQt6QmlModels.a(x.o)",
            f"{qt_dir}/lib/libQt6Quick.a(x.o)", f"{qt_dir}/lib/libQt6QuickControls2Basic.a(x.o)",
            f"{qt_dir}/lib/libQt6QuickTemplates2.a(x.o)", f"{qt_dir}/lib/libQt6QuickDialogs2QuickImpl.a(x.o)",
            f"{qt_dir}/lib/libQt6LabsFolderListModel.a(x.o)", f"{qt_dir}/lib/libQt6Svg.a(x.o)", f"{qt_dir}/lib/libQt6OpenGL.a(x.o)",
            f"{qt_dir}/lib/libQt6BundledHarfbuzz.a(hb.cc.o)", f"{qt_dir}/lib/libQt6BundledPcre2.a(x.o)",
            f"{qt_dir}/lib/libQt6BundledLibpng.a(x.o)", f"{qt_dir}/lib/libQt6BundledLibjpeg.a(x.o)",
            f"{qt_dir}/lib/libQt6BundledFreetype.a(x.o)",
            f"{qt_dir}/lib/QtCore.framework/QtCore(bignum-dtoa.cc.o)", f"{qt_dir}/lib/QtGui.framework/QtGui[3](qimage.cpp.o)",
            f"{qt_dir}/lib/QtNetwork.framework/QtNetwork(x.o)", f"{qt_dir}/lib/QtQml.framework/QtQml(x.o)",
            f"{qt_dir}/lib/QtQmlModels.framework/QtQmlModels(x.o)", f"{qt_dir}/lib/QtQuick.framework/QtQuick(x.o)",
            f"{qt_dir}/lib/QtQuickControls2Basic.framework/QtQuickControls2Basic(x.o)",
            f"{qt_dir}/lib/QtQuickTemplates2.framework/QtQuickTemplates2(x.o)",
            f"{qt_dir}/lib/QtLabsFolderListModel.framework/QtLabsFolderListModel(x.o)",
            f"{qt_dir}/lib/QtSvg.framework/QtSvg(x.o)", f"{qt_dir}/lib/QtOpenGL.framework/QtOpenGL(x.o)",
            f"{qt_dir}/lib/objects-Release/Gui_resources_1/.rcc/qrc_qpdf.cpp.o",
            f"{qt_dir}/lib/objects-Release/QuickControls2Basic_resources_2/.rcc/qrc_x.cpp.o",
            f"{qt_dir}/plugins/platforms/libqios.a(qiosintegration.mm.o)",
            f"{qt_dir}/plugins/platforms/objects-Release/QIOSIntegrationPlugin_init/QIOSIntegrationPlugin_init.cpp.o",
            f"{qt_dir}/plugins/imageformats/libqjpeg.a(x.o)", f"{qt_dir}/plugins/imageformats/libqsvg.a(x.o)",
            f"{qt_dir}/plugins/imageformats/objects-Release/QSvgPlugin_init/QSvgPlugin_init.cpp.o",
            f"{qt_dir}/plugins/iconengines/libqsvgicon.a(x.o)",
            f"{qt_dir}/qml/QtQuick/libqtquick2plugin.a(x.o)",
            f"{qt_dir}/qml/QtQuick/Controls/Basic/objects-Release/qtquickcontrols2basicstyleplugin_init/x.cpp.o",
            f"{deps}/lib/libTKernel.a(Standard.cxx.o)", f"{deps}/lib/libTKDESTEP.a(x.o)", f"{deps}/lib/libfreetype.a(x.o)",
            f"{deps}/lib/libzip.a(zip_open.c.o)",
            f"{build}/lib/Release-iphoneos/libplanegcs.a(GCS.cpp.o)", f"{build}/lib/Release-iphoneos/libopenshape_ui.a(x.o)",
            f"{build}/src/ui/Release-iphoneos/libopenshape_uiplugin.a(x.o)",
            f"{build}/DerivedData/Build/Intermediates.noindex/ArchiveIntermediates/openshape/IntermediateBuildFilesPath/"
            "openshape.build/Release-iphoneos/openshape.build/Objects-normal/arm64/main.o",
            "/Applications/Xcode_26.3.0.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/clang/17/lib/darwin/libclang_rt.ios.a(x.o)",
            "/Applications/Xcode_26.3.0.app/Contents/Developer/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk/System/Library/Frameworks/UIKit.framework/UIKit.tbd",
        ]
        with open(real_map, "w", encoding="utf-8", newline="") as f:
            f.write("# Path: OpenShape\n# Object files:\n[  0] linker synthesized\n")
            f.write("".join(f"[{n + 1:3}] {p}\n" for n, p in enumerate(ios_inputs)))
            f.write("# Sections:\n")
        rns = argparse.Namespace(components=DEFAULTS["components"], out=DEFAULTS["out"], link_map=real_map, qt_dir=qt_dir,
                                 deps_dir=deps, build_dir=build, report=os.path.join(tmp, "ios-gate.txt"))
        have_real = os.path.isfile(os.path.join(DEFAULTS["out"], "index.json"))
        with open(os.devnull, "w") as devnull:
            stdout = sys.stdout
            sys.stdout = devnull
            try:
                real_failures = gate(rns) if have_real else -1
            finally:
                sys.stdout = stdout
        real_report = open(rns.report, encoding="utf-8").read() if have_real else "resources/licenses/index.json missing"
        checks(real_failures == 0 and "qt qtsvg: " in real_report and "qt qtdeclarative: " in real_report
               and "component occt: " in real_report and "component planegcs: " in real_report,
               "self-test: the repository's gate rules cover an iOS link map (Qt modules, plugins, OCCT, FreeType, "
               "libzip, PlaneGCS, OpenShape, Apple's SDK)", real_report)

        checks(good_failures == 0, "self-test: the gate passes a link map of known origins")
        checks(bad_failures == 3 and "libQt6Multimedia.a" in report and "libavcodec.a" in report and "QtMultimedia.framework" in report
               and "'qtdeclarative' is pinned" in report and "bundled Other" in report,
               "self-test: the gate fails on an unknown Qt library, a file from elsewhere, an unpinned Qt module and an unlisted bundled library",
               f"{bad_failures} failures")

    print(f"licenses self-test: {checks.count - checks.failures}/{checks.count} passed")
    return checks.failures


# ---------------------------------------------------------------- main

def main(argv):
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)

    def common(p):
        p.add_argument("--sources", default=DEFAULTS["sources"], help="scripts/ios/sources.txt")
        p.add_argument("--components", default=DEFAULTS["components"], help="scripts/licenses/components.json")
        p.add_argument("--out", default=DEFAULTS["out"], help="resources/licenses")
        p.add_argument("--repo", default=REPO, help="the repository root (for texts copied from it)")

    p = sub.add_parser("generate", help="write resources/licenses/ from the pinned archives")
    common(p)
    p.add_argument("--archives", required=True, help="folder with the archives (scripts/ios/mirror-sources.sh)")
    p = sub.add_parser("check", help="check resources/licenses/ against the pins (no downloads)")
    common(p)
    p.add_argument("--workflow", default=DEFAULTS["workflow"], help="ipad.yml (its QT_VERSION); '' to skip")
    p.add_argument("--quiet", action="store_true", help="print failures only")
    p = sub.add_parser("gate", help="map the iOS app's link map to licensed sources")
    common(p)
    p.add_argument("--link-map", required=True)
    p.add_argument("--qt-dir", required=True, help="Qt for iOS (e.g. ~/Qt/6.11.2/ios)")
    p.add_argument("--deps-dir", required=True, help="scripts/ios/build-deps.sh's prefix")
    p.add_argument("--build-dir", required=True, help="scripts/ios/build-app.sh's build folder")
    p.add_argument("--report", help="also write the report here")
    sub.add_parser("self-test", help="test generate, check and gate on made-up inputs")

    args = parser.parse_args(argv)
    if getattr(args, "workflow", None) == "":
        args.workflow = None
    for key in ("qt_dir", "deps_dir", "build_dir"):
        value = getattr(args, key, None)
        if value:
            setattr(args, key, os.path.abspath(os.path.expanduser(value)).replace("\\", "/")
                    if not value.startswith("/") else value.rstrip("/"))
    try:
        if args.command == "generate":
            return generate(args)
        if args.command == "check":
            return min(check(args), 125)
        if args.command == "gate":
            return min(gate(args), 125)
        return min(self_test(args), 125)
    except Failure as e:
        print(f"licenses.py {args.command}: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
