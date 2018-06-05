#include "stubctl.h"

#include "usbip_stub.h"

BOOL install_driver_service(void);
BOOL uninstall_driver_service(void);

static BOOL
is_admin(void)
{
	SID_IDENTIFIER_AUTHORITY	NtAuthority = { SECURITY_NT_AUTHORITY };
	PSID	AdministratorsGroup;
	BOOL	isMember;

	if (!AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
				      DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdministratorsGroup))
		return FALSE;

	if (!CheckTokenMembership(NULL, AdministratorsGroup, &isMember)) {
		isMember = FALSE;
	}
	FreeSid(AdministratorsGroup);

	return isMember;
}

int
install_stub_driver(devno_t devno)
{
	if (!is_admin()) {
		err("requires administrative privileges.\n");
		return 1;
	}

#if 0 ////TODO
	/* only add the default class keys if there is nothing else to do. */
	if (filter_context->class_filters ||
	    filter_context->device_filters ||
	    filter_context->inf_files ||
	    filter_context->switches.add_all_classes ||
	    filter_context->switches.add_device_classes || 
	    filter_context->remove_all_device_filters) {
		filter_context->switches.add_default_classes = FALSE;
	}
	else {
		filter_context->switches.add_default_classes = TRUE;
	}
#endif

	if (devno == 0) {
		install_driver_service();
	}
	else {
		if (attach_stub_driver(devno)) {
			info("filter inserted successfully: %hhu", devno);
		}
		else {
			info("failed to insert filter: %hhu", devno);
		}
	}
#if 0 ///TODO
	if (filter_context->switches.switches_value || filter_context->class_filters || filter_context->device_filters)
	{
		if (filter_context->switches.add_all_classes || filter_context->switches.add_device_classes)
			refresh_only = FALSE;
		else
			refresh_only = TRUE;
		
		if (!usb_registry_get_usb_class_keys(filter_context, refresh_only)) {
			ret = -1;
			break;
		}

		if (!usb_registry_get_all_class_keys(filter_context, refresh_only))
		{
			ret = -1;
			break;
		}
		ret = usb_install_service(filter_context);
		if (ret < 0) {
			break;
		}
	}
#endif

#if 0 ////DEL??
	filter_file = filter_context->inf_files;
	while (filter_file)
	{
		USBMSG("installing inf %s..\n", filter_file->name);
		if (usb_install_inf_np(filter_file->name, FALSE, TRUE) < 0)
		{
			ret = -1;
			break;
		}
		filter_file = filter_file->next;
	}
	if (ret == -1)
		break;
#endif
	return 0;
}

int
uninstall_stub_driver(devno_t devno)
{
#if 0 ////TODO
	if (filter_context->switches.switches_value ||
	    filter_context->class_filters ||
	    filter_context->device_filters)
	{
		if (filter_context->switches.add_all_classes || filter_context->switches.add_device_classes)
			refresh_only = FALSE;
		else
			refresh_only = TRUE;

		if (!usb_registry_get_usb_class_keys(filter_context, refresh_only))
		{
			ret = -1;
			break;
		}
		if (!usb_registry_get_all_class_keys(filter_context, refresh_only))
		{
			ret = -1;
			break;
		}
	}
#endif

	if (devno == 0)
		uninstall_driver_service();
	else
		detach_stub_driver(devno);

#if 0
	// rollback/uninstall devices using inf files
	filter_file = filter_context->inf_files;
	while (filter_file)
	{
		USBMSG("uninstalling inf %s..\n", filter_file->name);
		if (usb_install_inf_np(filter_file->name, TRUE, TRUE) < 0)
		{
			ret = -1;
			break;
		}
		filter_file = filter_file->next;
	}
	if (ret == -1)
		break;
#endif
	return 0;
}
