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

	return TRUE;
}

BOOL
uninstall_driver_service(void)
{
	/* older version of libusb used a system service, just remove it */
	if (!stop_driver_service())
		return FALSE;
	delete_driver_service();

	return 0;
}
