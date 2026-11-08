"""Сборка отчёта .docx в оформлении шаблона кафедры.

За основу берётся пример отчёта (стили, титульный лист, поля): всё содержимое
после титульного листа удаляется и заполняется заново. Подписи таблиц и
рисунков нумеруются самим Word (стили «Название таблицы»/«Название рисунка»
привязаны к спискам), а программа ведёт те же счётчики, чтобы ссылки в тексте
вида {tab:key}, {fig:key}, {eq:key} подставлялись правильными номерами.
Сборка идёт в два прохода: первый только собирает номера.
"""
import copy
import hashlib
import os
import re
import subprocess
import tempfile
import zipfile

from docx import Document
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_COLOR_INDEX
from docx.oxml import parse_xml
from docx.oxml.ns import qn
from docx.shared import Cm, Pt
from lxml import etree

M_NS = "http://schemas.openxmlformats.org/officeDocument/2006/math"
W_NS = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
TEXT_WIDTH = 9355  # ширина полосы набора в шаблоне, dxa

_omml_cache = {}


def _fix_omml(node):
    """Приводит OMML от pandoc к схеме ECMA-376, иначе Word может отказаться открыть файл:
    m:nor и m:sty в одном m:rPr недопустимы; в m:dPr порядок begChr, sepChr, endChr, grow, shp."""
    m = lambda tag: f"{{{M_NS}}}{tag}"
    for rpr in node.iter(m("rPr")):
        if rpr.find(m("nor")) is not None:
            for sty in rpr.findall(m("sty")):
                rpr.remove(sty)
    order = ["begChr", "sepChr", "endChr", "grow", "shp", "ctrlPr"]
    for dpr in node.iter(m("dPr")):
        kids = list(dpr)
        kids.sort(key=lambda e: order.index(etree.QName(e).localname)
                  if etree.QName(e).localname in order else len(order))
        for k in kids:
            dpr.remove(k)
            dpr.append(k)


def latex_to_omml(latex):
    """LaTeX → элемент <m:oMath> через pandoc (кэшируется)."""
    if latex in _omml_cache:
        return copy.deepcopy(_omml_cache[latex])
    with tempfile.TemporaryDirectory() as tmp:
        md = os.path.join(tmp, "eq.md")
        out = os.path.join(tmp, "eq.docx")
        with open(md, "w", encoding="utf8") as f:
            f.write(f"${latex}$\n")
        subprocess.run(["pandoc", md, "-o", out], check=True)
        with zipfile.ZipFile(out) as z:
            root = etree.fromstring(z.read("word/document.xml"))
    node = root.find(f".//{{{M_NS}}}oMath")
    if node is None:
        raise ValueError(f"pandoc не вернул формулу для: {latex}")
    _fix_omml(node)
    _omml_cache[latex] = node
    return copy.deepcopy(node)


