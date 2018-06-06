#include "stubctl.h"

#include <SetupAPI.h>
#include <stdlib.h>

#include "usbip_setupdi.h"
#include "usbip_stub.h"

static int
walker_ctl(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	BOOL	*pis_start = (BOOL *)ctx;

	info("%s devices..", *pis_start ? "starting" : "stopping");
	if (is_service_usbip_stub(dev_info, pdev_info_data)) {
		set_device_state(dev_info, pdev_info_data, *pis_start ? DICS_ENABLE : DICS_DISABLE);
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