/* sg_danger_policy.c -- fail-closed selection of one danger writer. */
#include "q_shared.h"
#include "slipgate/sg_danger_policy.h"

#include <stddef.h>

typedef enum danger_port_parse_e
{
	DANGER_PORT_OK = 0,
	DANGER_PORT_MISSING,
	DANGER_PORT_NONCANONICAL
} danger_port_parse_t;

static danger_port_parse_t Danger_ParsePort(const char *text,
	uint16_t *value_out)
{
	uint32_t value = 0;
	size_t length = 0;
	size_t i;

	if (!text || !value_out)
		return DANGER_PORT_MISSING;
	while (length <= 5U && text[length] != '\0')
		length++;
	if (length == 0U || length > 5U ||
	    (length > 1U && text[0] == '0'))
		return DANGER_PORT_NONCANONICAL;
	for (i = 0; i < length; i++)
	{
		uint32_t digit;

		if (text[i] < '0' || text[i] > '9')
			return DANGER_PORT_NONCANONICAL;
		digit = (uint32_t)(text[i] - '0');
		value = value * UINT32_C(10) + digit;
		if (value > UINT16_MAX)
			return DANGER_PORT_NONCANONICAL;
	}
	*value_out = (uint16_t)value;
	return DANGER_PORT_OK;
}

static sg_danger_policy_status_t Danger_EffectivePort(
	const sg_danger_port_value_t *port,
	const sg_danger_port_value_t *ip_hostport,
	const sg_danger_port_value_t *hostport,
	uint16_t *effective_out)
{
	const sg_danger_port_value_t *candidate = NULL;
	uint16_t value = 0;
	danger_port_parse_t parsed;

	if (ip_hostport && ip_hostport->string)
	{
		parsed = Danger_ParsePort(ip_hostport->string, &value);
		if (parsed != DANGER_PORT_OK)
			return SG_DANGER_POLICY_PORT_NONCANONICAL;
		if (value != 0U)
			candidate = ip_hostport;
	}
	if (!candidate && hostport && hostport->string)
	{
		parsed = Danger_ParsePort(hostport->string, &value);
		if (parsed != DANGER_PORT_OK)
			return SG_DANGER_POLICY_PORT_NONCANONICAL;
		if (value != 0U)
			candidate = hostport;
	}
	if (!candidate)
		candidate = port;
	if (!candidate || !candidate->string)
		return SG_DANGER_POLICY_PORT_MISSING;
	parsed = Danger_ParsePort(candidate->string, &value);
	if (parsed == DANGER_PORT_MISSING)
		return SG_DANGER_POLICY_PORT_MISSING;
	if (parsed != DANGER_PORT_OK || value == 0U)
		return SG_DANGER_POLICY_PORT_NONCANONICAL;
	if (!(candidate->flags & CVAR_NOSET))
		return SG_DANGER_POLICY_PORT_UNPROTECTED;
	*effective_out = value;
	return SG_DANGER_POLICY_OK;
}

sg_danger_policy_status_t SG_DangerPolicySelect(
	const char *selector,
	const sg_danger_port_value_t *port,
	const sg_danger_port_value_t *ip_hostport,
	const sg_danger_port_value_t *hostport,
	uint16_t *selected_port_out)
{
	uint16_t selected;
	uint16_t effective;
	danger_port_parse_t parsed;
	sg_danger_policy_status_t status;

	if (!selected_port_out)
		return SG_DANGER_POLICY_BAD_SELECTOR;
	parsed = Danger_ParsePort(selector, &selected);
	if (parsed != DANGER_PORT_OK)
		return SG_DANGER_POLICY_BAD_SELECTOR;
	if (selected == 0U)
		return SG_DANGER_POLICY_DISABLED;
	status = Danger_EffectivePort(port, ip_hostport, hostport, &effective);
	if (status != SG_DANGER_POLICY_OK)
		return status;
	if (selected != effective)
		return SG_DANGER_POLICY_PORT_MISMATCH;
	*selected_port_out = selected;
	return SG_DANGER_POLICY_OK;
}

const char *SG_DangerPolicyReason(sg_danger_policy_status_t status)
{
	switch (status)
	{
	case SG_DANGER_POLICY_OK:
		return "selected protected server port";
	case SG_DANGER_POLICY_DISABLED:
		return "selector is zero";
	case SG_DANGER_POLICY_BAD_SELECTOR:
		return "selector is not canonical decimal port";
	case SG_DANGER_POLICY_PORT_MISSING:
		return "effective engine port is missing";
	case SG_DANGER_POLICY_PORT_NONCANONICAL:
		return "effective engine port is not canonical decimal";
	case SG_DANGER_POLICY_PORT_UNPROTECTED:
		return "effective engine port lacks CVAR_NOSET";
	case SG_DANGER_POLICY_PORT_MISMATCH:
		return "selector does not match effective engine port";
	default:
		return "unknown danger persistence policy status";
	}
}
