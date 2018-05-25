#define INITGUID

#include "usbip_common.h"
#include "usbip_windows.h"

#include <setupapi.h>
#include <stdlib.h>

#include "usbip_vhci_api.h"

static int
find_vhci_index(HDEVINFO dev_info)
{
	SP_DEVINFO_DATA	dev_info_data;
	int		idx;

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	// Loop reading information from the devices until we get some error.
	for (idx = 0;; idx++) {
		char	hardwareID[256];

		// Get device info data.
		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			DWORD	err = GetLastError();

			if (err != ERROR_NO_MORE_ITEMS) {
				err("failed to get device information: err: %d\n", err);
			}
			return -1;
		}

		// Get hardware ID.
		if (!SetupDiGetDeviceRegistryProperty(dev_info, &dev_info_data,
			SPDRP_HARDWAREID, 0L, (PBYTE)hardwareID, sizeof(hardwareID), 0L)) {
			// We got some error reading the hardware id. I'm pretty sure this isn't supposed
			// to happen, but let's continue anyway.
			continue;
		}

		// Check if we got the correct device.
		if (strcmp(hardwareID, "root\\usbip_vhci") == 0)
			return idx;
	}
}

static BOOL
get_vhci_intf(HDEVINFO dev_info, PSP_DEVICE_INTERFACE_DATA pdev_interface_data)
{
	int	idx;

	idx = find_vhci_index(dev_info);
	if (idx < 0)
		return FALSE;

	pdev_interface_data->cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
	// Get device interfaces.
	if (!SetupDiEnumDeviceInterfaces(dev_info, NULL, (LPGUID)&GUID_DEVINTERFACE_VHCI_USBIP,
		idx, pdev_interface_data)) {
		DWORD	err;

		err = GetLastError();
		// No more items here isn't supposed to happen since we checked that on the
		// SetupDiEnumDeviceInfo, but there's no harm checking again.
		if (err == ERROR_NO_MORE_ITEMS) {
			err("usbip vhci interface is not registered\n");
		}
		else {
			err("unknown error when get interface_data: err: %d\n", err);
		}
		return FALSE;
	}
	return TRUE;
}

static PSP_DEVICE_INTERFACE_DETAIL_DATA
get_vhci_intf_detail(HDEVINFO dev_info, PSP_DEVICE_INTERFACE_DATA pdev_interface_data)
{
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;
	unsigned long len = 0;
	DWORD	err;

	// Get required length for dev_interface_detail.
	SetupDiGetDeviceInterfaceDetail(dev_info, pdev_interface_data, NULL, 0, &len, NULL);
	err = GetLastError();
	if (err != ERROR_INSUFFICIENT_BUFFER) {
		err("SetupDiGetDeviceInterfaceDetail failed: err: %ld\n", err);
		return NULL;
	}

	// Allocate the required memory and set the cbSize.
	pdev_interface_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(len);
	if (pdev_interface_detail == NULL) {
		err("can't malloc %lu size memory", len);
		return NULL;
	}

	pdev_interface_detail->cbSize = sizeof(PSP_DEVICE_INTERFACE_DETAIL_DATA);

	// Try to get device details.
	if (!SetupDiGetDeviceInterfaceDetail(dev_info, pdev_interface_data,
		pdev_interface_detail, len, &len, NULL)) {
		// Errors.
		err("SetupDiGetDeviceInterfaceDetail failed: err: %ld", GetLastError());
		free(pdev_interface_detail);
		return NULL;
	}

	return pdev_interface_detail;
}

