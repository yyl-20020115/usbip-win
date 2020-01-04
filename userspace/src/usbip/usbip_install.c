
#include "usbip.h"
#include "usbip_common.h"

#include <windows.h>
#include <setupapi.h>
#include <newdev.h>

#include <shlwapi.h>

#include <string.h>
#include <assert.h>
#include <stdio.h>

#define BUFFER_SIZE 256
#define STR_BUFFER_SIZE 32

struct usbip_install_devinfo_struct {
	char inf_filename[STR_BUFFER_SIZE];
	char hwid_inf_section[STR_BUFFER_SIZE];
	char hwid_inf_key[STR_BUFFER_SIZE];
	char devdesc_inf_section[STR_BUFFER_SIZE];  // Section in INF file in which the Device desc. is located
	char devdesc_inf_key[STR_BUFFER_SIZE];      // Device Description / Device Name
	char dev_instance_path[STR_BUFFER_SIZE];    // Device instance Path - needed for reinstall feature
};

enum {
	DEVICE_LIST_VHCI_DRIVER = 0,
};

static struct usbip_install_devinfo_struct device_list[] = {
	{
		.inf_filename        = "usbip_vhci.inf",
		.hwid_inf_section    = "Standard.NTamd64",
		.hwid_inf_key        = "USB/IP VHCI",
		.devdesc_inf_section = "Strings",
		.devdesc_inf_key     = "DeviceDesc",
		.dev_instance_path   = "ROOT\\USBIP\\0000"
	}
};

static const char usbip_install_usage_string[] =
"usbip install\n"
"    install or reinstall usbip VHCI driver\n";

void usbip_install_usage(void)
{
	printf("usage: %s", usbip_install_usage_string);
}

static BOOL usbip_install_get_inf_path(char *inf_path_buffer, DWORD buffer_size,
		const struct usbip_install_devinfo_struct *dev_data) {
	HRESULT result = GetModuleFileName(NULL, inf_path_buffer, buffer_size - 1);
	if (!result) {
		return FALSE;
	}
	result = PathRemoveFileSpec(inf_path_buffer);
	if (!result) {
		return FALSE;
	}
	result = PathAppend(inf_path_buffer, dev_data->inf_filename);
	if (!result) {
		return FALSE;
	}
	return TRUE;
}

static BOOL usbip_install_get_class_id(GUID *class_guid, const char *inf_path)
{
	char buffer[BUFFER_SIZE] = { 0 };
	const BOOL class_ok = SetupDiGetINFClass(inf_path,
		class_guid, buffer, BUFFER_SIZE, NULL);
	if (!class_ok) {
		return FALSE;
	}
	return TRUE;
}

static BOOL usbip_install_get_device_description(char *device_desc_buffer, DWORD buffer_size,
		HINF inf_handle,
		const struct usbip_install_devinfo_struct *dev_data)
{
	const BOOL ok = SetupGetLineText(NULL,
		inf_handle,
		dev_data->devdesc_inf_section,
		dev_data->devdesc_inf_key,
		device_desc_buffer, buffer_size, NULL);
	if (!ok) {
		return FALSE;
	}
	return TRUE;
}

static BOOL usbip_install_get_hardware_id(char *buffer, DWORD buffer_size,
	HINF inf_handle, const struct usbip_install_devinfo_struct *dev_data)
{
	assert(buffer != NULL);
	assert(buffer_size > 0);
	assert(inf_handle != INVALID_HANDLE_VALUE);

	char device_info[BUFFER_SIZE] = { 0 };
	const BOOL ok = SetupGetLineText(NULL,
		inf_handle,
		dev_data->hwid_inf_section,
		dev_data->hwid_inf_key,
		buffer, BUFFER_SIZE, NULL);
	if (!ok) {
		return FALSE;
	}

	char* separator_pos = strchr(buffer, ',');
	if (separator_pos == NULL) {
		return FALSE;
	}
	// End of the string
	if (separator_pos - buffer >= BUFFER_SIZE) {
		return FALSE;
	}

	char* hw_id_ptr = separator_pos + 1;
	strncpy_s(buffer, buffer_size, hw_id_ptr, buffer_size);

	return TRUE;
}

