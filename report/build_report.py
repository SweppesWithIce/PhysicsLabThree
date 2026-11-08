"""Сборка учебного отчёта.

    python report/build_report.py <шаблон.docx> <выход.docx>

Проходы: (1) собрать номера таблиц/рисунков/формул и список заголовков;
(2) собрать документ, сконвертировать в PDF через LibreOffice и найти, на какой
странице стоит каждый заголовок; (3) собрать заново с номерами страниц в содержании.
"""
import os
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from docbuilder import Report  # noqa: E402
import content  # noqa: E402


def make(template, labels, toc_entries):
    R = Report(template, labels)
    R.toc_entries = toc_entries
    content.build(R)
    return R


def pdf_pages(docx_path):
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run(["soffice", "--headless", "--convert-to", "pdf", "--outdir", tmp, docx_path],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pdf = os.path.join(tmp, os.path.splitext(os.path.basename(docx_path))[0] + ".pdf")
        text = subprocess.run(["pdftotext", "-layout", pdf, "-"], check=True,
                              capture_output=True, text=True).stdout
    return text.split("\f")


def norm(s):
    return re.sub(r"\s+", " ", s).strip().upper()


def find_pages(pages, headings, first_content_page):
    result = []
    start = first_content_page
    for level, text in headings:
        key = norm(text)[:40]
        page = "?"
        for i in range(start, len(pages)):
            if key in norm(pages[i]):
                page = i + 1
                start = i  # заголовки идут по порядку
                break
        result.append((level, text, page))
    return result


def main(template, out):
    R1 = make(template, None, [])
    placeholder = [(lvl, t, 0) for lvl, t in R1.headings]
    R2 = make(template, R1.labels, placeholder)
    R2.save(out)
    pages = pdf_pages(out)
    # содержание занимает страницы после титульного листа; ищем заголовки после него
    toc_end = next(i for i, p in enumerate(pages) if "СОДЕРЖАНИЕ" in norm(p)) + 1
    entries = find_pages(pages, R1.headings, toc_end)
    R3 = make(template, R1.labels, entries)
    R3.save(out)
    print(f"saved {out}: {len(pages) - 1} pages, labels={len(R1.labels)}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
