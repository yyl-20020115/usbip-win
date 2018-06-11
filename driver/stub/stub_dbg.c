#include "stub_driver.h"
#include "stub_dev.h"

#include <ntstrsafe.h>
#include "dbgcode.h"
#include "usbip_stub_api.h"

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

const char *
dbg_devstub_confdescs(usbip_stub_dev_t *devstub)
{
	static char	buf[1024];
	int	i, n = 0;

	if (devstub == NULL)
		return "<null>";
	if (devstub->n_conf_descs == 0)
		return "empty";
	for (i = 0; i < devstub->n_conf_descs; i++) {
		size_t	len;

		PUSB_CONFIGURATION_DESCRIPTOR	conf_desc = devstub->conf_descs[i];
		if (conf_desc != NULL)
			RtlStringCchPrintfA(buf + n, 1024 - n, "[%d:%hhu,%hhu]", i, conf_desc->bConfigurationValue, conf_desc->bNumInterfaces);
		else
			RtlStringCchPrintfA(buf + n, 1024 - n, "[%d:null]", i);
		RtlStringCchLengthA(buf + n, 1024 - n, &len);
		n += (int)len;
	}
	return buf;
}

static namecode_t	namecodes_stub_ioctl[] = {
	K_V(IOCTL_USBIP_STUB_GET_DEVINFO)
	K_V(IOCTL_USBIP_STUB_EXPORT)
	{0,0}
};

const char *
dbg_stub_ioctl_code(unsigned int ioctl_code)
{
	return dbg_namecode(namecodes_stub_ioctl, "ioctl", ioctl_code);
}

#endif
