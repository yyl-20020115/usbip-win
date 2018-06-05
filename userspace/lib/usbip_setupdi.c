#include "usbip_common.h"
#include "usbip_windows.h"

#include <setupapi.h>
#include <stdlib.h>

#include "usbip_setupdi.h"

char *
get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop)
{
	char	*value;
	DWORD	length;

	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, NULL, NULL, 0, &length)) {
		DWORD	err = GetLastError();
		switch (err) {
		case ERROR_INVALID_DATA:
			return _strdup("");
		case ERROR_INSUFFICIENT_BUFFER:
			break;
		default:
			err("get_dev_property: failed to get device property: err: %x", err);
			return NULL;
		}
	}
	else {
		err("get_dev_property: unexpected case");
		return NULL;
	}
	value = malloc(length);
	if (value == NULL) {
		err("get_dev_property: out of memory");
		return NULL;
	}
	if (!SetupDiGetDeviceRegistryProperty(dev_info, pdev_info_data, prop, NULL, (PBYTE)value, length, &length)) {
		err("get_dev_property: failed to get device property: err: %x", GetLastError());
		free(value);
		return NULL;
	}
	return value;
}

char *
get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_HARDWAREID);
}

char *
get_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_UPPERFILTERS);
}

char *
get_id_inst(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*id_inst;
	DWORD	length;

	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, NULL, 0, &length)) {
		DWORD	err = GetLastError();
		if (err != ERROR_INSUFFICIENT_BUFFER) {
			err("get_id_inst: failed to get instance id: err: %x", err);
			return NULL;
		}
	}
	else {
		err("get_id_inst: unexpected case");
		return NULL;
	}
	id_inst = (char *)malloc(length);
	if (id_inst == NULL) {
		err("get_id_inst: out of memory");
		return NULL;
	}
	if (!SetupDiGetDeviceInstanceId(dev_info, pdev_info_data, id_inst, length, NULL)) {
		err("failed to get instance id\n");
		free(id_inst);
		return NULL;
	}
	return id_inst;
}

static unsigned char
get_devno_from_inst_id(unsigned char devno_map[], const char *id_inst)
{
	unsigned char	devno = 0;
	int	ndevs;
	int	i;

	for (i = 0; id_inst[i]; i++) {
		devno += (unsigned char)(id_inst[i] * 19 + 13);
	}
	if (devno == 0)
		devno++;

	ndevs = 0;
	while (devno_map[devno - 1]) {
		if (devno == 255)
			devno = 1;
		else
			devno++;
		if (ndevs == 255) {
			/* devno map is full */
			return 0;
		}
		ndevs++;
	}
	devno_map[devno - 1] = 1;
	return devno;
}

int
traverse_usbdevs(walkfunc_t walker, BOOL present_only, void *ctx)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	DWORD	flags = DIGCF_ALLCLASSES;
	unsigned char	devno_map[255];
	int	ret = 0;
	int	idx;

	if (present_only)
		flags |= DIGCF_PRESENT;
	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, flags);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("SetupDiGetClassDevs failed: 0x%lx\n", GetLastError());
		return -1;
	}

	memset(devno_map, 0, 255);
	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	for (idx = 0;; idx++) {
		char	*id_inst;
		devno_t	devno;

		if (!SetupDiEnumDeviceInfo(dev_info, idx, &dev_info_data)) {
			DWORD	err = GetLastError();

			if (err != ERROR_NO_MORE_ITEMS) {
				err("failed to get device information: err: %d\n", err);
			}
			break;
		}
		id_inst = get_id_inst(dev_info, &dev_info_data);
		if (id_inst == NULL)
			continue;
		devno = get_devno_from_inst_id(devno_map, id_inst);
		free(id_inst);
		if (devno == 0)
			continue;
		ret = walker(dev_info, &dev_info_data, devno, ctx);
		if (ret != 0)
			break;
	}

	SetupDiDestroyDeviceInfoList(dev_info);
	return ret;
}