#include "devconf.h"

#include <usbdlib.h>

PUSB_INTERFACE_DESCRIPTOR
dsc_find_intf(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PVOID start)
{
	return (PUSB_INTERFACE_DESCRIPTOR)USBD_ParseDescriptors(dsc_conf, dsc_conf->wTotalLength, start, USB_INTERFACE_DESCRIPTOR_TYPE);
}
