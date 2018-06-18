#include <ntddk.h>

#include <ntstrsafe.h>

#ifdef DBG

int
dbg_snprintf(char *buf, int size, const char *fmt, ...)
{
	va_list	arglist;
	size_t	len;
	NTSTATUS	status;

	va_start(arglist, fmt);
	status = RtlStringCchVPrintfA(buf, size, fmt, arglist);
	va_end(arglist);

	if (NT_ERROR(status))
		return 0;
	status = RtlStringCchLengthA(buf, size, &len);
	if (NT_ERROR(status))
		return 0;
	return (int)len;
}

#endif
