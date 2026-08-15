/* sg_danger_policy.h -- fail-closed selection of one danger writer. */
#ifndef SG_DANGER_POLICY_H
#define SG_DANGER_POLICY_H

#include <stdint.h>

typedef struct sg_danger_port_value_s
{
	const char *string;
	int flags;
} sg_danger_port_value_t;

typedef enum sg_danger_policy_status_e
{
	SG_DANGER_POLICY_OK = 0,
	SG_DANGER_POLICY_DISABLED,
	SG_DANGER_POLICY_BAD_SELECTOR,
	SG_DANGER_POLICY_PORT_MISSING,
	SG_DANGER_POLICY_PORT_NONCANONICAL,
	SG_DANGER_POLICY_PORT_UNPROTECTED,
	SG_DANGER_POLICY_PORT_MISMATCH,
	SG_DANGER_POLICY_STATUS_COUNT
} sg_danger_policy_status_t;

/*
 * Select the effective Yamagi server port without trusting cvar_t::value.
 * Windows prefers ip_hostport, then hostport, then port; Unix uses port, but
 * accepting the same precedence everywhere fails closed if an override is
 * unexpectedly present. Zero-valued absent/unprotected overrides are neutral.
 * selector "0" is the shipped disabled state and does not inspect ports.
 */
sg_danger_policy_status_t SG_DangerPolicySelect(
	const char *selector,
	const sg_danger_port_value_t *port,
	const sg_danger_port_value_t *ip_hostport,
	const sg_danger_port_value_t *hostport,
	uint16_t *selected_port_out);

const char *SG_DangerPolicyReason(sg_danger_policy_status_t status);

#endif /* SG_DANGER_POLICY_H */
