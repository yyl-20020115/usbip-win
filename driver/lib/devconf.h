#pragma once

#include <ntddk.h>
#include <usbdi.h>

typedef PUSB_CONFIGURATION_DESCRIPTOR	devconf_t;

#define NEXT_DESC(dsc)			(PUSB_COMMON_DESCRIPTOR)((PUCHAR)(dsc) + (dsc)->bLength)
#define NEXT_DESC_INTF(dsc)		(PUSB_INTERFACE_DESCRIPTOR)NEXT_DESC(dsc)
#define NEXT_DESC_EP(dsc)		(PUSB_ENDPOINT_DESCRIPTOR)NEXT_DESC(dsc)

PUSB_COMMON_DESCRIPTOR
devconf_find_desc(devconf_t devconf, unsigned int *poffset, UCHAR type);

PUSB_INTERFACE_DESCRIPTOR
devconf_find_intf_desc(devconf_t devconf, unsigned int *poffset, unsigned int num, unsigned int alternatesetting);

PUSB_ENDPOINT_DESCRIPTOR
devconf_find_ep_desc(devconf_t devconf, unsigned int intf_num, int epaddr);