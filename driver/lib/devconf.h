#pragma once

#include <ntddk.h>
#include <usbdi.h>

typedef PUSB_CONFIGURATION_DESCRIPTOR	devconf_t;

#define DEVCONF_NEXT_DESC(pdesc)	 (PUSB_COMMON_DESCRIPTOR)((PUCHAR)(pdesc) + (pdesc)->bLength)

#define DEVCONF_NEXT_EP_DESC(pdesc)	 (PUSB_ENDPOINT_DESCRIPTOR)DEVCONF_NEXT_DESC(pdesc)

PUSB_COMMON_DESCRIPTOR
devconf_find_desc(devconf_t devconf, unsigned int *poffset, UCHAR type);

PUSB_INTERFACE_DESCRIPTOR
devconf_find_intf_desc(devconf_t devconf, unsigned int *poffset, unsigned int num, unsigned int alternatesetting);

PUSB_ENDPOINT_DESCRIPTOR
devconf_find_ep_desc(devconf_t devconf, unsigned int intf_num, int epaddr);