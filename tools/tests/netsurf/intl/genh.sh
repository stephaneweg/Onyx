#!/bin/sh
# tools/tests/netsurf/intl/genh.sh <intl.js> <intl-data.txt> <out.h> -- intl.js and the locale data
# as C strings (qjs_intl_js, qjs_intl_data) for qjs_intl.h; used by host.mk, netsurf-app.mk and
# build.sh. Escaped: backslashes, quotes, '?' (no trigraphs); CRs stripped; '#' lines of the data
# (its header) dropped.
set -e
{
	echo '/* generated from intl.js and intl-data.txt by genh.sh */'
	echo 'static const char qjs_intl_js[] ='
	sed -e 's/\r$//' -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/?/\\?/g' -e 's/^/"/' -e 's/$/\\n"/' "$1"
	echo ';'
	echo 'static const char qjs_intl_data[] ='
	sed -e 's/\r$//' -e '/^#/d' -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/?/\\?/g' -e 's/^/"/' -e 's/$/\\n"/' "$2"
	echo ';'
} > "$3.tmp"
mv "$3.tmp" "$3"
