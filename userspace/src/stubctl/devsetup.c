#include "stubctl.h"

#include <SetupAPI.h>
#include <stdlib.h>

static char *
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

static BOOL
set_device_state(DWORD state, HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)
{
	SP_PROPCHANGE_PARAMS	prop_params;

	memset(&prop_params, 0, sizeof(SP_PROPCHANGE_PARAMS));

	prop_params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
	prop_params.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
	prop_params.StateChange = state;
	prop_params.Scope = DICS_FLAG_CONFIGSPECIFIC;//DICS_FLAG_GLOBAL;
	prop_params.HwProfile = 0;

	if (!SetupDiSetClassInstallParams(dev_info, dev_info_data,
					  (SP_CLASSINSTALL_HEADER *)&prop_params, sizeof(SP_PROPCHANGE_PARAMS))) {
		err("failed to set class install parameters\n");
		return FALSE;
	}

	if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, dev_info, dev_info_data)) {
		err("failed to call class installer\n");
		return FALSE;
	}

	return TRUE;
}

static BOOL
is_service_usbip_stub(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)
{
	char	*svcname;
	BOOL	res;

	svcname = get_dev_property(dev_info, dev_info_data, SPDRP_SERVICE);
	if (svcname == NULL)
		return FALSE;
	res = _stricmp(svcname, DRIVER_SVCNAME) == 0 ? TRUE: FALSE;
	free(svcname);
	return res;
}

static void
ctl_usbip_stub_devs(BOOL is_start)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	int	dev_index;

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("failed to get device info set\n");
		return;
	}

	info("%s devices..", is_start ? "starting": "stopping");

	dev_index = 0;
	while (SetupDiEnumDeviceInfo(dev_info, dev_index, &dev_info_data)) {
		if (is_service_usbip_stub(dev_info, &dev_info_data)) {
			set_device_state(is_start ? DICS_ENABLE: DICS_DISABLE, dev_info, &dev_info_data);
		}
		dev_index++;
	}

	SetupDiDestroyDeviceInfoList(dev_info);
}

static BOOL
has_str_in_multistr(const char *mz_str, const char *str, BOOL no_case)
{
	while (*mz_str) {
		int	ret;

		ret = no_case ? _stricmp(mz_str, str): strcmp(mz_str, str);
		if (ret == 0)
			return TRUE;
		mz_str += strlen(mz_str) + 1;
	}

	return FALSE;
}

static int
get_multistr_size(const char *mz_str)
{
	const char *p = mz_str;

	while (*p) {
		p += (strlen(p) + 1);
	}

	return (int)(p - mz_str) + 1;
}

static void
append_str_into_multistr(const char **pmz_str, const char *str)
{
	char	*mz_str;
	int	len_mz = get_multistr_size(*pmz_str);
	size_t	len = strlen(str);

	mz_str = realloc((void *)*pmz_str, len_mz + len + 1);
	if (mz_str == NULL)
		return;
	memcpy(mz_str + (len_mz - 1), str, len + 1);
	mz_str[len_mz - 1 + len + 1] = '\0';
	*pmz_str = mz_str;
}

static void
drop_str_from_multistr(char *mz_str, const char *str)
{
	char	*p = mz_str;
	int	len_mz = get_multistr_size(mz_str);

	while (*p) {
		if (strcmp(p, str) == 0) {
			memcpy(p, p + strlen(p) + 1, len_mz - strlen(p) - 1 - (p - mz_str));
			return;
		}
		else {
			p += (strlen(p) + 1);
		}
	}
}

static char *
get_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_UPPERFILTERS);
}

static BOOL
set_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, const char *filters)
{
	int	len;

	len = get_multistr_size(filters);
	if (len == 1) {
		filters = NULL;
		len = 0;
	}
	if (!SetupDiSetDeviceRegistryProperty(dev_info, pdev_info_data, SPDRP_UPPERFILTERS, (PBYTE)filters, len))
		return FALSE;
	return TRUE;
}

static BOOL
insert_filter(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*filters;

	filters = get_upper_filters(dev_info, pdev_info_data);
	if (filters == NULL)
		return FALSE;

	if (!has_str_in_multistr(filters, DRIVER_SVCNAME, TRUE)) {
		append_str_into_multistr(&filters, DRIVER_SVCNAME);
	}

	set_upper_filters(dev_info, pdev_info_data, filters);
	free(filters);

#if 0 ////TODO
	if (usb_registry_remove_device_regvalue(dev_info, dev_info_data, "SurpriseRemovalOK"))
	{
		if (!filter_context->class_filters)
		{
			USBMSG("restarting device %s..\n", hwid+4);
			usb_registry_restart_device(dev_info, dev_info_data);
		}
	}
#endif
	return TRUE;
}

