#define INITGUID

#include "usbip_common.h"
#include "usbip_windows.h"

#include <setupapi.h>

#include "usbipenum_api.h"

static char *usbip_vbus_dev_node_name(char *buf, unsigned long buf_len)
{
	HDEVINFO dev_info;
	SP_DEVICE_INTERFACE_DATA dev_interface_data;
	PSP_DEVICE_INTERFACE_DETAIL_DATA dev_interface_detail = NULL;
	SP_DEVINFO_DATA dev_info_data;
	unsigned long len;
	char *ret=NULL;
	int memberIndex = 0, rc = 1;
	char hardwareID[256] = {0};

	// Get devices info.
	dev_info = SetupDiGetClassDevs(
		(LPGUID) &GUID_DEVINTERFACE_BUSENUM_USBIP, /* ClassGuid */
		NULL,	/* Enumerator */
		NULL,	/* hwndParent */
		DIGCF_PRESENT|DIGCF_DEVICEINTERFACE /* Flags */
	);
	if (INVALID_HANDLE_VALUE == dev_info) {
		err("SetupDiGetClassDevs failed: %ld\n", GetLastError());
		return NULL;
	}

	// Prepare some structures.
	dev_interface_data.cbSize = sizeof (dev_interface_data);
	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);

	// Loop reading information from the devices until we get some error.
	while (rc) {
		// Get device info data.
		rc = SetupDiEnumDeviceInfo(
			dev_info,		/* DeviceInfoSet */
			memberIndex,	/* MemberIndex */
			&dev_info_data	/* DeviceInfoData */
		);
		if (!rc) {
			if (GetLastError() == ERROR_NO_MORE_ITEMS) {
				// No more items. Leave.
				memberIndex = -1;
				break;
			} else {
				// Something else...
				err("error getting device information\n");
				goto end;
			}
		}

		// Get hardware ID.
		rc = SetupDiGetDeviceRegistryProperty(
			dev_info,			/* DeviceInfoSet */
			&dev_info_data,		/* DeviceInfoData */
			SPDRP_HARDWAREID,	/* Property */
			0L,					/* PropertyRegDataType */
			(PBYTE)hardwareID,	/* PropertyBuffer */
			sizeof(hardwareID),	/* PropertyBufferSize */
			0L					/* RequiredSize */
		);
		if (!rc) {
			// We got some error reading the hardware id. I'm pretty sure this isn't supposed
			// to happen, but let's continue anyway.
			memberIndex++;
			continue;
		}

		// Check if we got the correct device.
		if (strcmp(hardwareID, "root\\usbipenum") != 0) {
			// Wrong hardware ID. Get the next one.
			memberIndex++;
			continue;
		} else {
			// Got it!
			break;
		}
	}

	// Get device interfaces.
	rc = SetupDiEnumDeviceInterfaces(
		dev_info,									/* DeviceInfoSet */
		NULL,										/* DeviceInfoData */
		(LPGUID) &GUID_DEVINTERFACE_BUSENUM_USBIP,	/* InterfaceClassGuid */
		memberIndex,								/* MemberIndex */
		&dev_interface_data							/* DeviceInterfaceData */
	);
	if (!rc) {
		// No more items here isn't supposed to happen since we checked that on the
		// SetupDiEnumDeviceInfo, but there's no harm checking again.
		if (ERROR_NO_MORE_ITEMS == GetLastError()) {
			err("usbvbus interface is not registered\n");
		} else {
			err("unknown error when get interface_data\n");
		}
		goto end;
	}

	// Get required length for dev_interface_detail.
	rc = SetupDiGetDeviceInterfaceDetail(
		dev_info,				/* DeviceInfoSet */
		&dev_interface_data,	/* DeviceInterfaceData */
		NULL,					/* DeviceInterfaceDetailData */
		0,						/* DeviceInterfaceDetailDataSize */
		&len,					/* RequiredSize */
		NULL					/* DeviceInfoData */
	);
	if (ERROR_INSUFFICIENT_BUFFER != GetLastError()) {
		err("Error in SetupDiGetDeviceInterfaceDetail: %ld\n", GetLastError());
		goto end;
	}

	// Allocate the required memory and set the cbSize.
	dev_interface_detail = (PSP_DEVICE_INTERFACE_DETAIL_DATA)malloc(len);
	if (NULL == dev_interface_detail) {
		err("can't malloc %lu size memoery", len);
		goto end;
	}
	dev_interface_detail->cbSize = sizeof (*dev_interface_detail);

	// Try to get device details.
	rc = SetupDiGetDeviceInterfaceDetail(
		dev_info,				/* DeviceInfoSet */
		&dev_interface_data,	/* DeviceInterfaceData */
		dev_interface_detail,	/* DeviceInterfaceDetailData */
		len,					/* DeviceInterfaceDetailDataSize */
		&len,					/* RequiredSize */
		NULL					/* DeviceInfoData */
	);
	if (!rc) {
		// Errors.
		err("Error in SetupDiGetDeviceInterfaceDetail\n");
		goto end;
	}

	// Copy the device path to the buffer.
	len = _snprintf_s(buf, buf_len, buf_len, "%s", dev_interface_detail->DevicePath);
	if (len>=buf_len) {
		goto end;
	}
	ret = buf;

