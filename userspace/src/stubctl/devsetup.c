#include "stubctl.h"

#include <SetupAPI.h>
#include <stdlib.h>

#include "usbip_setupdi.h"
#include "usbip_stub.h"

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

static int
walker_ctl(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	BOOL	*pis_start = (BOOL *)ctx;

	info("%s devices..", *pis_start ? "starting" : "stopping");
	if (is_service_usbip_stub(dev_info, pdev_info_data)) {
		set_device_state(*pis_start ? DICS_ENABLE : DICS_DISABLE, dev_info, pdev_info_data);
	}
	return 0;
}

static void
ctl_usbip_stub_devs(BOOL is_start)
{
	traverse_usbdevs(walker_ctl, TRUE, &is_start);
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