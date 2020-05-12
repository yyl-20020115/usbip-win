#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"

extern PAGEABLE NTSTATUS reg_wmi(__in pusbip_vhub_dev_t vhub);
extern PAGEABLE NTSTATUS dereg_wmi(__in pusbip_vhub_dev_t vhub);

extern PAGEABLE void complete_pending_irp(pusbip_vpdo_dev_t vpdo);
extern PAGEABLE void complete_pending_read_irp(pusbip_vpdo_dev_t vpdo);

PAGEABLE BOOLEAN
vhub_is_empty_port(pusbip_vhub_dev_t vhub, ULONG port)
{
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);
		if (port == vpdo->port && vpdo->common.DevicePnPState != SurpriseRemovePending) {
			ExReleaseFastMutex(&vhub->Mutex);
			return FALSE;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	return TRUE;
}

PAGEABLE void
vhub_attach_vpdo(pusbip_vhub_dev_t vhub, pusbip_vpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	InsertTailList(&vhub->head_vpdo, &vpdo->Link);
	vhub->n_vpdos++;
	if (vpdo->plugged)
		vhub->n_vpdos_plugged++;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_detach_vpdo(pusbip_vhub_dev_t vhub, pusbip_vpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	RemoveEntryList(&vpdo->Link);
	InitializeListHead(&vpdo->Link);
	ASSERT(vhub->n_vpdos > 0);
	vhub->n_vpdos--;

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE PDEVICE_RELATIONS
vhub_get_bus_relations(pusbip_vhub_dev_t vhub, PDEVICE_RELATIONS oldRelations)
{
	PDEVICE_RELATIONS	relations;
	ULONG			length, prevcount = 0;
	PLIST_ENTRY		entry;
	int	i;

	ExAcquireFastMutex(&vhub->Mutex);

	if (oldRelations)
		prevcount = oldRelations->Count;

	// Need to allocate a new relations structure and add our vpdos to it
	length = sizeof(DEVICE_RELATIONS) + (vhub->n_vpdos_plugged + prevcount - 1) * sizeof(PDEVICE_OBJECT);

	relations = (PDEVICE_RELATIONS)ExAllocatePoolWithTag(PagedPool, length, USBIP_VHCI_POOL_TAG);
	if (relations == NULL) {
		DBGE(DBG_VHUB, "failed to allocate a new relation: out of memory\n");
		DBGE(DBG_VHUB, "old relations will be used\n");

		ExReleaseFastMutex(&vhub->Mutex);
		return oldRelations;
	}

	// Copy in the device objects so far
	if (prevcount > 0) {
		RtlCopyMemory(relations->Objects, oldRelations->Objects, prevcount * sizeof(PDEVICE_OBJECT));
	}

	relations->Count = prevcount + vhub->n_vpdos_plugged;

	// For each vpdo present on this bus add a pointer to the device relations
	// buffer, being sure to take out a reference to that object.
	// The Plug & Play system will dereference the object when it is done
	// with it and free the device relations buffer.
	for (entry = vhub->head_vpdo.Flink, i = 0; entry != &vhub->head_vpdo; entry = entry->Flink, i++) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		if (vpdo->plugged) {
			relations->Objects[prevcount + i] = vpdo->common.Self;
			ObReferenceObject(vpdo->common.Self);
		} else {
			vpdo->ReportedMissing = TRUE;
		}
	}

	DBGI(DBG_VHUB, "# of vpdo's: present: %d, reported: %d\n", vhub->n_vpdos, relations->Count);

	if (oldRelations) {
		ExFreePool(oldRelations);
	}

	ExReleaseFastMutex(&vhub->Mutex);

	return relations;
}

static PAGEABLE void
mark_unplugged(pusbip_vhub_dev_t vhub, pusbip_vpdo_dev_t vpdo)
{
	if (vpdo->plugged) {
		vpdo->plugged = FALSE;
		ASSERT(vhub->n_vpdos_plugged > 0);
		vhub->n_vpdos_plugged--;
	}
	else {
		DBGE(DBG_VHUB, "vpdo already unplugged: port: %u\n", vpdo->port);
	}
}

PAGEABLE void
vhub_mark_unplugged_vpdo(pusbip_vhub_dev_t vhub, pusbip_vpdo_dev_t vpdo)
{
	ExAcquireFastMutex(&vhub->Mutex);

	mark_unplugged(vhub, vpdo);

	ExReleaseFastMutex(&vhub->Mutex);
}

/* TODO: NOTE: This function will be removed if it is useless */
PAGEABLE void
vhub_invalidate_vpdos_by_vhub_surprise_removal(pusbip_vhub_dev_t vhub)
{
	PLIST_ENTRY	entry, nextEntry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink, nextEntry = entry->Flink; entry != &vhub->head_vpdo; entry = nextEntry, nextEntry = entry->Flink) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		RemoveEntryList(&vpdo->Link);
		InitializeListHead(&vpdo->Link);
		vpdo->vhub = NULL;
		vpdo->ReportedMissing = TRUE;
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE void
vhub_remove_all_vpdos(pusbip_vhub_dev_t vhub)
{
	PLIST_ENTRY	entry, nextEntry;

	// Typically the system removes all the  children before
	// removing the parent vhub. If for any reason child vpdo's are
	// still present we will destroy them explicitly, with one exception -
	// we will not delete the vpdos that are in SurpriseRemovePending state.

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink, nextEntry = entry->Flink; entry != &vhub->head_vpdo; entry = nextEntry, nextEntry = entry->Flink) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		RemoveEntryList(&vpdo->Link);
		if (vpdo->common.DevicePnPState == SurpriseRemovePending) {
			// We will reinitialize the list head so that we
			// wouldn't barf when we try to delink this vpdo from
			// the parent's vpdo list, when the system finally
			// removes the vpdo. Let's also not forget to set the
			// ReportedMissing flag to cause the deletion of the vpdo.
			DBGI(DBG_VHUB, "\tFound a surprise removed device: 0x%p\n", vpdo->common.Self);
			InitializeListHead(&vpdo->Link);
			vpdo->vhub = NULL;
			vpdo->ReportedMissing = TRUE;
			continue;
		}
		vhub->n_vpdos--;
		destroy_vpdo(vpdo);
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

PAGEABLE NTSTATUS
vhub_unplug_vpdo(pusbip_vhub_dev_t vhub, ULONG port, BOOLEAN is_eject)
{
	BOOLEAN		found = FALSE;
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);
	if (vhub->n_vpdos == 0) {
		ExReleaseFastMutex(&vhub->Mutex);
		return STATUS_NO_SUCH_DEVICE;
	}

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		if (port == 0 || port == vpdo->port) {
			if (!is_eject) {
				DBGI(DBG_VHUB, "plugging out: port: %u\n", vpdo->port);
				mark_unplugged(vhub, vpdo);
				complete_pending_read_irp(vpdo);
			}
			else {
				IoRequestDeviceEject(vpdo->common.Self);
			}
			found = TRUE;
			if (port != 0)
				break;
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);

	if (!found)
		return STATUS_INVALID_PARAMETER;
	return STATUS_SUCCESS;
}

