#
# tools/tests/netsurf/html5lib.mk -- the html5lib test drivers (html5lib_tree, html5lib_tok)
# and the parse timer (html5lib_time) built for the PC over NetSurf's HTML parser: libhubbub,
# libdom with its hubbub binding, libparserutils, libwapcaplet (the sources of the Pi build).
#
#   make -f tools/tests/netsurf/html5lib.mk OUT=<dir> [-j4]      (html5lib.sh does it)
#
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../..)
OUT  ?= /tmp/nshtml5
TP   := $(ROOT)/third_party
HERE := $(ROOT)/tools/tests/netsurf
WAP  := $(TP)/libwapcaplet
PU   := $(TP)/libparserutils
HB   := $(TP)/libhubbub
DOM  := $(TP)/libdom
CC   ?= gcc
OPT  ?= -O2
.DEFAULT_GOAL := all

CF   := $(OPT) -g -std=c99 -fcommon -w -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200112L -DNDEBUG -MMD -MP

WAP_SRC := $(shell find $(WAP)/src -name '*.c')
PU_SRC  := $(shell find $(PU)/src -name '*.c')
HB_SRC  := $(shell find $(HB)/src -name '*.c')
DOM_SRC := $(shell find $(DOM)/src -name '*.c') $(wildcard $(DOM)/bindings/hubbub/*.c)

I_WAP := -I$(WAP)/include -I$(WAP)/src
I_PU  := -I$(PU)/include -I$(PU)/src -DWITHOUT_ICONV_FILTER
I_HB  := -I$(HB)/include -I$(HB)/src -I$(PU)/include
I_DOM := -I$(DOM)/include -I$(DOM)/src -I$(WAP)/include -I$(PU)/include -I$(HB)/include

obj = $(OUT)/o/$(subst /,_,$(patsubst $(ROOT)/%,%,$(patsubst %.c,%.o,$(1))))
define RULE
$(call obj,$(1)): $(1)
	@mkdir -p $(OUT)/o
	$(CC) $(CF) $(2) -c $$< -o $$@
endef
$(foreach s,$(WAP_SRC),$(eval $(call RULE,$(s),$(I_WAP))))
$(foreach s,$(PU_SRC),$(eval $(call RULE,$(s),$(I_PU))))
$(foreach s,$(HB_SRC),$(eval $(call RULE,$(s),$(I_HB))))
$(foreach s,$(DOM_SRC),$(eval $(call RULE,$(s),$(I_DOM))))

LIBOBJ := $(foreach s,$(WAP_SRC) $(PU_SRC) $(HB_SRC) $(DOM_SRC),$(call obj,$(s)))
DRV    := tree tok time
all: $(addprefix $(OUT)/html5lib_,$(DRV))

$(OUT)/html5lib_%: $(HERE)/html5lib_%.c $(LIBOBJ)
	$(CC) $(CF) $(I_DOM) $(I_HB) -I$(HB)/src $< $(LIBOBJ) -o $@

-include $(wildcard $(OUT)/o/*.d)
.PHONY: all
