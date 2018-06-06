#include "usbipd.h"

#include "usbip_network.h"
#include "usbipd_stub.h"
#include "usbip_setupdi.h"

static SRWLOCK	lock;

static LIST_HEAD(edev_list);
static int	n_edevs;

struct usbip_exportable_device *
find_edev(const char *busid)
{
	struct list_head	*p;

	list_for_each(p, &edev_list) {
		struct usbip_exportable_device *edev;

		edev = list_entry(p, struct usbip_exportable_device, node);
		if (!strncmp(busid, edev->udev.busid, USBIP_BUS_ID_SIZE)) {
			return edev;
		}
	}
	return NULL;
}

void
cleanup_edevs(void)
{
	struct list_head	*p, *n;

	list_for_each_safe(p, n, &edev_list) {
		struct usbip_exportable_device *edev;

		edev = list_entry(p, struct usbip_exportable_device, node);
		list_del(&edev->node);
		free(edev);
	}
}

void
add_edev(devno_t devno, const char *devpath, unsigned short vendor, unsigned short product)
{
	struct usbip_exportable_device	*edev;

	edev = (struct usbip_exportable_device *)malloc(sizeof(struct usbip_exportable_device));
	if (edev == NULL) {
		err("add_edev: out of memory");
		return;
	}
	memset(edev, 0, sizeof(struct usbip_exportable_device));
	edev->udev.busnum = 1;
	edev->udev.devnum = (int)devno;
	snprintf(edev->udev.path, USBIP_DEV_PATH_MAX, devpath);
	snprintf(edev->udev.busid, USBIP_BUS_ID_SIZE, "1-%hhu", devno);
	edev->udev.idVendor = vendor;
	edev->udev.idProduct = product;

	list_add(&edev->node, edev_list.prev);
	n_edevs++;
}

void
get_edev_list(struct list_head **phead, int *pn_edevs)
{
	AcquireSRWLockShared(&lock);
	*phead = &edev_list;
	*pn_edevs = n_edevs;
}

void
put_edev_list(void)
{
	ReleaseSRWLockShared(&lock);
}

void
enter_refresh_edevs(void)
{
	AcquireSRWLockExclusive(&lock);
}

void
leave_refresh_edevs(void)
{
	ReleaseSRWLockExclusive(&lock);
}

void
init_edev(void)
{
	InitializeSRWLock(&lock);
}