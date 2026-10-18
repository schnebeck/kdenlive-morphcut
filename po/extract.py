#!/usr/bin/env python3
# kdenlive-morphcut - po/extract.py
# Extracts the Kdenlive UI strings for translation.
#
#   Copyright (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
#   Produced by Thorsten Schnebeck - the idea, the decisions, the testing.
#   Written by Anthropic Claude Opus 5.5 - AI generated content.
#
#   Free software under the GNU General Public License, version 3 or later.
#   There is no warranty, to the extent permitted by law. The full text is in
#   LICENSES/GPL-3.0-or-later.txt.
#
# SPDX-FileCopyrightText: (C) 2026 Thorsten Schnebeck <thorsten.schnebeck@gmx.net>
# SPDX-FileContributor: Anthropic Claude Opus 5.5 (AI generated content)
# SPDX-License-Identifier: GPL-3.0-or-later

"""Extract the translatable strings of kdenlive/morphcut.xml into po/morphcut.pot.

Kdenlive translates composition XML with i18n() in its own "kdenlive" domain:
<name>, <description>, <comment> and each <paramlistdisplay> (as a whole,
comma separated list) are looked up verbatim.

Update a translation afterwards with: msgmerge -U po/de.po po/morphcut.pot
"""
import os
import xml.etree.ElementTree as ET

root_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
tree = ET.parse(os.path.join(root_dir, "kdenlive", "morphcut.xml"))

msgids = []
for elem in tree.iter():
    if elem.tag in ("name", "description", "comment", "paramlistdisplay") and elem.text:
        text = elem.text.strip()
        if text and text not in msgids:
            msgids.append(text)


def quote(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


with open(os.path.join(root_dir, "po", "morphcut.pot"), "w", encoding="utf-8") as out:
    out.write('msgid ""\nmsgstr ""\n'
              '"Project-Id-Version: kdenlive-morphcut\\n"\n'
              '"MIME-Version: 1.0\\n"\n'
              '"Content-Type: text/plain; charset=UTF-8\\n"\n'
              '"Content-Transfer-Encoding: 8bit\\n"\n\n')
    for m in msgids:
        out.write("#: kdenlive/morphcut.xml\nmsgid %s\nmsgstr \"\"\n\n" % quote(m))