static BOOL
remove_filter(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*filters;

	filters = get_upper_filters(dev_info, pdev_info_data);
	if (filters == NULL)
		return FALSE;

	drop_str_from_multistr(filters, DRIVER_SVCNAME);
	set_upper_filters(dev_info, pdev_info_data, filters);
	free(filters);

#if 0 ////TODO
	if (usb_registry_remove_device_regvalue(dev_info, dev_info_data, "SurpriseRemovalOK"))
	{
		if (!filter_context->class_filters)
		{
			USBMSG("restarting device %s..\n", hwid+4);
			usb_registry_restart_device(dev_info, dev_info_data);
		}
	}
#endif
	return TRUE;
}

static char *
get_id_hw(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	return get_dev_property(dev_info, pdev_info_data, SPDRP_HARDWAREID);
}

BOOL
insert_device_filter(const char *id_hw)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	int	dev_index;
	BOOL	res = FALSE;

	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("failed to get device info set");
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	dev_index = 0;
	while (SetupDiEnumDeviceInfo(dev_info, dev_index, &dev_info_data)) {
		char	*id_hw_dev;

		if (is_service_usbip_stub(dev_info, &dev_info_data)) {
			dev_index++;
			continue;
		}

		id_hw_dev = get_id_hw(dev_info, &dev_info_data);
		if (id_hw_dev != NULL && _stricmp(id_hw, id_hw_dev) == 0) {
			insert_filter(dev_info, &dev_info_data);
			free(id_hw_dev);
			SetupDiDestroyDeviceInfoList(dev_info);
			return TRUE;
		}
		free(id_hw_dev);
		dev_index++;
	}

	SetupDiDestroyDeviceInfoList(dev_info);

	return FALSE;
}

BOOL
remove_device_filter(const char *id_hw)
{
	HDEVINFO	dev_info;
	SP_DEVINFO_DATA	dev_info_data;
	int	dev_index;

	dev_info = SetupDiGetClassDevs(NULL, "USB", NULL, DIGCF_ALLCLASSES);
	if (dev_info == INVALID_HANDLE_VALUE) {
		err("failed to get device info set");
		return FALSE;
	}

	dev_info_data.cbSize = sizeof(SP_DEVINFO_DATA);
	dev_index = 0;
	while (SetupDiEnumDeviceInfo(dev_info, dev_index, &dev_info_data)) {
		char	*id_hw_dev;

		id_hw_dev = get_id_hw(dev_info, &dev_info_data);
		if (id_hw_dev != NULL && strcmp(id_hw, id_hw_dev) == 0) {
			remove_filter(dev_info, &dev_info_data);
			free(id_hw_dev);
			break;
		}
		free(id_hw_dev);
		dev_index++;
#if 0 ////DEL ////TODO
		if (usb_registry_get_hardware_id(dev_info, &dev_info_data, hwid))
		{
			if (filter_context->remove_all_device_filters)
			{
				// remove all device upper/lower filters.
				remove_device_filters = TRUE;
			}
			else if (usb_registry_match_filter_device(&filter_context->device_filters, dev_info, &dev_info_data))
			{
				// if not, remove only the ones specified by the user.
				remove_device_filters = TRUE;
			}
			else
			{
				// skip device filter removal for this device.
				remove_device_filters = FALSE;
			}

			if (remove_device_filters)
			{
				/* remove libusb as a device upper filter */
				if (usb_registry_get_property(SPDRP_UPPERFILTERS, dev_info,
					&dev_info_data,
					filters, sizeof(filters)))
				{
					if (usb_registry_mz_string_find(filters, driver_name, TRUE))
					{
						int size;
						USBMSG("removing device upper filter %s..\n", hwid+4);

						usb_registry_mz_string_remove(filters, driver_name, TRUE);
						size = usb_registry_mz_string_size(filters);

						usb_registry_set_property(SPDRP_UPPERFILTERS, dev_info,
							&dev_info_data, filters, size);

						if (!filter_context->class_filters)
						{
							USBMSG("restarting device %s..\n", hwid+4);
							usb_registry_restart_device(dev_info, &dev_info_data);
						}
					}
				}

			}
		}
#endif
	}

	SetupDiDestroyDeviceInfoList(dev_info);

	return TRUE;
}

void
start_usbip_stub_devs(void)
{
	ctl_usbip_stub_devs(TRUE);
}

void
stop_usbip_stub_devs(void)
{
	ctl_usbip_stub_devs(FALSE);
}