#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

wchar_t *
utf8_to_wchar(const char *str)
{
	wchar_t	*wstr;
	int	size;

	size = MultiByteToWideChar(CP_UTF8, 0, str, -1, NULL, 0);
	if (size <= 1)
		return NULL;

	if ((wstr = (wchar_t *)calloc(size, sizeof(wchar_t))) == NULL)
		return NULL;
	if (MultiByteToWideChar(CP_UTF8, 0, str, -1, wstr, size) != size) {
		free(wstr);
		return NULL;
	}
	return wstr;
}

int
vasprintf(char **strp, const char *fmt, va_list ap)
{
	size_t	size;
	char	*str;
	int	ret;

	int len = _vscprintf(fmt, ap);
	if (len == -1) {
		return -1;
	}
	size = (size_t)len + 1;
	str = malloc(size);
	if (!str) {
		return -1;
	}
	ret = vsprintf_s(str, len + 1, fmt, ap);
	if (ret == -1) {
		free(str);
		return -1;
	}
	*strp = str;
	return ret;
}

int
asprintf(char **strp, const char *fmt, ...)
{
	va_list	ap;
	int	ret;

	va_start(ap, fmt);
	ret = vasprintf(strp, fmt, ap);
	va_end(ap);
	return ret;
}