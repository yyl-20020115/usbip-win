/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_host.h"

 /* External API to access the driver */
int
usbip_driver_open(void)
{
#if 0 ///TODO
	if (!hdriver->ops.open)
		return -EOPNOTSUPP;
	return hdriver->ops.open(hdriver);
#endif
	return 0;
}

void
usbip_driver_close(void)
{
#if 0 ////TODO
	if (!hdriver->ops.close)
		return;
	hdriver->ops.close(hdriver);
#endif
}

int
usbip_refresh_device_list(void)
{
#if 0 ////TODO
	if (!hdriver->ops.refresh_device_list)
		return -EOPNOTSUPP;
	return hdriver->ops.refresh_device_list(hdriver);
#endif
	return 0;
}

struct usbip_exported_device *
usbip_get_device(struct usbip_host_driver *hdriver, int num)
{
#if 0 ////TODO
	if (!hdriver->ops.get_device)
		return NULL;
	return hdriver->ops.get_device(hdriver, num);
#endif
	return 0;
}

BOOL
usbip_export_device(struct usbip_exported_device *edev, SOCKET sockfd)
{
	return TRUE;
}