#pragma once

#include "stub_dev.h"

BOOLEAN get_usb_status(usbip_stub_dev_t *devstub, USHORT op, USHORT idx, PVOID buff);
BOOLEAN get_usb_device_desc(usbip_stub_dev_t *devstub, PUSB_DEVICE_DESCRIPTOR pdesc);
BOOLEAN get_usb_desc(usbip_stub_dev_t *devstub, UCHAR descType, UCHAR idx, USHORT idLang, PVOID buff, ULONG bufflen);
BOOLEAN select_usb_conf(usbip_stub_dev_t *devstub, USHORT idx);