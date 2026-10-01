/*
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 * Licensed under the GNU General Public License version 2.
 */

/**
 * \file
 * Onyx: XML for the scripts (qjs_xml.c, docs/06 §43): DOMParser's XML, the documents' XML
 * kinds, xslt.js (XPath, XSLT) loaded on demand, an <object>'s resource.
 */

#ifndef NETSURF_QJS_XML_H
#define NETSURF_QJS_XML_H

#include "quickjs.h"

/** the XML natives (N.parseXML, N.docKind, N.setDocKind, N.loadXslt, N.objectSource) */
void qjs_xml_natives(JSContext *ctx, JSValueConst natives);

#endif
