#!/usr/bin/env python3
"""tools/tests/slides/make_pptx.py -- a deck made the way PowerPoint makes them (python-pptx's template is
PowerPoint's own: placeholders that take their place and their formats from the layouts, shapes coloured by the
theme's style references, a group, a chart with its workbook, a table in PowerPoint's default style, notes), for
Slides' reader (tools/tests/slides/powerpoint.pptx; the screenshot slides-pptx.png).

    pip install python-pptx; python3 tools/tests/slides/make_pptx.py

MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
"""
import os
from pptx import Presentation
from pptx.util import Inches, Pt
from pptx.chart.data import CategoryChartData
from pptx.enum.chart import XL_CHART_TYPE
from pptx.enum.shapes import MSO_SHAPE
from pptx.dml.color import RGBColor

p = Presentation()
s = p.slides.add_slide(p.slide_layouts[0]); s.shapes.title.text = "Quarterly Review"; s.placeholders[1].text = "Prepared with PowerPoint's template"
s = p.slides.add_slide(p.slide_layouts[1]); s.shapes.title.text = "Highlights"
tf = s.placeholders[1].text_frame; tf.text = "Revenue up 12 %"
for t, l in [("Two new markets", 1), ("Costs flat", 0), ("Hiring: 4 engineers", 1), ("Next steps", 0)]:
	para = tf.add_paragraph(); para.text = t; para.level = l
s.notes_slide.notes_text_frame.text = "Mention the markets first."
s = p.slides.add_slide(p.slide_layouts[5]); s.shapes.title.text = "Sales by region"
cd = CategoryChartData(); cd.categories = ['North', 'South', 'East', 'West']
cd.add_series('2025', (12.5, 9.1, 14.2, 7.7)); cd.add_series('2026', (14.0, 10.3, 15.8, 9.9))
s.shapes.add_chart(XL_CHART_TYPE.COLUMN_CLUSTERED, Inches(0.5), Inches(1.5), Inches(6), Inches(4.5), cd).chart.has_legend = True
sh = s.shapes.add_shape(MSO_SHAPE.ROUNDED_RECTANGLE, Inches(6.8), Inches(2), Inches(2.6), Inches(1.2)); sh.text = "+11 % overall"
sh2 = s.shapes.add_shape(MSO_SHAPE.RIGHT_ARROW, Inches(6.8), Inches(3.6), Inches(2.6), Inches(1)); sh2.fill.solid(); sh2.fill.fore_color.rgb = RGBColor(0xC0, 0x50, 0x4D)
s = p.slides.add_slide(p.slide_layouts[5]); s.shapes.title.text = "Team"
tbl = s.shapes.add_table(4, 3, Inches(0.8), Inches(1.6), Inches(8.4), Inches(2.4)).table
for r, row in enumerate([("Name", "Role", "Since"), ("Ana", "Lead", "2019"), ("Bo", "Design", "2021"), ("Cy", "Ops", "2024")]):
	for c, v in enumerate(row): tbl.cell(r, c).text = v
grp = s.shapes.add_group_shape()
grp.shapes.add_shape(MSO_SHAPE.OVAL, Inches(1), Inches(4.6), Inches(1.2), Inches(1.2))
grp.shapes.add_shape(MSO_SHAPE.OVAL, Inches(2.6), Inches(4.6), Inches(1.2), Inches(1.2))
tb = s.shapes.add_textbox(Inches(4.5), Inches(4.8), Inches(4.5), Inches(1)); tb.text_frame.text = "A text box, 24 pt bold"
r = tb.text_frame.paragraphs[0].runs[0]; r.font.size = Pt(24); r.font.bold = True
out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "powerpoint.pptx")
p.save(out)
print("wrote", out)
