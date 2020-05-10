#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"

extern PAGEABLE NTSTATUS reg_wmi(__in pusbip_vhub_dev_t vhub);
extern PAGEABLE NTSTATUS dereg_wmi(__in pusbip_vhub_dev_t vhub);

PAGEABLE void
invalidate_vhub(pusbip_vhub_dev_t vhub)
{
	PAGED_CODE();

	// Stop all access to the device, fail any outstanding I/O to the device,
	// and free all the resources associated with the device.

	// Disable the device interface and free the buffer
	if (vhub->InterfaceName.Buffer != NULL) {
		IoSetDeviceInterfaceState(&vhub->InterfaceName, FALSE);

		ExFreePool(vhub->InterfaceName.Buffer);
		RtlZeroMemory(&vhub->InterfaceName, sizeof(UNICODE_STRING));
	}

	// Inform WMI to remove this DeviceObject from its list of providers.
	dereg_wmi(vhub);
}

PAGEABLE NTSTATUS
start_vhub(pusbip_vhub_dev_t vhub)
{
	POWER_STATE	powerState;
	NTSTATUS	status;

	PAGED_CODE();

	// Check the function driver source to learn
	// about parsing resource list.

	// Enable device interface. If the return status is
	// STATUS_OBJECT_NAME_EXISTS means we are enabling the interface
	// that was already enabled, which could happen if the device
	// is stopped and restarted for resource rebalancing.
	status = IoSetDeviceInterfaceState(&vhub->InterfaceName, TRUE);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "IoSetDeviceInterfaceState failed: 0x%x\n", status);
		return status;
	}

	// Set the device power state to fully on. Also if this Start
	// is due to resource rebalance, you should restore the device
	// to the state it was before you stopped the device and relinquished
	// resources.

	vhub->common.DevicePowerState = PowerDeviceD0;
	powerState.DeviceState = PowerDeviceD0;
	PoSetPowerState(vhub->common.Self, DevicePowerState, powerState);

	SET_NEW_PNP_STATE(vhub, Started);

	// Register with WMI
	status = reg_wmi(vhub);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "start_vhub: reg_wmi failed (%x)\n", status);
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pusbip_vhub_dev_t vhub, ULONG *info)
{
	pusbip_vpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;

	PAGED_CODE();

	DBGI(DBG_PNP, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, usbip_vpdo_dev_t, Link);
		if (vpdo->port > 127 || vpdo->port == 0) {
			DBGE(DBG_PNP, "strange error");
			continue;
		}
		if (st->u.max_used_port < (char)vpdo->port)
			st->u.max_used_port = (char)vpdo->port;
		st->u.port_status[vpdo->port] = 1;
	}
	ExReleaseFastMutex(&vhub->Mutex);
	*info = sizeof(*st);
	return STATUS_SUCCESS;
}
