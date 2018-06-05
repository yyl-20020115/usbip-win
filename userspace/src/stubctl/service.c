#include "stubctl.h"

#include "usbip_stub.h"

#define DRIVER_DISPLAY	"usbip stub driver"
#define DRIVER_PATH	"system32\\drivers\\usbip_stub.sys"

extern void start_usbip_stub_devs(void);
extern void stop_usbip_stub_devs(void);

static BOOL
create_driver_service(void)
{
	SC_HANDLE	hScm;
	SC_HANDLE	hSvc;
	BOOL		res = FALSE;

	hScm = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (hScm == NULL) {
		err("failed to open service control manager: err: %x", GetLastError());
		return FALSE;
	}

	hSvc = OpenService(hScm, STUB_DRIVER_SVCNAME, SERVICE_ALL_ACCESS);
	if (hSvc != NULL) {
		if (ChangeServiceConfig(hSvc, SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START,
					SERVICE_ERROR_NORMAL, DRIVER_PATH,
					NULL, NULL, NULL, NULL, NULL, DRIVER_DISPLAY))
			res = TRUE;
		else {
			err("failed to change service config: err: %x", GetLastError());
		}
	}
	else if (GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST) {
		hSvc = CreateService(hScm, STUB_DRIVER_SVCNAME, DRIVER_DISPLAY, GENERIC_EXECUTE,
				     SERVICE_KERNEL_DRIVER, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, DRIVER_PATH,
				     NULL, NULL, NULL, NULL, NULL);
		if (hSvc != NULL)
			res = TRUE;
		else {
			err("failed to create service: err: %x", GetLastError());
		}
	}

	CloseServiceHandle(hSvc);
	CloseServiceHandle(hScm);

	return res;
}

static BOOL
check_service_stopped(SC_HANDLE hSvc)
{
	int	try = 0;

	while (try < 20) {
		SERVICE_STATUS	status;

		if (!QueryServiceStatus(hSvc, &status)) {
			err("failed to get status of service: err: %x\n", GetLastError());
			return FALSE;
		}

		if (status.dwCurrentState == SERVICE_STOPPED)
			return TRUE;

		if (!ControlService(hSvc, SERVICE_CONTROL_STOP, &status)) {
			err("failed to stop service: err: %x", GetLastError());
			return FALSE;
		}

		Sleep(500);
		try++;
	}
	return FALSE;
}

static BOOL
stop_driver_service(void)
{
	SC_HANDLE	hScm;
	SC_HANDLE	hSvc;
	BOOL		res = FALSE;

	info("stopping driver service..");

	hScm = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (hScm == NULL) {
		err("failed to open service control manager: err: %x", GetLastError());
		return FALSE;
	}

	hSvc = OpenService(hScm, STUB_DRIVER_SVCNAME, SERVICE_ALL_ACCESS);
	if (hSvc != NULL) {
		if (check_service_stopped(hSvc))
			res = TRUE;
		else {
			err("cannot stop service");
		}
		CloseServiceHandle(hSvc);
	}

	CloseServiceHandle(hScm);
	return res;
}

static BOOL
delete_driver_service(void)
{
	SC_HANDLE	hScm;
	SC_HANDLE	hSvc;
	BOOL	res = FALSE;

	info("deleting driver service..");

	hScm = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_ALL_ACCESS);
	if (hScm == NULL) {
		err("failed to open service control manager: err: %x", GetLastError());
		return FALSE;
	}

	hSvc = OpenService(hScm, STUB_DRIVER_SVCNAME, SERVICE_ALL_ACCESS);
	if (hSvc != NULL) {
		if (DeleteService(hSvc))
			res = TRUE;
		else {
			err("failed to delete service: err: %x", GetLastError());
		}
		CloseServiceHandle(hSvc);
	}

	CloseServiceHandle(hSvc);

	return res;
}

BOOL
install_driver_service(void)
{
	stop_usbip_stub_devs();

	if (!create_driver_service()) {
		err("failed to create service\n");
		return FALSE;
	}

	start_usbip_stub_devs();

#if 0 ///TODO
	/* insert device filter drivers */
	usb_registry_insert_device_filters(filter_context);

	/* insert class filter driver */
	usb_registry_insert_class_filter(filter_context);

	if (filter_context->class_filters_modified)
	{
		/* restart the whole USB system so that the new drivers will be loaded */
		usb_registry_restart_all_devices();
		filter_context->class_filters_modified = FALSE;
	}
	return ret;
#endif
	return TRUE;
}

BOOL
uninstall_driver_service(void)
{
	/* older version of libusb used a system service, just remove it */
	if (!stop_driver_service())
		return FALSE;
	delete_driver_service();

#if 0 ///TODO
	/* remove user specified device filters */
	if (filter_context->device_filters || filter_context->remove_all_device_filters)
	{
		usb_registry_remove_device_filter(filter_context);
	}
	/* remove class filter driver */
	usb_registry_remove_class_filter(filter_context);

	/* unload filter drivers */
	if (filter_context->class_filters_modified)
	{
		usb_registry_restart_all_devices();
		filter_context->class_filters_modified = FALSE;
	}
#endif
	return 0;
}
