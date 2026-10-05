# mupdf.mk -- MuPDF (third_party/mupdf-1.28.5: its fitz + pdf parts only) for the PDF Viewer, as one static
# library: $(MU_OUT)/libmupdf.a. Included by user/Makefile (the aarch64 newlib build) and run by
# tools/tests/desktop_sim/shots.sh with the host's gcc (MU_CC=gcc MU_OUT=... MU_CFLAGS=-O2):
#
#	make -f user/Apps/pdf/mupdf.mk MU_ROOT=. MU_CC=gcc MU_OUT=/tmp/x MU_CFLAGS=-O2
#
# What is left out (docs/pdf/README.md): the other formats (XPS, EPUB, HTML, SVG, images...), JavaScript, the
# ICC colour management (lcms2), the Noto / CJK fonts (TOFU: the 14 standard fonts stay), the barcodes, OCR,
# the spot / CMYK plotters. Its third-party libraries: jbig2dec, openjpeg (from MuPDF's tarball), Onyx's zlib,
# libjpeg (jpeg-9f) and FreeType (2.14.3 -- with the CFF / Type 1 / CID drivers PDF fonts need, plus the
# TrueType + autofit ones of the apps' text: the whole app links this FreeType instead of user/Kits/fontkit/libft.a).

MU_ROOT ?= ..
MU_CC ?= aarch64-none-elf-gcc
MU_AR ?= $(patsubst %gcc,%ar,$(MU_CC))
MU_OUT ?= Apps/pdf/obj
MU_CFLAGS ?= -O2
MU_ONYX ?= 0

MU := $(MU_ROOT)/third_party/mupdf-1.28.5
MU_FT := $(MU_ROOT)/third_party/freetype-2.14.3
MU_JPEG := $(MU_ROOT)/third_party/jpeg-9f
MU_ZLIB := $(MU_ROOT)/third_party/zlib-1.3.1
MU_CONF := $(MU_ROOT)/user/Apps/pdf/mu

MU_DEFS := -DFZ_ENABLE_XPS=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_HTML=0 \
	-DFZ_ENABLE_FB2=0 -DFZ_ENABLE_MOBI=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_OFFICE=0 -DFZ_ENABLE_TXT=0 \
	-DFZ_ENABLE_HTML_ENGINE=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 \
	-DFZ_ENABLE_ICC=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_HYPHEN=0 -DFZ_ENABLE_MD=0 \
	-DFZ_ENABLE_SPOT_RENDERING=0 -DFZ_PLOTTERS_CMYK=0 -DFZ_PLOTTERS_N=0 \
	-DTOFU -DTOFU_CJK -DTOFU_SIL -DTOFU_EMOJI -DTOFU_HISTORIC -DTOFU_SYMBOL -DFZ_HIDE_INTERNAL_JPEG \
	-DMEMENTO_SQUEEZEBUILD=0
MU_INC := -I$(MU)/include -I$(MU_CONF) -I$(MU_ROOT)/user -I$(MU_ROOT)/user/Kits -I$(MU_ROOT)/user/Runtime -I$(MU_ROOT)/user/Include -I$(MU_ROOT)/user/Libs -I$(MU_ROOT)/kernel/include -I$(MU)/scripts/libjpeg -I$(MU_JPEG) -I$(MU_ZLIB) -I$(MU_FT)/include \
	-I$(MU)/thirdparty/jbig2dec -I$(MU)/thirdparty/openjpeg/src/lib/openjp2 \
	-DOPJ_STATIC -DOPJ_HAVE_INTTYPES_H -DOPJ_HAVE_STDINT_H -DHAVE_STDINT_H
MU_FTDEFS := -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<onyx_muftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<onyx_ftoption.h>'

