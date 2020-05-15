#include "vhci.h"

#include "usbip_vhci_api.h"
#include "vhci_pnp.h"
#include "usbreq.h"

extern PAGEABLE void invalidate_vhub(pusbip_vhub_dev_t vhub);
extern PAGEABLE NTSTATUS start_vhub(pusbip_vhub_dev_t vhub);

extern PAGEABLE PDEVICE_RELATIONS
vhub_get_bus_relations(pusbip_vhub_dev_t vhub, PDEVICE_RELATIONS oldRelations);
extern PAGEABLE BOOLEAN vhub_invalidate_vpdos_by_vhub_surprise_removal(pusbip_vhub_dev_t vhub);
extern PAGEABLE void vhub_remove_all_vpdos(pusbip_vhub_dev_t vhub);

static NTSTATUS
vhci_completion_routine(__in PDEVICE_OBJECT devobj, __in PIRP Irp, __in PVOID Context)
{
	UNREFERENCED_PARAMETER(devobj);

	// If the lower driver didn't return STATUS_PENDING, we don't need to
	// set the event because we won't be waiting on it.
	// This optimization avoids grabbing the dispatcher lock and improves perf.
	if (Irp->PendingReturned == TRUE) {
		KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED; // Keep this IRP
}

static PAGEABLE NTSTATUS
vhci_send_irp_synchronously(__in PDEVICE_OBJECT devobj, __in PIRP Irp)
{
	KEVENT		event;
	NTSTATUS	status;

	PAGED_CODE();

	KeInitializeEvent(&event, NotificationEvent, FALSE);

	IoCopyCurrentIrpStackLocationToNext(Irp);

	IoSetCompletionRoutine(Irp, vhci_completion_routine, &event, TRUE, TRUE, TRUE);

	status = IoCallDriver(devobj, Irp);

	// Wait for lower drivers to be done with the Irp.
	// Important thing to note here is when you allocate
	// the memory for an event in the stack you must do a
	// KernelMode wait instead of UserMode to prevent
	// the stack from getting paged out.
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = Irp->IoStatus.Status;
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_add_vhub(__in PDRIVER_OBJECT drvobj, __in PDEVICE_OBJECT devobj_lower)
{
	PDEVICE_OBJECT		devobj;
	pusbip_vhub_dev_t	vhub = NULL;
	PWCHAR		deviceName = NULL;
#if DBG
	ULONG		nameLength;
#endif
	NTSTATUS	status;

	PAGED_CODE();

	DBGI(DBG_GENERAL | DBG_PNP, "Add Device: 0x%p\n", devobj_lower);

	status = IoCreateDevice(drvobj, sizeof(usbip_vhub_dev_t), NULL,
		FILE_DEVICE_BUS_EXTENDER, FILE_DEVICE_SECURE_OPEN, TRUE, &devobj);

	if (!NT_SUCCESS(status)) {
		goto End;
	}

	vhub = (pusbip_vhub_dev_t)devobj->DeviceExtension;
	RtlZeroMemory(vhub, sizeof(usbip_vhub_dev_t));

	// Set the initial state of the vhub
	INITIALIZE_PNP_STATE(vhub);

	vhub->common.is_vhub = TRUE;
	vhub->common.Self = devobj;

	ExInitializeFastMutex(&vhub->Mutex);

	InitializeListHead(&vhub->head_vpdo);

	// Set the vpdo for use with PlugPlay functions
	vhub->UnderlyingPDO = devobj_lower;

	// Set the initial powerstate of the vhub
	vhub->common.DevicePowerState = PowerDeviceUnspecified;
	vhub->common.SystemPowerState = PowerSystemWorking;

	// Biased to 1. Transition to zero during remove device
	// means IO is finished. Transition to 1 means the device
	// can be stopped.
	vhub->OutstandingIO = 1;

	// Initialize the remove event to Not-Signaled.  This event
	// will be set when the OutstandingIO will become 0.
	KeInitializeEvent(&vhub->RemoveEvent, SynchronizationEvent, FALSE);

	// Initialize the stop event to Signaled:
	// there are no Irps that prevent the device from being
	// stopped. This event will be set when the OutstandingIO
	// will become 0.
	KeInitializeEvent(&vhub->StopEvent, SynchronizationEvent, TRUE);

	devobj->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;

	// Tell the Plug & Play system that this device will need a
	// device interface.
	status = IoRegisterDeviceInterface(devobj_lower, (LPGUID)&GUID_DEVINTERFACE_VHCI_USBIP, NULL, &vhub->DevIntfVhci);
	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "failed to register vhci device interface: %s\n", dbg_ntstatus(status));
		goto End;
	}
	status = IoRegisterDeviceInterface(devobj_lower, (LPGUID)&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, NULL, &vhub->DevIntfUSBHC);
	if (!NT_SUCCESS(status)) {
		RtlFreeUnicodeString(&vhub->DevIntfVhci);
		DBGE(DBG_PNP, "failed to register USB Host controller device interface: %s\n", dbg_ntstatus(status));
		goto End;
	}
	status = IoRegisterDeviceInterface(devobj_lower, (LPGUID)&GUID_DEVINTERFACE_USB_HUB, NULL, &vhub->DevIntfRootHub);
	if (!NT_SUCCESS(status)) {
		RtlFreeUnicodeString(&vhub->DevIntfVhci);
		RtlFreeUnicodeString(&vhub->DevIntfUSBHC);
		DBGE(DBG_PNP, "failed to register USB Root Hub device interface: %s\n", dbg_ntstatus(status));
		goto End;
	}

	// Attach our vhub to the device stack.
	// The return value of IoAttachDeviceToDeviceStack is the top of the
	// attachment chain.  This is where all the IRPs should be routed.
	vhub->NextLowerDriver = IoAttachDeviceToDeviceStack(devobj, devobj_lower);

	if (vhub->NextLowerDriver == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto End;
	}
#if DBG
	// We will demonstrate here the step to retrieve the name of the vpdo
	status = IoGetDeviceProperty(devobj_lower, DevicePropertyPhysicalDeviceObjectName, 0, NULL, &nameLength);

	if (status != STATUS_BUFFER_TOO_SMALL) {
		DBGE(DBG_PNP, "AddDevice:IoGDP failed (0x%x)\n", status);
		goto End;
	}

	deviceName = ExAllocatePoolWithTag(NonPagedPool, nameLength, USBIP_VHCI_POOL_TAG);

	if (NULL == deviceName) {
		DBGE(DBG_PNP, "AddDevice: no memory to alloc for deviceName(0x%x)\n", nameLength);
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}

	status = IoGetDeviceProperty(devobj_lower, DevicePropertyPhysicalDeviceObjectName,
		nameLength, deviceName, &nameLength);

	if (!NT_SUCCESS(status)) {
		DBGE(DBG_PNP, "AddDevice:IoGDP(2) failed (0x%x)", status);
		goto End;
	}

	DBGI(DBG_PNP, "AddDevice: %p to %p->%p (%ws) \n", vhub, vhub->NextLowerDriver, devobj_lower, deviceName);
#endif

	// We are done with initializing, so let's indicate that and return.
	// This should be the final step in the AddDevice process.
	devobj->Flags &= ~DO_DEVICE_INITIALIZING;

End:
	if (deviceName) {
		ExFreePool(deviceName);
	}

	if (!NT_SUCCESS(status) && devobj) {
		if (vhub && vhub->NextLowerDriver) {
			IoDetachDevice(vhub->NextLowerDriver);
		}
		IoDeleteDevice(devobj);
	}

	return status;
}

