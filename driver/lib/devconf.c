#include "devconf.h"

PUSB_COMMON_DESCRIPTOR
devconf_find_desc(devconf_t devconf, unsigned int *poffset, UCHAR type)
{
	unsigned int	offset = *poffset;

	do {
		PUSB_COMMON_DESCRIPTOR	desc;

		if (devconf->wTotalLength <= offset + sizeof(USB_COMMON_DESCRIPTOR))
			return NULL;

		desc = (PUSB_COMMON_DESCRIPTOR)((PUINT8)devconf + offset);
		if (devconf->wTotalLength < offset + desc->bLength)
			return NULL;

		offset += desc->bLength;
		if (desc->bDescriptorType == type) {
			*poffset = offset;
			return desc;
		}
	} while (TRUE);
}

PUSB_INTERFACE_DESCRIPTOR
devconf_find_intf_desc(devconf_t devconf, unsigned int *poffset, unsigned int num, unsigned int alternatesetting)
{
	do {
		PUSB_INTERFACE_DESCRIPTOR	intf_desc;

		intf_desc = (PUSB_INTERFACE_DESCRIPTOR)devconf_find_desc(devconf, poffset, USB_INTERFACE_DESCRIPTOR_TYPE);
		if (intf_desc == NULL)
			break;
		if (intf_desc->bInterfaceNumber < num)
			continue;
		if (intf_desc->bInterfaceNumber > num)
			break;
		if (intf_desc->bAlternateSetting < alternatesetting)
			continue;
		if (intf_desc->bAlternateSetting > alternatesetting)
			break;
		return intf_desc;
	} while (1);

	return NULL;
}

PUSB_ENDPOINT_DESCRIPTOR
devconf_find_ep_desc(devconf_t devconf, unsigned int intf_num, int epaddr)
{
	PUSB_INTERFACE_DESCRIPTOR	intf_desc;
	unsigned int	offset = 0;
	int	i;

	intf_desc = devconf_find_intf_desc(devconf, &offset, intf_num, 0);
	if (intf_desc == NULL)
		return NULL;
	for (i = 0; i < intf_desc->bNumEndpoints; i++) {
		PUSB_ENDPOINT_DESCRIPTOR	ep_desc;

		ep_desc = (PUSB_ENDPOINT_DESCRIPTOR)devconf_find_desc(devconf, &offset, USB_ENDPOINT_DESCRIPTOR_TYPE);
		if (ep_desc == NULL)
			return NULL;
		if (ep_desc->bEndpointAddress == epaddr)
			return ep_desc;
	}
	return NULL;
}