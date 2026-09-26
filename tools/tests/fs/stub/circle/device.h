// host stub of Circle's CDevice: just what addon/fatfs/diskio.cpp uses
#ifndef _circle_device_h
#define _circle_device_h
#include <circle/types.h>
#define DEVICE_IOCTL_SYNC 0
class CDevice;
typedef void TDeviceRemovedHandler (CDevice *pDevice, void *pContext);
class CDevice
{
public:
	virtual ~CDevice (void) {}
	virtual int Read (void *pBuffer, size_t nCount) = 0;
	virtual int Write (const void *pBuffer, size_t nCount) = 0;
	virtual u64 Seek (u64 ullOffset) = 0;
	virtual u64 GetSize (void) const = 0;
	virtual int IOCtl (unsigned long, void *) { return -1; }
	virtual boolean RemoveDevice (void) { return TRUE; }
	void *RegisterRemovedHandler (TDeviceRemovedHandler *pHandler, void *pContext) { m_pHandler = pHandler; m_pContext = pContext; return 0; }
	void Remove (void) { if (m_pHandler) (*m_pHandler) (this, m_pContext); }
private:
	TDeviceRemovedHandler *m_pHandler = 0; void *m_pContext = 0;
};
#endif
