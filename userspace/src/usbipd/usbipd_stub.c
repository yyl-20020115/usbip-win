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

static BOOL
get_device_desc(const char *devpath, ioctl_usbip_stub_getdesc_t *getdesc)
{
	HANDLE	hdev;
	DWORD	len;

	hdev = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (hdev == INVALID_HANDLE_VALUE) {
		err("cannot open device: %s", devpath);
		return FALSE;
	}
	if (!DeviceIoControl(hdev, IOCTL_USBIP_STUB_GET_DESC, NULL, 0, getdesc, sizeof(ioctl_usbip_stub_getdesc_t), &len, NULL)) {
		err("DeviceIoControl failed: err: 0x%lx", GetLastError());
		CloseHandle(hdev);
		return FALSE;
	}
	CloseHandle(hdev);

	if (len != sizeof(ioctl_usbip_stub_getdesc_t)) {
		err("DeviceIoControl failed: invalid size: len: %d", len);
		return FALSE;
	}

	return TRUE;
}

typedef struct {
	devno_t	devno;
	char	*id_hw, *id_inst;
} get_id_inst_ctx_t;

static int
walker_get_id_inst(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	get_id_inst_ctx_t	*pctx = (get_id_inst_ctx_t *)ctx;

	if (devno == pctx->devno) {
		pctx->id_hw = get_id_hw(dev_info, pdev_info_data);
		pctx->id_inst = get_id_inst(dev_info, pdev_info_data);
		return -100;
	}
	return 0;
}

static BOOL
get_id_from_devno(devno_t devno, char **pid_hw, char **pid_inst)
{
	get_id_inst_ctx_t	ctx;

	ctx.devno = devno;
	if (traverse_usbdevs(walker_get_id_inst, TRUE, &ctx) == -100) {
		if (ctx.id_hw && ctx.id_inst) {
			*pid_hw = ctx.id_hw;
			*pid_inst = ctx.id_inst;
			return TRUE;
		}
		if (ctx.id_hw)
			free(ctx.id_hw);
		if (ctx.id_inst)
			free(ctx.id_inst);
	}
	return FALSE;
}

BOOL
build_udev(devno_t devno, struct usbip_usb_device *pudev)
{
	char	*id_hw, *id_inst;
	char	*devpath;
	unsigned short	vendor, product;
	ioctl_usbip_stub_getdesc_t	Getdesc;

	if (!get_id_from_devno(devno, &id_hw, &id_inst)) {
		err("build_udev: invalid devno: %hhu", devno);
		return FALSE;
	}
	devpath = get_device_path(id_inst);
	free(id_inst);

	if (devpath == NULL) {
		err("build_udev: invalid device path: id_inst: %s", id_inst);
		free(id_hw);
		return FALSE;
	}

	memset(pudev, 0, sizeof(struct usbip_usb_device));

	pudev->busnum = 1;
	pudev->devnum = (int)devno;
	snprintf(pudev->path, USBIP_DEV_PATH_MAX, devpath);
	snprintf(pudev->busid, USBIP_BUS_ID_SIZE, "1-%hhu", devno);

	if (get_usbdev_info(id_hw, &vendor, &product)) {
		pudev->idVendor = vendor;
		pudev->idProduct = product;
	}
	else {
		err("failed to get vendor, product: hw id: %s\n", id_hw);
	}
	free(id_hw);

	if (get_device_desc(devpath, &Getdesc)) {
		pudev->bDeviceClass = Getdesc.class;
		pudev->bDeviceSubClass = Getdesc.subclass;
		pudev->bDeviceProtocol = Getdesc.protocol;
	}
	free(devpath);

	return TRUE;
}

HANDLE
open_stub_dev(devno_t devno)
{
	HANDLE	hdev;
	char	*id_inst, *id_hw;
	char	*devpath;

	if (!get_id_from_devno(devno, &id_hw, &id_inst))
		return INVALID_HANDLE_VALUE;
	free(id_hw);

	devpath = get_device_path(id_inst);
	free(id_inst);
	if (devpath == NULL)
		return INVALID_HANDLE_VALUE;

	hdev = CreateFile(devpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	free(devpath);

	return hdev;
}