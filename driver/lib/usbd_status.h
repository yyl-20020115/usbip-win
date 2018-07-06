#pragma once

#include <ntddk.h>
#include <usb.h>

USBD_STATUS to_usbd_status(int usbip_status);
int to_usbip_status(USBD_STATUS usbd_status);