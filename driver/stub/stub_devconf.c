#include "stub_driver.h"

#include "stub_dev.h"
#include "dbgcommon.h"
#include "stub_dbg.h"
#include "stub_usbd.h"
#include "devconf.h"

PUSB_CONFIGURATION_DESCRIPTOR get_usb_conf_desc(usbip_stub_dev_t *devstub, UCHAR idx);

#ifdef DBG

const char *
dbg_devconfs(devconfs_t *devconfs)
{
	static char	buf[1024];
	int	i, n = 0;

	if (devconfs == NULL)
		return "<null>";
	if (devconfs->n_devconfs == 0)
		return "empty";
	for (i = 0; i < devconfs->n_devconfs; i++) {
		devconf_t	devconf = devconfs->devconfs[i];

		if (devconf != NULL)
			n += dbg_snprintf(buf + n, 1024 - n, "[%d:%hhu,%hhu]", i, devconf->bConfigurationValue, devconf->bNumInterfaces);
		else
			n += dbg_snprintf(buf + n, 1024 - n, "[%d:null]", i);
	}
	return buf;
}

#endif

static devconf_t
get_selected_devconf(devconfs_t *devconfs)
{
	return get_devconf(devconfs, (USHORT)devconfs->selected);
}

BOOLEAN
is_iso_transfer(devconfs_t *devconfs, int ep, BOOLEAN is_in)
{
	devconf_t	devconf;
	int		epaddr;
	PUSB_ENDPOINT_DESCRIPTOR	ep_desc;

	devconf = get_selected_devconf(devconfs);
	if (devconf == NULL)
		return FALSE;
	epaddr = (is_in ? USB_ENDPOINT_DIRECTION_MASK: 0) | ep;
	ep_desc = devconf_find_ep_desc(devconf, 0, epaddr);
	if (ep_desc == NULL)
		return FALSE;
	if ((ep_desc->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_ISOCHRONOUS)
		return TRUE;
	return FALSE;
}

devconfs_t *
create_devconfs(usbip_stub_dev_t *devstub)
{
	devconfs_t	*devconfs;
	USB_DEVICE_DESCRIPTOR	DevDesc;
	int	size;
	UCHAR	i;

	if (!get_usb_device_desc(devstub, &DevDesc)) {
		DBGE(DBG_DEVCONF, "create_devconfs: cannot get device configuration\n");
		return NULL;
	}
	if (DevDesc.bNumConfigurations == 0) {
		DBGE(DBG_DEVCONF, "create_devconfs: empty configurations\n");
		return NULL;
	}
	size = sizeof(devconfs_t) + sizeof(PUSB_CONFIGURATION_DESCRIPTOR) * (DevDesc.bNumConfigurations - 1);
	devconfs = (devconfs_t *)ExAllocatePoolWithTag(NonPagedPool, size, USBIP_STUB_POOL_TAG);
	if (devconfs == NULL) {
		DBGE(DBG_DEVCONF, "create_devconfs: out of memory\n");
		return NULL;
	}

	RtlZeroMemory(devconfs, size);
	devconfs->n_devconfs = DevDesc.bNumConfigurations;
	for (i = 0; i < devconfs->n_devconfs; i++) {
		devconfs->devconfs[i] = get_usb_conf_desc(devstub, i);
		if (devconfs->devconfs[i] == NULL) {
			DBGE(DBG_DEVCONF, "process_export: out of memory(?)\n");
			free_devconfs(devconfs);
			return NULL;
		}
	}
	return devconfs;
}

void
free_devconfs(devconfs_t *devconfs)
{
	int	i;

	if (devconfs == NULL)
		return;
	for (i = 0; i < devconfs->n_devconfs; i++) {
		if (devconfs->devconfs[i] != NULL)
			ExFreePool(devconfs->devconfs[i]);
	}
	ExFreePool(devconfs);
}

devconf_t
get_devconf(devconfs_t *devconfs, USHORT idx)
{
	if (devconfs == NULL || idx == 0)
		return NULL;
	if (devconfs->n_devconfs < idx)
		return NULL;
	return devconfs->devconfs[idx - 1];
}

void
set_conf_info(devconfs_t *devconfs, USHORT idx, USBD_CONFIGURATION_HANDLE hconf, PUSBD_INTERFACE_INFORMATION intf)
{
	devconfs->selected = idx;
	///TODO
	UNREFERENCED_PARAMETER(hconf);
	UNREFERENCED_PARAMETER(intf);
}

int
get_n_intfs(devconfs_t *devconfs, USHORT idx)
{
	devconf_t	devconf = devconfs->devconfs[idx - 1];

	return devconf->bNumInterfaces;
}

int
get_n_endpoints(devconfs_t *devconfs, USHORT idx)
{
	devconf_t	devconf = devconfs->devconfs[idx - 1];
	int	n_eps = 0;
	unsigned int	offset = 0;

	while (TRUE) {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;
		intf_desc = (PUSB_INTERFACE_DESCRIPTOR)devconf_find_desc(devconf, &offset, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (intf_desc == NULL)
			break;
		n_eps += intf_desc->bNumEndpoints;
	}
	return n_eps;
}