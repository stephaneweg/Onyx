// host stub of <fatfs/ff.h> (tools/tests/run_image_test.sh): the volumes' names, as the Circle
// fork's addon/fatfs/ffconf.h defines them (SD: the card's first FAT volume, SD1..SD3: its
// partitions 2..4). Keep in step with it.
#ifndef _fatfs_ff_h
#define _fatfs_ff_h
#define FF_VOLUME_STRS		"SD","SD1","SD2","SD3","USB","USB2","USB3","FD","NVME"
#endif
