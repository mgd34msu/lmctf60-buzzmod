/* Compile-only probe for the generated C action contract. */
#include "../slipgate/sg_action_contract.generated.h"

_Static_assert(SG_RUNE_V3_MAGIC == 0x454e5552U, "v3 magic drift");
_Static_assert(SG_RUNE_V3_HEADER_BYTES == 128, "v3 header size drift");
_Static_assert(SG_RUNE_V3_SEED_BYTES == 16, "v3 seed size drift");
_Static_assert(SG_RUNE_V3_LINK_BYTES == 44, "v3 link size drift");
_Static_assert(SG_RUNE_V3_LITTLE_ENDIAN_REQUIRED == 1, "wire endian drift");
_Static_assert(SG_RUNE_PROOF_PMOVE_SUBSTEP_MS == 25, "Pmove cadence drift");
_Static_assert(SG_RUNE_PROOF_SERVER_FRAME_MS == 100, "server cadence drift");
_Static_assert(SG_RUNE_PROOF_WORLD_FIXED_MIN == -32768 &&
               SG_RUNE_PROOF_WORLD_FIXED_MAX == 32767,
               "fixed world bounds drift");
_Static_assert(SG_RUNE_PROOF_ANGLE_SHORT_UNITS == 65536 &&
               SG_RUNE_PROOF_ANGLE_BYTE_UNITS == 256,
               "angle quantization drift");
_Static_assert(SG_RUNE_PROOF_DROP_LIP_Z_FIXED == 64 &&
               SG_RUNE_PROOF_DROP_LIP_Z_TOLERANCE_FIXED == 2,
               "drop lip fixed-point law drift");
_Static_assert(RL_RUN == 0 && RL_DOOR == 8, "legacy action IDs drift");
_Static_assert(RL_DOOR_DROP == 9 && RL_DOOR_HOOK == 11,
               "compound action IDs drift");
_Static_assert(RL_PROVEN == 0 && RL_DECLARED == 3 && RL_CONTRACTED == 4,
               "provenance IDs drift");
_Static_assert(RLCM_NONE == 0 && RLCM_PREOPEN == 1 && RLCM_RIDE == 2,
               "compound mode IDs drift");
_Static_assert(SG_ACTION_TRAIT_COUNT == 7 &&
               SG_ACTION_TRAIT_ALL_MASK == 0x007fU,
               "action trait inventory drift");
_Static_assert(SG_ENDPOINT_POLICY_COUNT == 7,
               "endpoint policy inventory drift");
_Static_assert(RLAP_DOOR_WAIT == 6 && RLAP_DOOR_PREOPEN_CONTACT == 8 &&
               RLAP_DOOR_RIDE_INGRESS_LIP == 9,
               "door anchor policy IDs drift");

#define COUNT_ACTION_ROW(...) + 1
enum { PROBED_ACTION_ROWS = 0 SG_ACTION_CONTRACT_ROWS(COUNT_ACTION_ROW) };
#undef COUNT_ACTION_ROW
_Static_assert(PROBED_ACTION_ROWS == SG_ACTION_COUNT, "missing action row");

#define ASSERT_ACTION_ROW(symbol, id, runtime_supported, default_provenance, \
                          provenance_mask, mode_mask, trait_mask, endpoint, \
                          suffix_anchor, preopen_anchor, ride_anchor, control, \
                          mechanism, effective_suffix, bias_policy, bias_ms, \
                          controller_revision, name, short_name, color) \
    _Static_assert(symbol == id, "action row ID drift"); \
    _Static_assert(((id == 7 || id >= 9) ? runtime_supported == 0 : \
                    runtime_supported == 1), "runtime support gate drift"); \
    _Static_assert((runtime_supported ? controller_revision > 0 : \
                    controller_revision == 0), "controller revision drift");
SG_ACTION_CONTRACT_ROWS(ASSERT_ACTION_ROW)
#undef ASSERT_ACTION_ROW

_Static_assert(RLR_OK == 0, "success reason drift");
_Static_assert(RLR_UNSUPPORTED_ACTIVATOR == 74, "activator reason drift");
_Static_assert(RLR_ACTION_TIMEOUT == 103, "timeout reason drift");
