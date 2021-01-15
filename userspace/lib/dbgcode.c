#include "namecode.h"

#include "usbip_common.h"

static namecode_t	namecodes_op_code_status[] = {
	K_V(ST_OK)
	K_V(ST_NA)
	K_V(ST_DEV_BUSY)
	K_V(ST_DEV_ERR)
	K_V(ST_NODEV)
	K_V(ST_ERROR)
	{0,0}
};

static const char *
dbg_namecode(char *buf, int buf_max, namecode_t *namecodes, const char *codetype, int code)
{
	int i;

	for (i = 0; namecodes[i].name; i++) {
		if (code == namecodes[i].code) {
			snprintf(buf, buf_max, "%s", namecodes[i].name);
			return buf;
		}
	}

	snprintf(buf, buf_max, "Unknown %s code: %x", codetype, code);
	return buf;
}

const char *
dbg_opcode_status(int status)
{
	static char	buf[128];

	return dbg_namecode(buf, 128, namecodes_op_code_status, "op_code_status", status);
}
