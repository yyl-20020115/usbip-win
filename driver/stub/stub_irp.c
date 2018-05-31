#include "stub_driver.h"
#include "stub_dbg.h"

NTSTATUS
complete_irp(IRP *irp, NTSTATUS status, ULONG info)
{
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = info;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

BOOLEAN
is_my_irp(usbip_stub_dev_t *devstub, IRP *irp)
{
	UNREFERENCED_PARAMETER(devstub);
	UNREFERENCED_PARAMETER(irp);

#if 0 ////TODO
	/* check if the IRP is sent to usbip stub's device object or to */
	/* the lower one. This check is necessary since the device object */
	/* might be a filter */
	if (!irp->Tail.Overlay.OriginalFileObject)
		return FALSE;
	if (irp->Tail.Overlay.OriginalFileObject->DeviceObject == devstub->self)
		return TRUE;

	/* cover the cases when access is made by using device interfaces */
	if (!devstub->is_filter && devstub->interface_in_use &&
	    (irp->Tail.Overlay.OriginalFileObject->DeviceObject == devstub->pdo))
		return TRUE;

	return FALSE;
#endif
	return TRUE;
}

NTSTATUS
pass_irp_down(usbip_stub_dev_t *devstub, IRP *irp, PIO_COMPLETION_ROUTINE completion_routine, void *context)
{
	if (completion_routine) {
		IoCopyCurrentIrpStackLocationToNext(irp);
		IoSetCompletionRoutine(irp, completion_routine, context, TRUE, TRUE, TRUE);
	}
	else {
		IoSkipCurrentIrpStackLocation(irp);
	}

	return IoCallDriver(devstub->next_stack_dev, irp);
}
