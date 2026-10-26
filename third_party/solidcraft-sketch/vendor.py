#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright (c) 2026 The Nightfall Authors
# SPDX-License-Identifier: Apache-2.0
"""Regenerates the vendored SolidCraft sketch solver (RFC 0012) from an upstream checkout.

Usage: vendor.py <solidcraft repository containing PIN> [--check]

Every input is read from the pinned commit's tree (`git ls-tree`/`git show PIN:<path>`), never
from the worktree, so local edits, untracked or ignored files and the checked-out branch do
not matter. The output is deterministic: it reproduces the committed tree byte for byte
(`--check` compares instead of writing). Modifications, all mechanical:

- `sketch`: drop the `fonts` and `text` modules and every test module; replace `text.rs` with
  a stub that keeps only the `TextAlign` and `TextStyle` data types (no font files, no
  outlines); drop the corresponding `pub use` lines.
- `geom`: keep only the modules the solver uses (`aabb2`, `chain`, `profile`, `vec`) and
  their re-exports.
- Both: remove serde (`Serialize`/`Deserialize` derives, `#[serde(...)]` attributes, `use
  serde` lines) and thiserror (derives and `#[error(...)]` attributes, replaced by generated
  `Display` and `Error` impls with the same messages). The crates then have no dependencies.
"""
import re
import subprocess
import sys
from pathlib import Path

PIN = "6f5d0038abffbd3e1fc26069b1b96ca89984058f"
HERE = Path(__file__).resolve().parent
GEOM_MODULES = ["aabb2", "chain", "profile", "vec"]
HEADER = ("// SPDX-FileCopyrightText: Copyright (c) 2026 SolidCraft contributors\n"
          "// SPDX-License-Identifier: MIT OR Apache-2.0\n"
          "// Vendored from SolidCraft {pin}; modified by Nightfall's\n"
          "// third_party/solidcraft-sketch/vendor.py: serde, thiserror{extra} removed.\n")


def strip_attribute(text, name):
    """Removes every `#[name(...)]` attribute, balancing parentheses and brackets."""
    out, i = [], 0
    pattern = f"#[{name}("
    while True:
        j = text.find(pattern, i)
        if j < 0:
            out.append(text[i:])
            return "".join(out)
        out.append(text[i:j])
        depth, k = 0, j + 1
        while k < len(text):
            c = text[k]
            if c in "([":
                depth += 1
            elif c in ")]":
                depth -= 1
                if depth == 0:
                    break
            elif c == '"':
                k += 1
                while text[k] != '"':
                    k += 2 if text[k] == "\\" else 1
            k += 1
        k += 1
        # Swallow the attribute's own line when it stood alone.
        line_start = text.rfind("\n", 0, j) + 1
        if text[line_start:j].strip() == "" and k < len(text) and text[k] == "\n":
            out[-1] = out[-1][: len(out[-1]) - (j - line_start)]
            k += 1
        i = k


def strip_derives(text, names):
    def fix(m):
        items = [x.strip() for x in m.group(1).split(",") if x.strip()]
        kept = [x for x in items if x not in names]
        if not kept:
            return ""
        return f"#[derive({', '.join(kept)})]"
    text = re.sub(r"#\[derive\(([^)]*)\)\]", fix, text)
    return re.sub(r"\n[ \t]*\n(?=[ \t]*(pub |enum |struct ))", "\n\n", text)


