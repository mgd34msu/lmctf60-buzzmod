#include "q_shared.h"
#include "slipgate/sg_danger_policy.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { \
	if (!(expr)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
		failures++; \
	} \
} while (0)

static sg_danger_port_value_t Port(const char *text, int flags)
{
	sg_danger_port_value_t value;

	value.string = text;
	value.flags = flags;
	return value;
}

static sg_danger_policy_status_t Select(const char *selector,
	const sg_danger_port_value_t *port,
	const sg_danger_port_value_t *ip_hostport,
	const sg_danger_port_value_t *hostport,
	uint16_t *output)
{
	return SG_DangerPolicySelect(selector, port, ip_hostport, hostport,
		output);
}

int main(void)
{
	static const char *bad[] = {
		"", "00", "01", "+1", "-1", " 1", "1 ", "1.0", "1e3",
		"65536", "999999", "x", NULL
	};
	sg_danger_port_value_t port = Port("27910", CVAR_NOSET);
	sg_danger_port_value_t host = Port("0", 0);
	sg_danger_port_value_t ip = Port("0", 0);
	uint16_t output = UINT16_C(4321);
	size_t i;

	CHECK(Select("0", NULL, NULL, NULL, &output) ==
		SG_DANGER_POLICY_DISABLED);
	CHECK(output == UINT16_C(4321));
	for (i = 0; bad[i]; i++)
	{
		output = UINT16_C(4321);
		CHECK(Select(bad[i], &port, &ip, &host, &output) ==
			SG_DANGER_POLICY_BAD_SELECTOR);
		CHECK(output == UINT16_C(4321));
	}
	CHECK(Select("27910", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_OK);
	CHECK(output == UINT16_C(27910));
	output = UINT16_C(4321);
	CHECK(Select("27911", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_MISMATCH);
	CHECK(output == UINT16_C(4321));

	port.flags = 0;
	output = UINT16_C(4321);
	CHECK(Select("27910", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_UNPROTECTED);
	CHECK(output == UINT16_C(4321));
	port.flags = CVAR_NOSET;
	port.string = NULL;
	output = UINT16_C(4321);
	CHECK(Select("27910", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_MISSING);
	CHECK(output == UINT16_C(4321));
	port.string = "27910";

	host = Port("28000", CVAR_NOSET);
	CHECK(Select("28000", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_OK);
	ip = Port("28001", CVAR_NOSET);
	CHECK(Select("28001", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_OK);
	ip.flags = 0;
	output = UINT16_C(4321);
	CHECK(Select("28001", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_UNPROTECTED);
	CHECK(output == UINT16_C(4321));
	ip = Port("0", 0);
	host = Port("000", CVAR_NOSET);
	output = UINT16_C(4321);
	CHECK(Select("27910", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_NONCANONICAL);
	CHECK(output == UINT16_C(4321));
	host = Port("0", 0);
	ip = Port("bad", 0);
	output = UINT16_C(4321);
	CHECK(Select("27910", &port, &ip, &host, &output) ==
		SG_DANGER_POLICY_PORT_NONCANONICAL);
	CHECK(output == UINT16_C(4321));

	CHECK(SG_DangerPolicySelect("27910", &port, &ip, &host, NULL) ==
		SG_DANGER_POLICY_BAD_SELECTOR);
	for (i = 0; i < SG_DANGER_POLICY_STATUS_COUNT; i++)
		CHECK(SG_DangerPolicyReason((sg_danger_policy_status_t)i)[0] != '\0');
	CHECK(strstr(SG_DangerPolicyReason((sg_danger_policy_status_t)999),
		"unknown") != NULL);

	if (failures)
		return 1;
	puts("sg_danger_policy_test: ok");
	return 0;
}
