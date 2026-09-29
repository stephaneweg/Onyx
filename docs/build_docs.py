#!/usr/bin/env python3
# build_docs.py -- generate the Word (.docx) and PDF (.pdf) exports of the Onyx
# documentation from the Markdown files in docs/.
#
#   .md --(pandoc + themed reference.docx)--> .docx --(Word, else LibreOffice)--> .pdf
#
# The first "# Title" line of each file becomes the document title (rendered on a
# styled title block); a constant subtitle gives the project signature. Images are
# referenced relatively (../screenshots/x.png), so docs/ is passed as --resource-path.
#
# Prerequisites (once):  pip install python-docx docx2pdf pypandoc_binary
# Theme:  python docs/assets/make_reference.py   (builds docs/assets/reference.docx)
# The PDF step uses Microsoft Word (docx2pdf, Windows COM) when it is installed, else
# LibreOffice (soffice --headless --convert-to pdf).
#
# Usage:  python docs/build_docs.py
#
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXPORTS = os.path.join(HERE, "exports")
REF = os.path.join(HERE, "assets", "reference.docx")
SUBTITLE = "Onyx · multi-process OS on Raspberry Pi 4"
os.makedirs(EXPORTS, exist_ok=True)

DOCS = [
    "01-PROJECT-OVERVIEW.md",
    "02-KERNEL-INTERNALS.md",
    "03-DEVELOPER-GUIDE.md",
    "04-USER-GUIDE.md",
    "05-CIRCLE-CHANGES.md",
    "06-NETSURF-CHANGES.md",
]


def read(p):
    with open(p, encoding="utf-8") as f:
        return f.read()


def split_title(text):
    """Pull the first `# ` heading out as the document title; return (title, body)."""
    title = None
    out = []
    for line in text.split("\n"):
        if title is None and line.startswith("# "):
            title = line[2:].strip()
            continue
        out.append(line)
    return (title or "Onyx"), "\n".join(out)


def build_docx():
    import pypandoc
    made = []
    for md in DOCS:
        src = os.path.join(HERE, md)
        if not os.path.exists(src):
            print(f"  MISSING {md} (skipped)")
            continue
        title, body = split_title(read(src))
        out = os.path.join(EXPORTS, os.path.splitext(md)[0] + ".docx")
        args = [
            "--standalone",
            "--resource-path", HERE,
            "--metadata", f"title={title}",
            "--metadata", f"subtitle={SUBTITLE}",
        ]
        if os.path.exists(REF):
            args += ["--reference-doc", REF]
        pypandoc.convert_text(body, "docx", format="gfm", outputfile=out, extra_args=args)
        print(f"  DOCX    {os.path.relpath(out, HERE)}")
        made.append(out)
    return made


def find_soffice():
    """LibreOffice's soffice, on the PATH or at its usual install places (None: absent)."""
    import shutil
    p = shutil.which("soffice")
    if p:
        return p
    for base in (os.environ.get("ProgramFiles", r"C:\Program Files"),
                 os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                 "/usr/bin", "/Applications/LibreOffice.app/Contents/MacOS"):
        for name in ("soffice.exe", "soffice"):
            for p in (os.path.join(base, "LibreOffice", "program", name), os.path.join(base, name)):
                if os.path.exists(p):
                    return p
    return None


def pdf_libreoffice(soffice, docx, pdf):
    """docx -> pdf with LibreOffice, headless. A private profile, so a LibreOffice already
    open does not swallow the conversion."""
    import subprocess, tempfile, pathlib
    with tempfile.TemporaryDirectory() as prof:
        subprocess.run([soffice, f"-env:UserInstallation={pathlib.Path(prof).as_uri()}",
                        "--headless", "--convert-to", "pdf", "--outdir", os.path.dirname(pdf), docx],
                       check=True, capture_output=True, timeout=600)
    if not os.path.exists(pdf):
        raise RuntimeError("LibreOffice produced no PDF")


def build_pdf(docx_files):
    # Microsoft Word first (docx2pdf); LibreOffice when Word is absent or fails.
    try:
        from docx2pdf import convert
    except Exception:
        convert = None
    soffice = find_soffice()
    if convert is None and soffice is None:
        print("  PDF skipped (neither Word/docx2pdf nor LibreOffice found)")
        return []
    made = []
    for docx in docx_files:
        pdf = os.path.splitext(docx)[0] + ".pdf"
        err = None
        if convert is not None:
            try:
                convert(docx, pdf)
                print(f"  PDF     {os.path.relpath(pdf, HERE)}")
                made.append(pdf)
                continue
            except Exception as e:
                err = e
                convert = None      # (Word missing: do not try it for each file)
        if soffice is not None:
            try:
                pdf_libreoffice(soffice, docx, pdf)
                print(f"  PDF     {os.path.relpath(pdf, HERE)} (LibreOffice)")
                made.append(pdf)
                continue
            except Exception as e:
                err = e
        print(f"  PDF FAILED {os.path.basename(docx)}: {err}")
    return made


def main():
    print(f"Exports -> {EXPORTS}")
    if not os.path.exists(REF):
        print("  note: themed reference.docx not found -> run docs/assets/make_reference.py")
    docx_files = build_docx()
    if not docx_files:
        print("No .docx produced.")
        return 1
    build_pdf(docx_files)
    print("Done.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
