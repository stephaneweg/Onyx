#!/usr/bin/env python3
# build_docs.py -- generate the Word (.docx) and PDF (.pdf) exports of the Onyx
# documentation from the Markdown files in docs/.
#
#   .md --(pandoc + themed reference.docx)--> .docx --(docx2pdf / Word)--> .pdf
#
# The first "# Title" line of each file becomes the document title (rendered on a
# styled title block); a constant subtitle gives the project signature. Images are
# referenced relatively (../screenshots/x.png), so docs/ is passed as --resource-path.
#
# Prerequisites (once):  pip install python-docx docx2pdf pypandoc_binary
# Theme:  python docs/assets/make_reference.py   (builds docs/assets/reference.docx)
# The PDF step uses Microsoft Word (docx2pdf, Windows COM): Word must be installed.
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


def build_pdf(docx_files):
    try:
        from docx2pdf import convert
    except Exception as e:
        print(f"  PDF skipped (docx2pdf unavailable: {e})")
        return []
    made = []
    for docx in docx_files:
        pdf = os.path.splitext(docx)[0] + ".pdf"
        try:
            convert(docx, pdf)
            print(f"  PDF     {os.path.relpath(pdf, HERE)}")
            made.append(pdf)
        except Exception as e:
            print(f"  PDF FAILED {os.path.basename(docx)}: {e}")
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
