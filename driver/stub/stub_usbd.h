#pragma once

#include "stub_dev.h"

BOOLEAN get_usb_status(usbip_stub_dev_t *devstub, USHORT op, USHORT idx, PVOID buff, PUCHAR plen);
BOOLEAN get_usb_device_desc(usbip_stub_dev_t *devstub, PUSB_DEVICE_DESCRIPTOR pdesc);
BOOLEAN get_usb_desc(usbip_stub_dev_t *devstub, UCHAR descType, UCHAR idx, USHORT idLang, PVOID buff, ULONG *pbufflen);
BOOLEAN select_usb_conf(usbip_stub_dev_t *devstub, USHORT idx);

BOOLEAN submit_class_vendor_req(usbip_stub_dev_t *devstub, BOOLEAN is_in, USHORT cmd,
	UCHAR rv, UCHAR request, USHORT value, USHORT index, PVOID data, ULONG len);

BOOLEAN
submit_bulk_transfer(usbip_stub_dev_t *devstub, USBD_PIPE_HANDLE hPipe, PVOID data, USHORT datalen, BOOLEAN is_in);