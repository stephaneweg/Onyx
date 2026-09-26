#ifndef _circle_devicenameservice_h
#define _circle_devicenameservice_h
#include <circle/device.h>
#include <string.h>
class CDeviceNameService
{
public:
	static CDeviceNameService *Get (void) { static CDeviceNameService s; return &s; }
	CDevice *GetDevice (const char *pName, boolean) { return strcmp (pName, "emmc1") == 0 ? s_pDisk : 0; }
	static CDevice *s_pDisk;
};
#endif
