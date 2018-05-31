#include "stub_driver.h"
#include "stub_dev.h"

#include <ntstrsafe.h>

#ifdef DBG

const char *
dbg_device(PDEVICE_OBJECT devobj)
{
	static char	buf[32];
	ANSI_STRING	name;

	if (devobj == NULL)
		return "null";
	if (devobj->DriverObject)
		return "driver null";
	if (NT_SUCCESS(RtlUnicodeStringToAnsiString(&name, &devobj->DriverObject->DriverName, TRUE))) {
		RtlStringCchCopyA(buf, 32, name.Buffer);
		RtlFreeAnsiString(&name);
		return buf;
	}
	else {
		return "error";
	}
}

const char *
dbg_devices(PDEVICE_OBJECT devobj, BOOLEAN is_attached)
{
	static char	buf[1024];
	int	n = 0;
	int	i;

	for (i = 0; i < 16; i++) {
		size_t	len;

		if (devobj == NULL)
			break;
		RtlStringCchPrintfA(buf + n, 1024 - n, "[%s]", dbg_device(devobj));
		RtlStringCchLengthA(buf + n, 1024 - n, &len);
		n += (int)len;
		if (is_attached)
			devobj = devobj->AttachedDevice;
		else
			devobj = devobj->NextDevice;
	}
	return buf;
}

const char *
dbg_devstub(usbip_stub_dev_t *devstub)
{
	static char	buf[512];

	if (devstub == NULL)
		return "<null>";
	RtlStringCchPrintfA(buf, 512, "id:%d,hw:%s", devstub->id, devstub->id_hw);
	return buf;
}

#endif