end:
	// Free the detail memory and destroy the device information list.
	if (dev_interface_detail) {
		free(dev_interface_detail);
	}
	SetupDiDestroyDeviceInfoList(dev_info);

	return ret;
}

HANDLE usbip_vhci_driver_open(void)
{
	char buf[256];
	if(NULL==usbip_vbus_dev_node_name(buf, sizeof(buf)))
		return INVALID_HANDLE_VALUE;
	return	CreateFile(buf,
			GENERIC_READ|GENERIC_WRITE,
			  0,
			  NULL,
			  OPEN_EXISTING,
			  FILE_FLAG_OVERLAPPED,
			  NULL);
}

void
usbip_vhci_driver_close(HANDLE hdev)
{
	CloseHandle(hdev);
}

static int usbip_vhci_get_ports_status(HANDLE fd, char *buf, int l)
{
	int ret;
	unsigned long len;
	ioctl_usbvbus_get_ports_status * st=(ioctl_usbvbus_get_ports_status *)buf;

	if(l!=sizeof(*st))
		return -1;

	ret = DeviceIoControl(fd, IOCTL_USBVBUS_GET_PORTS_STATUS,
				NULL, 0, st, sizeof(*st), &len, NULL);
	if(ret&&len==sizeof(*st))
		return 0;
	else
		return -1;
}

int usbip_vhci_get_free_port(HANDLE fd)
{
	int i;
	char buf[128];
	if(usbip_vhci_get_ports_status(fd, buf, sizeof(buf)))
		return -1;
	for(i=1;i<sizeof(buf);i++){
		if(!buf[i])
			return i;
	}
	return -1;
}

int usbip_vhci_detach_device(HANDLE fd, int port)
{
	int ret;
	ioctl_usbvbus_unplug  unplug;
	unsigned long unused;

	unplug.addr = port;
	ret = DeviceIoControl(fd, IOCTL_USBVBUS_UNPLUG_HARDWARE,
				&unplug, sizeof(unplug), NULL, 0, &unused, NULL);
	if(ret)
		return 0;
	return -1;
}

int usbip_vhci_attach_device(HANDLE fd, int port, struct usbip_usb_device *udev)
{
	int ret;
	ioctl_usbvbus_plugin  plugin;
	unsigned long unused;

	plugin.devid  = ((udev->busnum << 16)|udev->devnum);
	plugin.vendor = udev->idVendor;
	plugin.product = udev->idProduct;
	plugin.version = udev->bcdDevice;
	plugin.speed = udev->speed;
	plugin.inum = udev->bNumInterfaces;
	plugin.class = udev->bDeviceClass;
	plugin.subclass = udev->bDeviceSubClass;
	plugin.protocol = udev->bDeviceProtocol;

	plugin.addr = port;

	ret = DeviceIoControl(fd, IOCTL_USBVBUS_PLUGIN_HARDWARE,
				&plugin, sizeof(plugin), NULL, 0, &unused, NULL);
	if(ret)
		return 0;
	return -1;
}

int show_port_status(void)
{
	HANDLE fd;
	int i;
	char buf[128];

	fd = usbip_vhci_driver_open();
	if (INVALID_HANDLE_VALUE == fd) {
		err("open vbus driver");
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
