#include "vhci.h"

#include "vhci_dev.h"
#include "usbreq.h"

VOID
add_ref_vpdo(pusbip_vpdo_dev_t vpdo)
{
	InterlockedIncrement(&vpdo->n_refs);
}

VOID
del_ref_vpdo(pusbip_vpdo_dev_t vpdo)
{
	InterlockedDecrement(&vpdo->n_refs);
}

PAGEABLE NTSTATUS
destroy_vpdo(pusbip_vpdo_dev_t vpdo)
{
	PAGED_CODE();

	if (vpdo->winstid != NULL)
		ExFreePoolWithTag(vpdo->winstid, USBIP_VHCI_POOL_TAG);

	// VHCI does not queue any irps at this time so we have nothing to do.
	// Free any resources.

	//FIXME
	if (vpdo->fo) {
		vpdo->fo->FsContext = NULL;
		vpdo->fo = NULL;
	}
	DBGI(DBG_VPDO, "Deleting vpdo: 0x%p\n", vpdo);
	IoDeleteDevice(vpdo->common.Self);
	return STATUS_SUCCESS;
}

PAGEABLE void
complete_pending_read_irp(pusbip_vpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	PIRP	irp;

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	irp = vpdo->pending_read_irp;
	vpdo->pending_read_irp = NULL;
	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

	if (irp != NULL) {
		// We got pending_read_irp before submit_urbr
		BOOLEAN valid_irp;
		IoAcquireCancelSpinLock(&oldirql);
		valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
		IoReleaseCancelSpinLock(oldirql);
		if (valid_irp) {
			irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
			irp->IoStatus.Information = 0;
			IoCompleteRequest(irp, IO_NO_INCREMENT);
		}
	}
}

PAGEABLE void
complete_pending_irp(pusbip_vpdo_dev_t vpdo)
{
	KIRQL	oldirql;
	BOOLEAN	valid_irp;

	DBGI(DBG_VPDO, "finish pending irp\n");

	KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	while (!IsListEmpty(&vpdo->head_urbr)) {
		struct urb_req	*urbr;
		PIRP	irp;

		urbr = CONTAINING_RECORD(vpdo->head_urbr.Flink, struct urb_req, list_all);
		RemoveEntryListInit(&urbr->list_all);
		RemoveEntryListInit(&urbr->list_state);
		/* FIMXE event */
		KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);

		irp = urbr->irp;
		free_urbr(urbr);
		if (irp != NULL) {
			// urbr irps have cancel routine
			IoAcquireCancelSpinLock(&oldirql);
			valid_irp = IoSetCancelRoutine(irp, NULL) != NULL;
			IoReleaseCancelSpinLock(oldirql);
			if (valid_irp) {
				irp->IoStatus.Status = STATUS_DEVICE_NOT_CONNECTED;
				irp->IoStatus.Information = 0;
				IoCompleteRequest(irp, IO_NO_INCREMENT);
			}
		}

		KeAcquireSpinLock(&vpdo->lock_urbr, &oldirql);
	}

	vpdo->urbr_sent_partial = NULL; // sure?
	vpdo->len_sent_partial = 0;
	InitializeListHead(&vpdo->head_urbr_sent);
	InitializeListHead(&vpdo->head_urbr_pending);

	KeReleaseSpinLock(&vpdo->lock_urbr, oldirql);
}
