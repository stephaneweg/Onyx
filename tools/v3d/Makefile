# tools/v3d: the V3D control-list packets for the kernel, generated from Mesa's v3d_packet.xml
# (genpackets.py): kern/v3d_pack42.h (V3D 4.2, the Pi 4) and v3d_pack71.h (V3D 7.1, the Pi 5). The headers
# are committed: the kernel build does not need Python. tools/tests/run_v3d_cl_test.sh checks them.
INC = ../../kernel/include/kern

all: $(INC)/v3d_pack42.h $(INC)/v3d_pack71.h

$(INC)/v3d_pack%.h: genpackets.py v3d_packet.xml
	python3 genpackets.py $* > $@
