/*
 *
 * Copyright (C) 2005-2007 Takahiro Hirofuchi
 */

#define INITGUID

#include "usbip_windows.h"

#include "usbip_common.h"
#include "usbip_stub_api.h"
#include "usbip_setupdi.h"

#include <winsock2.h>
#include <stdlib.h>

void enter_refresh_edevs(void);
void leave_refresh_edevs(void);
void cleanup_edevs(void);
void add_edev(devno_t devno, const char *devpath, unsigned short vendor, unsigned short product);

typedef struct {
	const char	*id_inst;
	char	*devpath;
} devpath_ctx_t;

static int
walker_devpath(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devpath_ctx_t	*pctx = (devpath_ctx_t *)ctx;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdetail;
	char	*id_inst;

	id_inst = get_id_inst(dev_info, pdev_info_data);
	if (id_inst == NULL)
		return 0;
	if (strcmp(id_inst, pctx->id_inst) != 0) {
		free(id_inst);
		return 0;
	}
	free(id_inst);

	pdetail = get_intf_detail(dev_info, pdev_info_data, &GUID_DEVINTERFACE_STUB_USBIP);
	if (pdetail == NULL) {
		return 0;
	}
	pctx->devpath = _strdup(pdetail->DevicePath);
	free(pdetail);
	return -100;
}

static char *
get_device_path(const char *id_inst)
{
	devpath_ctx_t	devpath_ctx;

	devpath_ctx.id_inst = id_inst;
	if (traverse_intfdevs(walker_devpath, &GUID_DEVINTERFACE_STUB_USBIP, &devpath_ctx) == -100)
		return devpath_ctx.devpath;
	return NULL;
}

static int
walker_refresh(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	char	*id_inst, *devpath, *id_hw;
	unsigned short	vendor, product;

	id_inst = get_id_inst(dev_info, pdev_info_data);
	if (id_inst == NULL) {
		err("failed to get instance id");
		return 0;
	}
	devpath = get_device_path(id_inst);
	free(id_inst);
	if (devpath == NULL) {
		/* skip non-usbip stub device */
		return 0;
	}

	id_hw = get_id_hw(dev_info, pdev_info_data);
	if (!get_usbdev_info(id_hw, &vendor, &product)) {
		err("failed to get hw id: %s\n", id_hw ? id_hw: "");
		if (id_hw)
			free(id_hw);
		free(devpath);
		return 0;
	}
	free(id_hw);
	add_edev(devno, devpath, vendor, product);
	free(devpath);
	return 0;
}

int
usbipd_refresh_edevs(void)
{
	enter_refresh_edevs();
	cleanup_edevs();
	traverse_usbdevs(walker_refresh, TRUE, NULL);
	leave_refresh_edevs();
	return 0;
}

BOOL
usbip_export_device(struct usbip_exportable_device *edev, SOCKET sockfd)
{
	return TRUE;
}