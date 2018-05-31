#pragma once

#include <guiddef.h>
#ifdef _NTDDK_
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

// {FB265267-C609-41E6-8ECA-A20D92A833E6}
DEFINE_GUID(GUID_DEVINTERFACE_STUB_USBIP,
	0xfb265267, 0xc609, 0x41e6, 0x8e, 0xca, 0xa2, 0xd, 0x92, 0xa8, 0x33, 0xe6);