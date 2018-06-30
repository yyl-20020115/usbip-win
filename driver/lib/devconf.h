#pragma once

#include <ntddk.h>
#include <usbdi.h>

#define NEXT_DESC(dsc)			(PUSB_COMMON_DESCRIPTOR)((PUCHAR)(dsc) + (dsc)->bLength)
#define NEXT_DESC_INTF(dsc)		(PUSB_INTERFACE_DESCRIPTOR)NEXT_DESC(dsc)
#define NEXT_DESC_EP(dsc)		(PUSB_ENDPOINT_DESCRIPTOR)NEXT_DESC(dsc)

PUSB_INTERFACE_DESCRIPTOR
dsc_find_intf(PUSB_CONFIGURATION_DESCRIPTOR dsc_conf, PVOID start);