def error_impls(text):
    """Turns thiserror derives into explicit Display and Error impls with the same messages."""
    impls = []
    for m in re.finditer(r"#\[derive\(([^)]*)\)\]\s*(?:#\[error\(\"([^\"]*)\"\)\]\s*)?pub (enum|struct) (\w+)", text):
        if "thiserror::Error" not in m.group(1):
            continue
        kind, name = m.group(3), m.group(4)
        body_start = text.index("{" if kind == "enum" else "(", m.end())
        if kind == "struct":
            message = m.group(2)
            fmt = message.replace("{0}", "{}")
            args = ", self.0" if "{0}" in message else ""
            impls.append(f"impl std::fmt::Display for {name} {{\n    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {{\n        write!(f, \"{fmt}\"{args})\n    }}\n}}\n\nimpl std::error::Error for {name} {{}}\n")
            continue
        depth, k = 0, body_start
        while True:
            if text[k] == "{":
                depth += 1
            elif text[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        body = text[body_start + 1:k]
        arms = []
        for v in re.finditer(r"#\[error\(\"([^\"]*)\"\)\]\s*(\w+)(\(([^)]*)\))?", body):
            message, variant, fields = v.group(1), v.group(2), v.group(4)
            if fields:
                count = len([x for x in fields.split(",") if x.strip()])
                binds = ", ".join(f"a{n}" for n in range(count))
                fmt = re.sub(r"\{(\d+)\}", lambda d: "{a" + d.group(1) + "}", message)
                arms.append(f"            {name}::{variant}({binds}) => write!(f, \"{fmt}\"),")
            else:
                arms.append(f"            {name}::{variant} => write!(f, \"{message}\"),")
        impls.append(f"impl std::fmt::Display for {name} {{\n    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {{\n        match self {{\n" + "\n".join(arms) + "\n        }\n    }\n}\n\nimpl std::error::Error for " + name + " {}\n")
    return impls


def clean(text, extra=""):
    impls = error_impls(text)
    # Out-of-line test modules point at files that are not vendored.
    text = re.sub(r'#\[cfg\(test\)\]\n(#\[path = "[^"]+"\]\n)?(pub(\(crate\))? )?mod \w+;\n', "", text)
    text = strip_attribute(text, "error")
    text = strip_attribute(text, "serde")
    text = strip_derives(text, {"Serialize", "Deserialize", "serde::Serialize", "serde::Deserialize", "thiserror::Error"})
    text = re.sub(r"^use serde::[^;]*;\n", "", text, flags=re.M)
    if impls:
        text = text.rstrip("\n") + "\n\n" + "\n".join(impls)
    # Removed items leave blank runs behind; keep at most one blank line and one final newline.
    text = re.sub(r"\n{3,}", "\n\n", text).rstrip("\n") + "\n"
    return HEADER.format(pin=PIN, extra=extra) + text


def drop_lines(text, patterns):
    """Removes `pub use`/`mod` items (possibly multi-line) whose first line matches."""
    lines, out, skip = text.split("\n"), [], False
    for line in lines:
        if skip:
            if line.rstrip().endswith(";"):
                skip = False
            continue
        if any(re.match(p, line) for p in patterns):
            skip = not line.rstrip().endswith(";")
            continue
        out.append(line)
    return "\n".join(out)


def text_stub(upstream_text):
    """`TextAlign` and `TextStyle` with their doc comments, attributes and impls; nothing else."""
    lines = upstream_text.split("\n")
    first = next(i for i, l in enumerate(lines) if l.startswith("pub enum TextAlign"))
    while first > 0 and lines[first - 1].lstrip().startswith(("///", "#[")):
        first -= 1
    last = next(i for i, l in enumerate(lines) if l.startswith("impl TextStyle {"))
    while lines[last] != "}":
        last += 1
    helpers = [m.group(0) for m in re.finditer(r"^fn (is_false|is_zero|is_one|one)\b.*?^\}", upstream_text, re.M | re.S)]
    return ("//! Text style data kept for `LinkSource::Text`; the outline builder and its fonts are\n"
            "//! not vendored (RFC 0012: the fonts are OFL-1.1).\n\n"
            + "\n".join(lines[first:last + 1]) + "\n" + "".join(f"\n#[allow(dead_code)]\n{h}\n" for h in helpers))


class Pinned:
    """Reads files of the commit PIN from a repository's object store."""

    def __init__(self, repository):
        self.repository = str(repository)

    def git(self, *args):
        return subprocess.run(["git", "-C", self.repository, *args], capture_output=True,
                              check=True).stdout

    def files(self, directory):
        """Names of the files (blobs) directly in `directory`, sorted."""
        listing = self.git("ls-tree", "-z", PIN, "--", f"{directory}/").decode()
        names = []
        for entry in filter(None, listing.split("\0")):
            meta, path = entry.split("\t", 1)
            if meta.split()[1] == "blob":
                names.append(path.rsplit("/", 1)[-1])
        return sorted(names)

    def read(self, path):
        return self.git("show", f"{PIN}:{path}").decode()


def generate(upstream):
    files = {}
    sketch, geom = "crates/sketch/src", "crates/geom/src"
    for name in upstream.files(sketch):
        if not name.endswith(".rs"):
            continue
        if name.endswith("_tests.rs") or name in ("tests.rs", "fonts.rs", "profiling.rs"):
            continue
        text = upstream.read(f"{sketch}/{name}")
        if name == "text.rs":
            files["sketch/src/text.rs"] = clean(text_stub(text), ", font and outline code")
            continue
        if name == "lib.rs":
            text = drop_lines(text, [r"mod fonts;", r"pub use fonts::", r"pub use text::",
                                     r"#\[cfg\(test\)\]", r"#\[cfg\(feature = \"solver-profiling\"\)\]",
                                     r"pub mod profiling;"])
            text = re.sub(r"^mod (\w+_tests|tests);\n", "", text, flags=re.M)
            text = text.replace("pub use text::", "// text outlines are not vendored: pub use text::")
        files[f"sketch/src/{name}"] = clean(text)
    lib = upstream.read(f"{geom}/lib.rs")
    keep = []
    for line in lib.split("\n"):
        m = re.match(r"(pub )?mod (\w+);", line)
        if m and m.group(2) not in GEOM_MODULES:
            continue
        m = re.match(r"pub use (\w+)::", line)
        if m and m.group(1) not in GEOM_MODULES:
            continue
        if re.match(r"#\[cfg\(test\)\]|mod tests;", line):
            continue
        keep.append(line)
    files["geom/src/lib.rs"] = clean("\n".join(keep), ", and modules the sketch solver does not use")
    for module in GEOM_MODULES:
        text = upstream.read(f"{geom}/{module}.rs")
        text = re.sub(r"#\[cfg\(test\)\]\s*mod tests \{.*\Z", "", text, flags=re.S)
        files[f"geom/src/{module}.rs"] = clean(text)
    for crate, deps in (("geom", ""), ("sketch", "\n[dependencies]\nsolidcraft-geom = { path = \"../geom\" }\n")):
        files[f"{crate}/Cargo.toml"] = (
            f"# Vendored from SolidCraft {PIN}; see ../README.md.\n"
            f"[package]\nname = \"solidcraft-{crate}\"\nversion = \"0.0.0\"\nedition = \"2024\"\n"
            f"license = \"MIT OR Apache-2.0\"\npublish = false\n{deps}"
            "\n[lib]\n# Upstream's tests are not vendored (some need serde); nightfall-sketch tests "
            "the solver.\ntest = false\ndoctest = false\n"
            "\n[lints.rust]\nunsafe_code = \"forbid\"\n"
            "# Helpers that only serde attributes called, and the upstream profiling feature, stay\n"
            "# in the vendored source untouched.\n"
            "dead_code = \"allow\"\nunexpected_cfgs = \"allow\"\n")
    # Upstream's own formatting, so `cargo fmt --all --check` holds the vendored tree to the
    # style it was written in.
    files["rustfmt.toml"] = f"# SolidCraft's rustfmt.toml at {PIN}.\nmax_width = 150\nuse_small_heuristics = \"Max\"\n"
    for name in ("LICENSE-APACHE", "LICENSE-MIT", "NOTICE"):
        files[f"upstream/{name}"] = upstream.read(name)
    return files


def main():
    upstream = Pinned(Path(sys.argv[1]).resolve())
    try:
        upstream.git("cat-file", "-e", f"{PIN}^{{commit}}")
    except subprocess.CalledProcessError:
        sys.exit(f"{sys.argv[1]} does not contain commit {PIN}; fetch it first")
    files = generate(upstream)
    if "--check" in sys.argv:
        bad = [p for p, t in files.items() if not (HERE / p).exists() or (HERE / p).read_text() != t]
        extra = [str(p.relative_to(HERE)) for p in HERE.rglob("*") if p.is_file() and p.name != "vendor.py"
                 and p.name != "README.md" and str(p.relative_to(HERE)) not in files]
        if bad or extra:
            sys.exit(f"vendored tree differs: {bad[:5]} extra: {extra[:5]}")
        print(f"vendored tree matches {PIN} ({len(files)} files)")
        return
    for path, text in files.items():
        target = HERE / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text)
    print(f"wrote {len(files)} files from {PIN}")


if __name__ == "__main__":
    main()
