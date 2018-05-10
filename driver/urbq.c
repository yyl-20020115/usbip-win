#include "busenum.h"

#include "usbip_proto.h"

extern int
process_urb_req(PIRP irp, struct urb_req *urb_r);

struct urb_req *
find_urb_req(PPDO_DEVICE_DATA pdodata, struct usbip_header *hdr)
{
	struct urb_req	*urb_r = NULL;
	KIRQL		oldirql;
	PLIST_ENTRY	le;

	KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
	for (le = pdodata->ioctl_q.Flink; le != &pdodata->ioctl_q; le = le->Flink) {
		urb_r = CONTAINING_RECORD(le, struct urb_req, list);
		if (urb_r->seq_num == hdr->base.seqnum) {
			if (IoSetCancelRoutine(urb_r->irp, NULL) == NULL) {
				/* already cancelled ? */
				urb_r = NULL;
			}
			else
				RemoveEntryList(le);
			break;
		}
	}
	KeReleaseSpinLock(&pdodata->q_lock, oldirql);

	return urb_r;
}

struct urb_req *
find_pending_urb_req(PPDO_DEVICE_DATA pdodata)
{
	PLIST_ENTRY	le;

	for (le = pdodata->ioctl_q.Flink; le != &pdodata->ioctl_q; le = le->Flink) {
		struct urb_req	*urb;

		urb = CONTAINING_RECORD(le, struct urb_req, list);
		if (!urb->sent) {
			urb->sent = True;
			if (urb->seq_num != 0) {
				ERROR(("non-zero seq_num: %d\n", urb->seq_num));
			}
			urb->seq_num = ++(pdodata->seq_num);
			return urb;
		}
	}
	return NULL;
}

static void
cancel_irp(PDEVICE_OBJECT pdo, PIRP Irp)
{
	PLIST_ENTRY le = NULL;
	int found = 0;
	struct urb_req * urb_r = NULL;
	PPDO_DEVICE_DATA pdodata;
	KIRQL oldirql = Irp->CancelIrql;

	pdodata = (PPDO_DEVICE_DATA)pdo->DeviceExtension;
	//	IoReleaseCancelSpinLock(DISPATCH_LEVEL);
	KdPrint(("Cancle Irp %p called\n", Irp));
	KeAcquireSpinLockAtDpcLevel(&pdodata->q_lock);
	for (le = pdodata->ioctl_q.Flink;
		le != &pdodata->ioctl_q;
		le = le->Flink) {
		urb_r = CONTAINING_RECORD(le, struct urb_req, list);
		if (urb_r->irp == Irp) {
			found = 1;
			RemoveEntryList(le);
			break;
		}
	}
	KeReleaseSpinLock(&pdodata->q_lock, oldirql);
	if (found) {
		ExFreeToNPagedLookasideList(&g_lookaside, urb_r);
	}
	else {
		KdPrint(("Warning, why we can't found it?\n"));
	}
	Irp->IoStatus.Status = STATUS_CANCELLED;
	IoCompleteRequest(Irp, IO_NO_INCREMENT);
	IoReleaseCancelSpinLock(Irp->CancelIrql);
}

static struct urb_req *
create_urb_req(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
	struct urb_req	*urb_r;

	urb_r = ExAllocateFromNPagedLookasideList(&g_lookaside);
	if (urb_r == NULL)
		return NULL;
	RtlZeroMemory(urb_r, sizeof(*urb_r));
	urb_r->pdodata = pdodata;
	urb_r->irp = irp;
	return urb_r;
}

static BOOL_t
insert_urb_req(PPDO_DEVICE_DATA pdodata, struct urb_req *urb_r)
{
	PIRP	irp = urb_r->irp;

	IoSetCancelRoutine(irp, cancel_irp);
	if (irp->Cancel && IoSetCancelRoutine(irp, NULL)) {
		return False;
	}
	else {
		IoMarkIrpPending(irp);
		InsertTailList(&pdodata->ioctl_q, &urb_r->list);
	}
	return True;
}

NTSTATUS
submit_urb_req(PPDO_DEVICE_DATA pdodata, PIRP irp)
{
	struct urb_req	*urb_r;
	KIRQL oldirql;
	PIRP read_irp;
	NTSTATUS status = STATUS_PENDING;

	if ((urb_r = create_urb_req(pdodata, irp)) == NULL)
		return STATUS_INSUFFICIENT_RESOURCES;

	KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
	read_irp = pdodata->pending_read_irp;
	pdodata->pending_read_irp = NULL;
	if (read_irp == NULL) {
		if (!insert_urb_req(pdodata, urb_r)) {
			KeReleaseSpinLock(&pdodata->q_lock, oldirql);
			ExFreeToNPagedLookasideList(&g_lookaside, urb_r);
			return STATUS_CANCELLED;
		}
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		return STATUS_PENDING;
	}

	urb_r->seq_num = ++(pdodata->seq_num);
	KeReleaseSpinLock(&pdodata->q_lock, oldirql);

	read_irp->IoStatus.Status = process_urb_req(read_irp, urb_r);

	if (read_irp->IoStatus.Status == STATUS_SUCCESS) {
		KeAcquireSpinLock(&pdodata->q_lock, &oldirql);
		urb_r->sent = True;
		status = insert_urb_req(pdodata, urb_r) ? STATUS_PENDING: STATUS_CANCELLED;
		KeReleaseSpinLock(&pdodata->q_lock, oldirql);
		if (status == STATUS_CANCELLED) {
			ExFreeToNPagedLookasideList(&g_lookaside, urb_r);
		}
	}
	else {
		ExFreeToNPagedLookasideList(&g_lookaside, urb_r);
		status = STATUS_INVALID_PARAMETER;
	}
	IoCompleteRequest(read_irp, IO_NO_INCREMENT);
	return status;
}