PAGEABLE void
vhub_invalidate_unplugged_vpdos(pusbip_vhub_dev_t vhub)
{
	PLIST_ENTRY	entry;

	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		pusbip_vpdo_dev_t	vpdo = CONTAINING_RECORD(entry, usbip_vpdo_dev_t, Link);

		if (!vpdo->plugged) {
			complete_pending_irp(vpdo);
			SET_NEW_PNP_STATE(vpdo, PNP_DEVICE_REMOVED);
			IoInvalidateDeviceState(vpdo->common.Self);
		}
	}

	ExReleaseFastMutex(&vhub->Mutex);
}

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
		DBGE(DBG_VHUB, "IoSetDeviceInterfaceState failed: 0x%x\n", status);
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
		DBGE(DBG_VHUB, "start_vhub: reg_wmi failed (%x)\n", status);
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_get_ports_status(ioctl_usbip_vhci_get_ports_status *st, pusbip_vhub_dev_t vhub, ULONG *info)
{
	pusbip_vpdo_dev_t	vpdo;
	PLIST_ENTRY		entry;

	PAGED_CODE();

	DBGI(DBG_VHUB, "get ports status\n");

	RtlZeroMemory(st, sizeof(*st));
	ExAcquireFastMutex(&vhub->Mutex);

	for (entry = vhub->head_vpdo.Flink; entry != &vhub->head_vpdo; entry = entry->Flink) {
		vpdo = CONTAINING_RECORD (entry, usbip_vpdo_dev_t, Link);
		if (vpdo->port > 127 || vpdo->port == 0) {
			DBGE(DBG_VHUB, "strange error");
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
