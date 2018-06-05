#include "usbip_setupdi.h"
#include "usbip_stub.h"

#include <stdlib.h>

char *get_dev_property(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, DWORD prop);

static BOOL
has_str_in_multistr(const char *mz_str, const char *str, BOOL no_case)
{
	while (*mz_str) {
		int	ret;

		ret = no_case ? _stricmp(mz_str, str) : strcmp(mz_str, str);
		if (ret == 0)
			return TRUE;
		mz_str += strlen(mz_str) + 1;
	}

	return FALSE;
}

static int
get_multistr_size(const char *mz_str)
{
	const char *p = mz_str;

	while (*p) {
		p += (strlen(p) + 1);
	}

	return (int)(p - mz_str) + 1;
}

static void
append_str_into_multistr(const char **pmz_str, const char *str)
{
	char	*mz_str;
	int	len_mz = get_multistr_size(*pmz_str);
	size_t	len = strlen(str);

	mz_str = realloc((void *)*pmz_str, len_mz + len + 1);
	if (mz_str == NULL)
		return;
	memcpy(mz_str + (len_mz - 1), str, len + 1);
	mz_str[len_mz - 1 + len + 1] = '\0';
	*pmz_str = mz_str;
}

static void
drop_str_from_multistr(char *mz_str, const char *str)
{
	char	*p = mz_str;
	int	len_mz = get_multistr_size(mz_str);

	while (*p) {
		if (strcmp(p, str) == 0) {
			memcpy(p, p + strlen(p) + 1, len_mz - strlen(p) - 1 - (p - mz_str));
			return;
		}
		else {
			p += (strlen(p) + 1);
		}
	}
}

BOOL
is_service_usbip_stub(HDEVINFO dev_info, SP_DEVINFO_DATA *dev_info_data)
{
	char	*svcname;
	BOOL	res;

	svcname = get_dev_property(dev_info, dev_info_data, SPDRP_SERVICE);
	if (svcname == NULL)
		return FALSE;
	res = _stricmp(svcname, STUB_DRIVER_SVCNAME) == 0 ? TRUE: FALSE;
	free(svcname);
	return res;
}

static BOOL
set_upper_filters(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, const char *filters)
{
	int	len;

	len = get_multistr_size(filters);
	if (len == 1) {
		filters = NULL;
		len = 0;
	}
	if (!SetupDiSetDeviceRegistryProperty(dev_info, pdev_info_data, SPDRP_UPPERFILTERS, (PBYTE)filters, len))
		return FALSE;
	return TRUE;
}

static BOOL
insert_stub_filter(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*filters;

	filters = get_upper_filters(dev_info, pdev_info_data);
	if (filters == NULL)
		return FALSE;

	if (!has_str_in_multistr(filters, STUB_DRIVER_SVCNAME, TRUE)) {
		append_str_into_multistr(&filters, STUB_DRIVER_SVCNAME);
	}

	set_upper_filters(dev_info, pdev_info_data, filters);
	free(filters);

	return TRUE;
}

static BOOL
remove_stub_filter(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data)
{
	char	*filters;

	filters = get_upper_filters(dev_info, pdev_info_data);
	if (filters == NULL)
		return FALSE;

	drop_str_from_multistr(filters, STUB_DRIVER_SVCNAME);
	set_upper_filters(dev_info, pdev_info_data, filters);
	free(filters);

	return TRUE;
}

static int
walker_attach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		if (!insert_stub_filter(dev_info, pdev_info_data))
			return -2;
		return -100;
	}
	return 0;
}

BOOL
attach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_attach, FALSE, &devno);
	if (ret == -100)
		return TRUE;
	return FALSE;
}

static int
walker_detach(HDEVINFO dev_info, PSP_DEVINFO_DATA pdev_info_data, devno_t devno, void *ctx)
{
	devno_t	*pdevno = (devno_t *)ctx;

	if (devno == *pdevno) {
		if (!remove_stub_filter(dev_info, pdev_info_data))
			return -2;
		return -100;
	}
	return 0;
}

BOOL
detach_stub_driver(devno_t devno)
{
	int	ret;

	ret = traverse_usbdevs(walker_detach, FALSE, &devno);
	if (ret == -100)
		return TRUE;
	return FALSE;
}