static BOOL usbip_install_remove_device(HDEVINFO devinfoset,
		const struct usbip_install_devinfo_struct *dev_data)
{
	assert(devinfoset != NULL);
	assert(dev_data != NULL);

	SP_DEVINFO_DATA devinfo_data = { 0 };
	devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
	const BOOL open_ok = SetupDiOpenDeviceInfo(devinfoset,
		dev_data->dev_instance_path,
		GetConsoleWindow(),
		0,
		&devinfo_data);
	if (!open_ok) {
		err("Cannot open DeviceInfo, code %u", GetLastError());
		return FALSE;
	}
	const BOOL uninstall_ok = DiUninstallDevice(GetConsoleWindow(),
		devinfoset,
		&devinfo_data,
		0, FALSE);
	if (!uninstall_ok) {
		err("Cannot uninstall existing device!");
		return FALSE;
	}
	return TRUE;
}

static int usbip_install_base(struct usbip_install_devinfo_struct *data)
{
	GUID class_guid = { 0 };
	HINF inf_handle = INVALID_HANDLE_VALUE;
	char inf_file[BUFFER_SIZE] = { 0 };
	char hw_id[BUFFER_SIZE] = { 0 };
	char dev_decription[BUFFER_SIZE] = { 0 };

	HDEVINFO devinfoset = { 0 };
	SP_DEVINFO_DATA devinfo_data = { 0 };
	BOOL result_ok = TRUE;
	DWORD last_error = 0;

	result_ok = usbip_install_get_inf_path(inf_file, BUFFER_SIZE, data);
	if (!result_ok) {
		err("Cannot find usbip_vhci.inf file");
		goto error;
	}

	if (!usbip_install_get_class_id(&class_guid, inf_file)) {
		err("Cannot obtain class id");
		goto error;
	}

	inf_handle = SetupOpenInfFile(inf_file, NULL, INF_STYLE_WIN4, NULL);
	if (inf_handle == INVALID_HANDLE_VALUE) {
		err("Cannot open inf file");
		goto error;
	}
	result_ok = usbip_install_get_device_description(dev_decription, BUFFER_SIZE,
		inf_handle, data);
	if(!result_ok) {
		err("Cannot get Device decription");
		goto error;
	}

	devinfoset = SetupDiCreateDeviceInfoList(&class_guid, NULL);
	if (devinfoset == INVALID_HANDLE_VALUE) {
		goto error;
	}

	do {
		memset(&devinfo_data, 0, sizeof(SP_DEVINFO_DATA));
		devinfo_data.cbSize = sizeof(SP_DEVINFO_DATA);
		result_ok = SetupDiCreateDeviceInfo(devinfoset,
			data->dev_instance_path,
			&class_guid,
			dev_decription,
			GetConsoleWindow(),
			0,
			&devinfo_data);
		if (!result_ok) {
			last_error = GetLastError();
			if (last_error != ERROR_DEVINST_ALREADY_EXISTS) {
				err("Cannot get DeviceInfo. Remove device manually by Device Manager, restart PC and try again");
				goto error;
			}
			result_ok = usbip_install_remove_device(devinfoset, data);
			if (!result_ok) {
				err("Cannot get DeviceInfo. Remove device manually by Device Manager, restart PC and try again");
				goto error;
			}
			continue;
		}
		break;
	} while (TRUE);

	result_ok = usbip_install_get_hardware_id(hw_id, BUFFER_SIZE - 1,
		inf_handle, data);
	if (!result_ok) {
		goto error;
	}

	// We need to set HW ID for PlugAndPlay driver install
	result_ok = SetupDiSetDeviceRegistryProperty(devinfoset,
		&devinfo_data,
		SPDRP_HARDWAREID,
		hw_id, (DWORD)strlen(hw_id));
	if (!result_ok) {
		goto error;
	}

	result_ok = SetupDiRegisterDeviceInfo(devinfoset,
		&devinfo_data,
		SPRDI_FIND_DUPS, NULL, NULL, NULL);
	if (!result_ok) {
		goto error;
	}

	// Install driver by INF file and hardware id
	result_ok = UpdateDriverForPlugAndPlayDevices(NULL,
		hw_id,
		inf_file,
		INSTALLFLAG_FORCE,
		FALSE);
	if (!result_ok) {
		goto error;
	}

	SetupDiDestroyDeviceInfoList(devinfoset);
	info("usbip_vhci driver installed sucessfully");
	return 0;

error:
	err("Cannot install usbip_vhci driver");
	SetupDiDestroyDeviceInfoList(devinfoset);
	return 1;
}

int usbip_install(int argc, char *argv[])
{
	int return_code = 0;
	return_code = usbip_install_base(&device_list[DEVICE_LIST_VHCI_DRIVER]);
	return return_code;
}