class Report:
    def __init__(self, template, labels=None):
        self.doc = Document(template)
        self.labels = labels if labels is not None else {}
        self.collect = labels is None  # первый проход
        if self.collect:
            self.labels = {}
        self.n = {"tab": 0, "fig": 0, "eq": 0}
        self.headings = []  # (уровень, текст) — для содержания
        self._strip_after_title()

    # ------------------------------------------------------------------ base
    def _strip_after_title(self):
        body = self.doc.element.body
        paras = self.doc.paragraphs
        # всё, начиная с заголовка «Содержание», удаляем (кроме sectPr)
        start = next(i for i, p in enumerate(paras) if p.style.name == "Содержание")
        start_el = paras[start]._p
        remove = False
        for el in list(body):
            if el is start_el:
                remove = True
            if remove and el.tag != qn("w:sectPr"):
                body.remove(el)

    def title_page(self, work_no, topic, student, checker):
        P = self.doc.paragraphs
        def set_text(p, text, keep_highlight=False):
            runs = p.runs
            for r in runs[1:]:
                r._r.getparent().remove(r._r)
            runs[0].text = text
            if not keep_highlight:
                runs[0].font.highlight_color = None
        for p in P:
            t = p.text.strip()
            if t.startswith("ПРАКТИЧЕСКАЯ РАБОТА"):
                set_text(p, f"ПРАКТИЧЕСКАЯ РАБОТА №{work_no}")
            elif t.startswith("«") and t.endswith("»") and "Моделирование" not in t:
                set_text(p, f"«{topic}»")
            elif "Моделирование" in t:
                set_text(p, "«Моделирование физических процессов»")
            elif "студент группы" in t:
                set_text(p, student, keep_highlight=True)
        # «Проверил(а):» — следующий непустой/пустой абзац заполняем фамилией
        for i, p in enumerate(P):
            if p.text.strip() == "Проверил(а):":
                target = P[i + 1]
                if target.runs:
                    target.runs[0].text = checker
                else:
                    target.add_run(checker)
                break

    def _resolve(self, text):
        def rep(m):
            kind, key = m.group(1), m.group(2)
            return str(self.labels.get(f"{kind}:{key}", "?"))
        return re.sub(r"\{(tab|fig|eq):([\w\-]+)\}", rep, text)

    def _label(self, kind, key):
        self.n[kind] += 1
        if key:
            self.labels[f"{kind}:{key}"] = self.n[kind]
        return self.n[kind]

    # Разметка внутри текста:
    #   **жирный**, *курсив*, x_{sub}, x^{sup}, [[...]] — жёлтая подсветка (заполнить),
    #   `моноширинный` — имена файлов и функций.
    _TOKEN = re.compile(r"(\*\*.+?\*\*|\*.+?\*|_\{.+?\}|\^\{.+?\}|\[\[.+?\]\]|`.+?`)")

    def _runs(self, p, text, highlight=False, bold=False, italic=False, resolve=True):
        if resolve:
            text = self._resolve(text)
        for part in self._TOKEN.split(text):
            if not part:
                continue
            kw = dict(highlight=highlight, bold=bold, italic=italic, resolve=False)
            if part.startswith("[[") and part.endswith("]]"):
                self._runs(p, part[2:-2], **{**kw, "highlight": True})
                continue
            if part.startswith("**") and part.endswith("**"):
                self._runs(p, part[2:-2], **{**kw, "bold": True})
                continue
            if part.startswith("*") and part.endswith("*") and len(part) > 2:
                self._runs(p, part[1:-1], **{**kw, "italic": True})
                continue
            if part.startswith("_{"):
                r = p.add_run(part[2:-1]); r.font.subscript = True
            elif part.startswith("^{"):
                r = p.add_run(part[2:-1]); r.font.superscript = True
            elif part.startswith("`"):
                r = p.add_run(part[1:-1]); r.font.name = "Consolas"; r.font.size = Pt(11)
            else:
                r = p.add_run(part)
            if bold:
                r.bold = True
            if italic and not part.startswith(("_{", "^{")):
                r.italic = True
            if highlight:
                r.font.highlight_color = WD_COLOR_INDEX.YELLOW

    # ------------------------------------------------------------- blocks
    def toc(self, entries):
        """Статическое содержание (как в шаблоне): entries = [(level, text, page)]."""
        self.doc.add_paragraph("Содержание", style="Содержание")
        for level, text, page in entries:
            p = self.doc.add_paragraph(style=f"TOC {level}")
            p.add_run(f"{text}\t{page}")

    def h1(self, text):
        self.headings.append((1, text))
        self.doc.add_paragraph(text, style="ВЗИ")

    def h2(self, text):
        self.headings.append((2, text))
        # python-docx переводит «Heading 2» в «heading 2» и не находит стиль шаблона,
        # поэтому берём стиль по его id
        st = next(s for s in self.doc.styles if s.style_id == "Heading2")
        self.doc.add_paragraph(text, style=st)

    def p(self, text, highlight=False, indent=True):
        par = self.doc.add_paragraph(style="Normal")
        if not indent:
            par.paragraph_format.first_line_indent = Cm(0)
        self._runs(par, text, highlight)
        return par

    def todo(self, text):
        """Жёлтый абзац-заглушка: будет заполнено на следующих шагах."""
        return self.p(f"[[{text}]]")

    def bullets(self, items):
        for it in items:
            par = self.doc.add_paragraph(style="Maрк. список")
            self._runs(par, it)

    def numbered(self, items):
        for i, it in enumerate(items, 1):
            par = self.doc.add_paragraph(style="Normal")
            self._runs(par, f"{i}. {it}")

    def eq(self, latex, key=None):
        """Выключная формула: по центру, номер (N) у правого края."""
        n = self._label("eq", key)
        par = self.doc.add_paragraph(style="Normal")
        pPr = par._p.get_or_add_pPr()
        pPr.append(parse_xml(
            f'<w:tabs xmlns:w="{W_NS}"><w:tab w:val="center" w:pos="{TEXT_WIDTH // 2}"/>'
            f'<w:tab w:val="right" w:pos="{TEXT_WIDTH}"/></w:tabs>'))
        par.paragraph_format.first_line_indent = Cm(0)
        par.paragraph_format.space_before = Pt(6)
        par.paragraph_format.space_after = Pt(6)
        par.add_run("\t")
        par._p.append(latex_to_omml(latex))
        par.add_run(f"\t({n})")
        return n

    def where(self, items):
        """Расшифровка обозначений под формулой: «где x – ...;»."""
        for i, (sym, meaning) in enumerate(items):
            prefix = "где " if i == 0 else ""
            end = "." if i == len(items) - 1 else ";"
            par = self.p(f"{prefix}{sym} – {meaning}{end}")
            if i > 0:
                par.paragraph_format.left_indent = Cm(1.25)
                par.paragraph_format.first_line_indent = Cm(0)

    def table(self, caption, header, rows, widths, key=None, align=None):
        """Таблица в стиле шаблона: подпись сверху, сетка 0,5 pt, шапка жирная."""
        self._label("tab", key)
        self.doc.add_paragraph(self._resolve(caption), style="Название таблицы")
        assert abs(sum(widths) - TEXT_WIDTH) < 400 or sum(widths) <= TEXT_WIDTH
        ncol = len(header)
        align = align or (["left"] + ["center"] * (ncol - 1))
        tbl = self.doc.add_table(rows=0, cols=ncol)
        tblPr = tbl._tbl.tblPr
        for el in list(tblPr):
            if el.tag in (qn("w:tblStyle"), qn("w:tblW"), qn("w:tblLook")):
                tblPr.remove(el)
        tblPr.append(parse_xml(
            f'<w:tblW xmlns:w="{W_NS}" w:w="{sum(widths)}" w:type="dxa"/>'))
        tblPr.append(parse_xml(f'<w:jc xmlns:w="{W_NS}" w:val="center"/>'))
        borders = "".join(
            f'<w:{b} w:val="single" w:sz="4" w:space="0" w:color="000000"/>'
            for b in ("top", "left", "bottom", "right", "insideH", "insideV"))
        tblPr.append(parse_xml(f'<w:tblBorders xmlns:w="{W_NS}">{borders}</w:tblBorders>'))
        tblPr.append(parse_xml(f'<w:tblLayout xmlns:w="{W_NS}" w:type="fixed"/>'))
        tblPr.append(parse_xml(
            f'<w:tblCellMar xmlns:w="{W_NS}"><w:left w:w="85" w:type="dxa"/>'
            f'<w:right w:w="85" w:type="dxa"/></w:tblCellMar>'))
        grid = tbl._tbl.tblGrid
        for gc, w in zip(grid.findall(qn("w:gridCol")), widths):
            gc.set(qn("w:w"), str(w))

        def add_row(values, head=False):
            row = tbl.add_row()
            trPr = row._tr.get_or_add_trPr()
            trPr.append(parse_xml(f'<w:{"tblHeader" if head else "cantSplit"} xmlns:w="{W_NS}"/>'))
            for j, (cell, val) in enumerate(zip(row.cells, values)):
                cell.width = Cm(widths[j] / 567)
                tcPr = cell._tc.get_or_add_tcPr()
                tcPr.append(parse_xml(f'<w:vAlign xmlns:w="{W_NS}" w:val="center"/>'))
                par = cell.paragraphs[0]
                par.style = self.doc.styles["Table"]
                pf = par.paragraph_format
                pf.line_spacing = 1.0
                pf.first_line_indent = Cm(0)
                a = "center" if head else align[j]
                par.alignment = {"left": WD_ALIGN_PARAGRAPH.LEFT, "center": WD_ALIGN_PARAGRAPH.CENTER,
                                 "right": WD_ALIGN_PARAGRAPH.RIGHT}[a]
                self._runs(par, str(val) if not head else f"**{val}**")
        add_row(header, head=True)
        for r in rows:
            add_row(r)
        # пустой абзац после таблицы, как в шаблоне
        self.doc.add_paragraph(style="Normal")

    def figure(self, path, caption, width_cm=16.0, key=None):
        self._label("fig", key)
        par = self.doc.add_paragraph(style="Normal")
        par.alignment = WD_ALIGN_PARAGRAPH.CENTER
        par.paragraph_format.first_line_indent = Cm(0)
        par.paragraph_format.keep_with_next = True
        par.add_run().add_picture(path, width=Cm(width_cm))
        cap = self.doc.add_paragraph(style="Название рисунка")
        self._runs(cap, caption)

    def page_break(self):
        from docx.enum.text import WD_BREAK
        self.doc.add_paragraph().add_run().add_break(WD_BREAK.PAGE)

    def save(self, path):
        self.doc.save(path)