MU_SRC := $(sort $(wildcard $(MU)/source/fitz/*.c)) $(sort $(wildcard $(MU)/source/pdf/*.c)) \
	$(wildcard $(MU)/generated/resources/fonts/urw/*.c)
# On Onyx (MU_ONYX=1): newlib's gaps (mu/onyx_mucompat.h, forced into every MuPDF source; mu/onyx_mucompat.c).
ifeq ($(MU_ONYX),1)
MU_SRC := $(filter-out %/directory.c,$(MU_SRC)) $(MU_CONF)/onyx_mucompat.c
MU_DEFS += -include $(MU_CONF)/onyx_mucompat.h
endif
MU_JBIG2 := $(addprefix $(MU)/thirdparty/jbig2dec/,jbig2.c jbig2_arith.c jbig2_arith_iaid.c jbig2_arith_int.c \
	jbig2_generic.c jbig2_halftone.c jbig2_huffman.c jbig2_hufftab.c jbig2_image.c jbig2_mmr.c jbig2_page.c \
	jbig2_refinement.c jbig2_segment.c jbig2_symbol_dict.c jbig2_text.c)
MU_OPJ := $(addprefix $(MU)/thirdparty/openjpeg/src/lib/openjp2/,bio.c cio.c dwt.c event.c function_list.c \
	ht_dec.c image.c invert.c j2k.c jp2.c mct.c mqc.c openjpeg.c pi.c sparse_array.c t1.c t2.c tcd.c tgt.c thread.c)
MU_JPG := $(addprefix $(MU_JPEG)/,jaricom.c jcomapi.c jdapimin.c jdapistd.c jdarith.c jdatasrc.c jdcoefct.c \
	jdcolor.c jddctmgr.c jdhuff.c jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdpostct.c jdsample.c \
	jdtrans.c jerror.c jidctflt.c jidctfst.c jidctint.c jmemmgr.c jquant1.c jquant2.c jutils.c \
	jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c jcinit.c jcmainct.c jcmarker.c \
	jcmaster.c jcparam.c jcprepct.c jcsample.c jdatadst.c jfdctflt.c jfdctfst.c jfdctint.c)
MU_Z := $(addprefix $(MU_ZLIB)/,adler32.c crc32.c deflate.c inflate.c inffast.c inftrees.c trees.c zutil.c \
	compress.c uncompr.c)
MU_FTSRC := $(addprefix $(MU_FT)/src/,base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c \
	base/ftsynth.c base/ftbbox.c base/ftglyph.c base/ftstroke.c base/fttype1.c base/ftfstype.c base/ftgasp.c \
	autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c cff/cff.c \
	cid/type1cid.c psaux/psaux.c pshinter/pshinter.c psnames/psnames.c type1/type1.c)

o = $(MU_OUT)/$(1)/$(basename $(notdir $(2))).o
MU_OBJ := $(foreach f,$(MU_SRC),$(call o,mu,$(f))) $(foreach f,$(MU_JBIG2),$(call o,jbig2,$(f))) \
	$(foreach f,$(MU_OPJ),$(call o,opj,$(f))) $(foreach f,$(MU_JPG),$(call o,jpeg,$(f))) \
	$(foreach f,$(MU_Z),$(call o,z,$(f))) $(foreach f,$(MU_FTSRC),$(call o,ft,$(f)))

$(MU_OUT)/libmupdf.a: $(MU_OBJ)
	@echo "  AR      $@"
	@$(MU_AR) rcs $@ $^

define mu_rule
$(call o,$(1),$(2)): $(2)
	@mkdir -p $$(dir $$@)
	@echo "  CC      $$(notdir $$<)"
	@$$(MU_CC) $$(MU_CFLAGS) -w $(3) -c $$< -o $$@
endef
$(foreach f,$(MU_SRC),$(eval $(call mu_rule,mu,$(f),$$(MU_DEFS) $$(MU_INC))))
$(foreach f,$(MU_JBIG2),$(eval $(call mu_rule,jbig2,$(f),-I$$(MU)/include -DHAVE_STDINT_H '-DJBIG_EXTERNAL_MEMENTO_H="mupdf/memento.h"')))
$(foreach f,$(MU_OPJ),$(eval $(call mu_rule,opj,$(f),-DOPJ_STATIC -DOPJ_HAVE_INTTYPES_H -DOPJ_HAVE_STDINT_H -DMUTEX_pthread=0 -I$$(MU)/thirdparty/openjpeg/src/lib/openjp2)))
$(foreach f,$(MU_JPG),$(eval $(call mu_rule,jpeg,$(f),-DFZ_HIDE_INTERNAL_JPEG -I$$(MU)/scripts/libjpeg -I$$(MU_JPEG))))
$(foreach f,$(MU_Z),$(eval $(call mu_rule,z,$(f),-I$$(MU_ZLIB))))
$(foreach f,$(MU_FTSRC),$(eval $(call mu_rule,ft,$(f),$$(MU_FTDEFS) -I$$(MU_CONF) -I$$(MU_ROOT)/user/Kits/fontkit -I$$(MU_FT)/include)))