PAGEABLE NTSTATUS
vhci_pnp_vhub(PDEVICE_OBJECT devobj, PIRP Irp, PIO_STACK_LOCATION IrpStack, pusbip_vhub_dev_t vhub)
{
	NTSTATUS	status;

	PAGED_CODE ();

	inc_io_vhub(vhub);

	switch (IrpStack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		// Send the Irp down and wait for it to come back.
		// Do not touch the hardware until then.
		status = vhci_send_irp_synchronously(vhub->NextLowerDriver, Irp);
		if (NT_SUCCESS(status)) {
			// Initialize your device with the resources provided
			// by the PnP manager to your device.
			status = start_vhub(vhub);
		}

		// We must now complete the IRP, since we stopped it in the
		// completion routine with MORE_PROCESSING_REQUIRED.
		Irp->IoStatus.Status = status;
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		dec_io_vhub(vhub);
		return status;
	case IRP_MN_QUERY_STOP_DEVICE:
		// The PnP manager is trying to stop the device
		// for resource rebalancing. Fail this now if you
		// cannot stop the device in response to STOP_DEVICE.

		SET_NEW_PNP_STATE(vhub, StopPending);
		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
		break;
	case IRP_MN_CANCEL_STOP_DEVICE:
		// The PnP Manager sends this IRP, at some point after an
		// IRP_MN_QUERY_STOP_DEVICE, to inform the drivers for a
		// device that the device will not be stopped for
		// resource reconfiguration.

		// First check to see whether you have received cancel-stop
		// without first receiving a query-stop. This could happen if
		// someone above us fails a query-stop and passes down the subsequent
		// cancel-stop.
		if (StopPending == vhub->common.DevicePnPState) {
			// We did receive a query-stop, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhub);
			ASSERT(vhub->common.DevicePnPState == Started);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS; // We must not fail the IRP.
		break;
	case IRP_MN_STOP_DEVICE:
		// Stop device means that the resources given during Start device
		// are now revoked. Note: You must not fail this Irp.
		// But before you relieve resources make sure there are no I/O in
		// progress. Wait for the existing ones to be finished.
		// To do that, first we will decrement this very operation.
		// When the counter goes to 1, Stop event is set.

		dec_io_vhub(vhub);

		KeWaitForSingleObject(&vhub->StopEvent, Executive, KernelMode, FALSE, NULL);

		// Increment the counter back because this IRP has to
		// be sent down to the lower stack.
		inc_io_vhub(vhub);

		// Free resources given by start device.
		SET_NEW_PNP_STATE(vhub, Stopped);

		// We don't need a completion routine so fire and forget.
		// Set the current stack location to the next stack location and
		// call the next device object.

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_QUERY_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		SET_NEW_PNP_STATE(vhub, RemovePending);

		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	case IRP_MN_CANCEL_REMOVE_DEVICE:
		// If we were to fail this call then we would need to complete the
		// IRP here.  Since we are not, set the status to SUCCESS and
		// call the next driver.

		// First check to see whether you have received cancel-remove
		// without first receiving a query-remove. This could happen if
		// someone above us fails a query-remove and passes down the
		// subsequent cancel-remove.

		if (vhub->common.DevicePnPState == RemovePending) {
			// We did receive a query-remove, so restore.
			RESTORE_PREVIOUS_PNP_STATE(vhub);
		}
		Irp->IoStatus.Status = STATUS_SUCCESS;// You must not fail the IRP.
		break;
	case IRP_MN_SURPRISE_REMOVAL:
		/* NOTE: vhub does not seem to suffer from surprise removal because it's a legacy device */
		DBGE(DBG_PNP, "FIXME: IRP_MN_SURPRISE_REMOVAL for vhub called\n");

		// The vhub has been unexpectedly removed from the machine
		// and is no longer available for I/O. invalidate_vhub clears
		// all the resources, frees the interface and de-registers
		// with WMI, but it doesn't delete the vhub. That's done
		// later in Remove device query.
		SET_NEW_PNP_STATE(vhub, SurpriseRemovePending);
		invalidate_vhub(vhub);

		vhub_invalidate_vpdos_by_vhub_surprise_removal(vhub);

		Irp->IoStatus.Status = STATUS_SUCCESS; // You must not fail the IRP.
		break;
	case IRP_MN_REMOVE_DEVICE:
		// The Plug & Play system has dictated the removal of this device.
		// We have no choice but to detach and delete the device object.

		// Check the state flag to see whether you are surprise removed
		if (vhub->common.DevicePnPState != SurpriseRemovePending) {
			invalidate_vhub(vhub);
		}

		SET_NEW_PNP_STATE(vhub, Deleted);

		// Wait for all outstanding requests to complete.
		// We need two decrements here, one for the increment in
		// the beginning of this function, the other for the 1-biased value of
		// OutstandingIO.

		dec_io_vhub(vhub);

		// The requestCount is at least one here (is 1-biased)

		dec_io_vhub(vhub);

		KeWaitForSingleObject(&vhub->RemoveEvent, Executive, KernelMode, FALSE, NULL);

		vhub_remove_all_vpdos(vhub);

		// We need to send the remove down the stack before we detach,
		// but we don't need to wait for the completion of this operation
		// (and to register a completion routine).
		Irp->IoStatus.Status = STATUS_SUCCESS;
		IoSkipCurrentIrpStackLocation(Irp);
		status = IoCallDriver(vhub->NextLowerDriver, Irp);

		// Detach from the underlying devices.
		IoDetachDevice(vhub->NextLowerDriver);

		DBGI(DBG_PNP, "Deleting vhub device object: 0x%p\n", devobj);

		IoDeleteDevice(devobj);

		return status;
	case IRP_MN_QUERY_DEVICE_RELATIONS:
		DBGI(DBG_PNP, "\tQueryDeviceRelation Type: %s\n", dbg_dev_relation(IrpStack->Parameters.QueryDeviceRelations.Type));

		if (BusRelations != IrpStack->Parameters.QueryDeviceRelations.Type) {
			// We don't support any other Device Relations
			break;
		}

		// Tell the plug and play system about all the vpdos.

		// There might also be device relations below and above this vhub,
		// so, be sure to propagate the relations from the upper drivers.

		// No Completion routine is needed so long as the status is preset
		// to success.  (vpdos complete plug and play irps with the current
		// IoStatus.Status and IoStatus.Information as the default.)

		Irp->IoStatus.Information = (ULONG_PTR)vhub_get_bus_relations(vhub, (PDEVICE_RELATIONS)Irp->IoStatus.Information);

		// Set up and pass the IRP further down the stack
		Irp->IoStatus.Status = STATUS_SUCCESS;
		break;
	default:
		// In the default case we merely call the next driver.
		// We must not modify Irp->IoStatus.Status or complete the IRP.
		break;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	status = IoCallDriver(vhub->NextLowerDriver, Irp);
	dec_io_vhub(vhub);

	return status;
}