static char *
get_vhci_devpath(void)
{
	HDEVINFO	dev_info;
	SP_DEVICE_INTERFACE_DATA	dev_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA	pdev_interface_detail;
	char	*devpath;

	// Get devices info.
	dev_info = SetupDiGetClassDevs((LPGUID) &GUID_DEVINTERFACE_VHCI_USBIP, NULL, NULL,
				       DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("SetupDiGetClassDevs failed: %ld\n", GetLastError());
		return FALSE;
	}

	if (!get_vhci_intf(dev_info, &dev_interface_data)) {
		SetupDiDestroyDeviceInfoList(dev_info);
		return FALSE;
	}

	pdev_interface_detail = get_vhci_intf_detail(dev_info, &dev_interface_data);
	if (pdev_interface_detail == NULL) {
		SetupDiDestroyDeviceInfoList(dev_info);
		return FALSE;
	}

	devpath = _strdup(pdev_interface_detail->DevicePath);
	free(pdev_interface_detail);

	SetupDiDestroyDeviceInfoList(dev_info);

	return devpath;
}

HANDLE
usbip_vhci_driver_open(void)
{
	HANDLE	hdev;
	char	*devpath;

	devpath = get_vhci_devpath();
	if (devpath == NULL) {
		return INVALID_HANDLE_VALUE;
	}
	dbg("device path: %s", devpath);
	hdev = CreateFile(devpath, GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	free(devpath);
	return hdev;
}

void
usbip_vhci_driver_close(HANDLE hdev)
{
	CloseHandle(hdev);
}

static int
usbip_vhci_get_ports_status(HANDLE hdev, char *buf, int l)
{
	ioctl_usbip_vhci_get_ports_status	*st;
	unsigned long len;

	st = (ioctl_usbip_vhci_get_ports_status *)buf;

	if (l != sizeof(ioctl_usbip_vhci_get_ports_status))
		return -1;

	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_GET_PORTS_STATUS,
		NULL, 0, st, sizeof(ioctl_usbip_vhci_get_ports_status), &len, NULL)) {
		if (len == sizeof(ioctl_usbip_vhci_get_ports_status))
			return 0;
	}
	return -1;
}

int
usbip_vhci_get_free_port(HANDLE hdev)
{
	char	buf[128];
	int	i;

	if (usbip_vhci_get_ports_status(hdev, buf, sizeof(buf)))
		return -1;
	for(i = 1;i < sizeof(buf); i++) {
		if (!buf[i])
			return i;
	}
	return -1;
}

int
usbip_vhci_attach_device(HANDLE hdev, int port, struct usbip_usb_device *udev)
{
	ioctl_usbip_vhci_plugin  plugin;
	unsigned long	unused;

	plugin.devid  = ((udev->busnum << 16) | udev->devnum);
	plugin.vendor = udev->idVendor;
	plugin.product = udev->idProduct;
	plugin.version = udev->bcdDevice;
	plugin.speed = udev->speed;
	plugin.inum = udev->bNumInterfaces;
	plugin.class = udev->bDeviceClass;
	plugin.subclass = udev->bDeviceSubClass;
	plugin.protocol = udev->bDeviceProtocol;

	plugin.addr = port;

	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_PLUGIN_HARDWARE,
		&plugin, sizeof(plugin), NULL, 0, &unused, NULL))
		return 0;
	return -1;
}

int
usbip_vhci_detach_device(HANDLE hdev, int port)
{
	ioctl_usbip_vhci_unplug  unplug;
	unsigned long	unused;

	unplug.addr = port;
	if (DeviceIoControl(hdev, IOCTL_USBIP_VHCI_UNPLUG_HARDWARE,
		&unplug, sizeof(unplug), NULL, 0, &unused, NULL))
		return 0;
	return -1;
}

int
show_port_status(void)
{
	HANDLE fd;
	int i;
	char buf[128];

	fd = usbip_vhci_driver_open();
	if (INVALID_HANDLE_VALUE == fd) {
		err("open vhci driver");
		return -1;
	}
	if (usbip_vhci_get_ports_status(fd, buf, sizeof(buf))) {
		err("get port status");
		return -1;
	}
	info("max used port:%d\n", buf[0]);
	for (i = 1; i <= buf[0]; i++) {
		if (buf[i])
			info("port %d: used\n", i);
		else
			info("port %d: idle\n", i);
	}
	CloseHandle(fd);
	return 0;
}
