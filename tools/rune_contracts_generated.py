"""Generated rune contract metadata. DO NOT EDIT."""

CONTRACT_SCHEMA_VERSION = 1
CONTRACT_CRC32 = 0x5c64bc3b
CONTRACT_SHA256 = 'fd7b4c2288845f9c3448aa82aeabfd8921feb5c943a6ac2f4b8abacd49f36ece'
ACTION_COUNT = 12
PROVENANCE_COUNT = 5
COMPOUND_MODE_COUNT = 3
ACTION_TRAIT_COUNT = 7
ACTION_TRAIT_ALL_MASK = 127
ENDPOINT_POLICY_COUNT = 7
WIRE_DIAGNOSTIC_COUNT = 27

RUNE_V3_MAGIC = 1162761554
RUNE_V3_VERSION = 3
RUNE_V3_LITTLE_ENDIAN_REQUIRED = 1
RUNE_V3_HEADER_BYTES = 128
RUNE_V3_SEED_BYTES = 16
RUNE_V3_LINK_BYTES = 44
RUNE_V3_MAP_NAME_BYTES = 64
RUNE_V3_HEADER_CRC_OFFSET = 60
RUNE_V3_NONCOMPOUND_TAIL_OFFSET = 28
RUNE_V3_NONCOMPOUND_TAIL_BYTES = 16
RUNE_V3_LINK_RESERVED_OFFSET = 43
RUNE_V3_MAX_SEEDS = 32768
RUNE_V3_MAX_LINKS = 262144
RUNE_V3_MIN_COST_MS = 1
RUNE_V3_MAX_COST_MS = 30000

RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED = 0
RUNE_PROOF_HOST_PHYSICS_ID_MIN = 1
RUNE_PROOF_GRAVITY_MIN = 1
RUNE_PROOF_GRAVITY_MAX = 32767
RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED = 1
RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED = 1
RUNE_PROOF_MAXVELOCITY_MIN = 800
RUNE_PROOF_FUNKY_GRAVITY_REQUIRED = 0
RUNE_PROOF_PMOVE_SUBSTEP_MS = 25
RUNE_PROOF_SERVER_FRAME_MS = 100
RUNE_PROOF_DROP_APPROACH_MS = 2500
RUNE_PROOF_DROP_TRAVEL_MS = 2000
RUNE_PROOF_DROP_TOTAL_MS = 4500
RUNE_PROOF_TOP_WINDOW_MARGIN_MS = 100
RUNE_PROOF_DOOR_ANCHOR_SCALE = 8
RUNE_PROOF_WORLD_FIXED_SCALE = 8
RUNE_PROOF_WORLD_FIXED_MIN = -32768
RUNE_PROOF_WORLD_FIXED_MAX = 32767
RUNE_PROOF_ANGLE_SHORT_UNITS = 65536
RUNE_PROOF_ANGLE_BYTE_UNITS = 256
RUNE_PROOF_FULL_TURN_DEGREES = 360
RUNE_PROOF_DECLARED_CONTROL_MARKER = 254
RUNE_PROOF_DROP_CONTROL_MARKER = 254
RUNE_PROOF_DAMAGING_FALL_DELTA = 30
RUNE_PROOF_DROP_RECOVERY_RADIUS = 96
RUNE_PROOF_DROP_RECOVERY_Z = 72
RUNE_PROOF_DROP_LIP_HORIZONTAL_MIN = 2
RUNE_PROOF_DROP_LIP_HORIZONTAL_MAX = 256
RUNE_PROOF_DROP_LIP_Z_FIXED = 64
RUNE_PROOF_DROP_LIP_Z_TOLERANCE_FIXED = 2
RUNE_PROOF_TELEPORT_SEED_REACH = 128
RUNE_PROOF_HOOK_BOLT_SPEED = 800
RUNE_PROOF_HOOK_FRAME_DISTANCE = 80
RUNE_PROOF_HOOK_MIN_RAY = 1
RUNE_PROOF_HOOK_MAX_RAY = 8192
RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES = 89
RUNE_PROOF_HOOK_CONTROL_SLACK = 24
RUNE_PROOF_WATER_HOOK_CONTROL_MARKER = 253
RUNE_PROOF_HOOK_DRY_SETTLE_MS = 1000
RUNE_PROOF_HOOK_WATER_SETTLE_MS = 1250
RUNE_PROOF_DOOR_APPROACH_HORIZONTAL_MAX = 320
RUNE_PROOF_DOOR_APPROACH_VERTICAL_MAX = 48
RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX = 768
RUNE_PROOF_DOOR_EGRESS_VERTICAL_MAX = 96
RUNE_PROOF_DOOR_TEAM_MEMBERS_MAX = 16

RL_PROVEN = 0
RL_OBSERVED = 1
RL_ADJUSTED = 2
RL_DECLARED = 3
RL_CONTRACTED = 4

RLCM_NONE = 0
RLCM_PREOPEN = 1
RLCM_RIDE = 2

SG_ACTF_OWNS_CONTROL = 1
SG_ACTF_BALLISTIC = 2
SG_ACTF_MAP_MECHANISM = 4
SG_ACTF_ATOMIC = 8
SG_ACTF_DOOR_LEASE = 16
SG_ACTF_SUPPRESS_LOCALIZATION = 32
SG_ACTF_EFFECTIVE_SUFFIX = 64

RLEP_ANY = 0
RLEP_DRY_BOTH = 1
RLEP_FROM_DRY = 2
RLEP_AT_LEAST_ONE_WATER = 3
RLEP_NOT_BOTH_WATER = 4
RLEP_FROM_WATER = 5
RLEP_WATER_TO_DRY = 6

RLAP_ZERO = 0
RLAP_RUN_WAYPOINT = 1
RLAP_DROP_LIP = 2
RLAP_HOOK_CONTROL = 3
RLAP_WORLD = 4
RLAP_TELEPORT_PAD = 5
RLAP_DOOR_WAIT = 6
RLAP_UNSUPPORTED = 7
RLAP_DOOR_PREOPEN_CONTACT = 8
RLAP_DOOR_RIDE_INGRESS_LIP = 9

RLCP_RUN = 0
RLCP_JUMP = 1
RLCP_DROP = 2
RLCP_HOOK = 3
RLCP_SWIM = 4
RLCP_DECLARED = 5
RLCP_UNSUPPORTED = 6

RLMP_NONE = 0
RLMP_DOOR_WORLD_FIXED_1_8 = 1

RLFB_NONE = 0
RLFB_FIXED = 1
RLFB_ROPE_CVAR = 2
RLFB_INHERIT = 3

RL_RUN = 0
RL_JUMP = 1
RL_DROP = 2
RL_HOOK = 3
RL_SWIM = 4
RL_LIFT = 5
RL_TELEPORT = 6
RL_ROCKETJUMP = 7
RL_DOOR = 8
RL_DOOR_DROP = 9
RL_DOOR_SWIM = 10
RL_DOOR_HOOK = 11

RLR_OK = 0
RLR_UNKNOWN_ACTION = 1
RLR_ACTION_DISABLED = 2
RLR_UNKNOWN_PROVENANCE = 3
RLR_PROVENANCE_FORBIDDEN = 4
RLR_BAD_INDEX = 5
RLR_SELF_LINK = 6
RLR_TOMBSTONE_ENDPOINT = 7
RLR_BAD_COST = 8
RLR_BAD_ENDPOINT_POLICY = 9
RLR_NONFINITE_ANCHOR = 10
RLR_BAD_ANCHOR_POLICY = 11
RLR_BAD_CONTROL_POLICY = 12
RLR_BAD_MODE = 13
RLR_NONZERO_TAIL = 14
RLR_NONZERO_RESERVED = 15
RLR_BAD_RUN_CONTROL = 32
RLR_BAD_JUMP_CONTROL = 33
RLR_BAD_DROP_CONTROL = 34
RLR_BAD_HOOK_CONTROL = 35
RLR_BAD_SWIM_CONTROL = 36
RLR_BAD_DECLARED_CONTROL = 37
RLR_BAD_TELEPORT_REACH = 38
RLR_BAD_DOOR_REACH = 39
RLR_BAD_MECHANISM_ANCHOR = 40
RLR_BAD_SWEEP_CLEAR = 41
RLR_MECHANISM_UNRESOLVED = 64
RLR_MECHANISM_AMBIGUOUS = 65
RLR_DOOR_TEAM_UNSAFE = 66
RLR_APPROACH_REPLAY_FAILED = 67
RLR_RIDE_REPLAY_FAILED = 68
RLR_SUFFIX_REPLAY_FAILED = 69
RLR_COST_MISMATCH = 70
RLR_CLEAR_MISMATCH = 71
RLR_TOP_WINDOW_SHORT = 72
RLR_SUPPORT_MISMATCH = 73
RLR_UNSUPPORTED_ACTIVATOR = 74
RLR_LIVE_SOURCE_MISMATCH = 96
RLR_LIVE_TOUCH_MISMATCH = 97
RLR_LIVE_DOOR_SET_MISMATCH = 98
RLR_LIVE_SUPPORT_MISMATCH = 99
RLR_LIVE_TIMING_MISMATCH = 100
RLR_LIVE_PERTURBED = 101
RLR_RECOVERY_UNSAFE = 102
RLR_ACTION_TIMEOUT = 103

RLW_OK = 0
RLW_INVALID_ARGUMENT = 1
RLW_IO_ERROR = 2
RLW_BAD_MAGIC = 3
RLW_UNSUPPORTED_VERSION = 4
RLW_BAD_HEADER_SIZE = 5
RLW_BAD_SEED_SIZE = 6
RLW_BAD_LINK_SIZE = 7
RLW_BAD_COUNTS = 8
RLW_BAD_FILE_SIZE = 9
RLW_BAD_HEADER_CRC = 10
RLW_BAD_PAYLOAD_CRC = 11
RLW_BAD_MAPNAME = 12
RLW_MAPNAME_MISMATCH = 13
RLW_BAD_ACTION_CONTRACT = 14
RLW_BAD_PHYSICS_LAW = 15
RLW_IDENTITY_UNAVAILABLE = 16
RLW_BSP_CHECKSUM_MISMATCH = 17
RLW_ENTITY_CRC_MISMATCH = 18
RLW_PHYSICS_ID_MISMATCH = 19
RLW_BAD_SEED_RECORD = 20
RLW_BAD_LINK_RECORD = 21
RLW_DUPLICATE_LINK = 22
RLW_BAD_ROUTE_OWNERSHIP = 23
RLW_BAD_OBJECTIVE_CORE = 24
RLW_ALLOCATION_FAILED = 25
RLW_BAD_SIDECAR = 26

ACTIONS = (
    {'id': 0, 'symbol': 'RL_RUN', 'name': 'RUN', 'short_name': 'RUN', 'color': '#9a9a9a', 'runtime_supported': 1, 'default_provenance': 0, 'provenance_mask': 15, 'mode_mask': 1, 'trait_mask': 0, 'endpoint_policy': 1, 'suffix_anchor_policy': 1, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 0, 'mechanism_policy': 0, 'effective_suffix': 0, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 1, 'symbol': 'RL_JUMP', 'name': 'JUMP', 'short_name': 'JUMP', 'color': '#00c8d7', 'runtime_supported': 1, 'default_provenance': 0, 'provenance_mask': 15, 'mode_mask': 1, 'trait_mask': 3, 'endpoint_policy': 1, 'suffix_anchor_policy': 0, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 1, 'mechanism_policy': 0, 'effective_suffix': 1, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 2, 'symbol': 'RL_DROP', 'name': 'DROP', 'short_name': 'DROP', 'color': '#e0c000', 'runtime_supported': 1, 'default_provenance': 0, 'provenance_mask': 1, 'mode_mask': 1, 'trait_mask': 3, 'endpoint_policy': 2, 'suffix_anchor_policy': 2, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 2, 'mechanism_policy': 0, 'effective_suffix': 2, 'field_bias_policy': 1, 'field_bias_ms': 150, 'controller_revision': 2},
    {'id': 3, 'symbol': 'RL_HOOK', 'name': 'HOOK', 'short_name': 'HOOK', 'color': '#ff8c1a', 'runtime_supported': 1, 'default_provenance': 0, 'provenance_mask': 1, 'mode_mask': 1, 'trait_mask': 1, 'endpoint_policy': 4, 'suffix_anchor_policy': 3, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 3, 'mechanism_policy': 0, 'effective_suffix': 3, 'field_bias_policy': 2, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 4, 'symbol': 'RL_SWIM', 'name': 'SWIM', 'short_name': 'SWIM', 'color': '#3d7dff', 'runtime_supported': 1, 'default_provenance': 0, 'provenance_mask': 1, 'mode_mask': 1, 'trait_mask': 33, 'endpoint_policy': 3, 'suffix_anchor_policy': 0, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 4, 'mechanism_policy': 0, 'effective_suffix': 4, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 5, 'symbol': 'RL_LIFT', 'name': 'LIFT', 'short_name': 'LIFT', 'color': '#8f5cff', 'runtime_supported': 1, 'default_provenance': 3, 'provenance_mask': 8, 'mode_mask': 1, 'trait_mask': 37, 'endpoint_policy': 0, 'suffix_anchor_policy': 4, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 5, 'mechanism_policy': 0, 'effective_suffix': 5, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 6, 'symbol': 'RL_TELEPORT', 'name': 'TELEPORT', 'short_name': 'TELE', 'color': '#00d18a', 'runtime_supported': 1, 'default_provenance': 3, 'provenance_mask': 8, 'mode_mask': 1, 'trait_mask': 37, 'endpoint_policy': 0, 'suffix_anchor_policy': 5, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 5, 'mechanism_policy': 0, 'effective_suffix': 6, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 7, 'symbol': 'RL_ROCKETJUMP', 'name': 'ROCKETJUMP', 'short_name': 'RJ', 'color': '#ff3b30', 'runtime_supported': 0, 'default_provenance': 0, 'provenance_mask': 15, 'mode_mask': 1, 'trait_mask': 3, 'endpoint_policy': 1, 'suffix_anchor_policy': 7, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 6, 'mechanism_policy': 0, 'effective_suffix': 7, 'field_bias_policy': 1, 'field_bias_ms': 900, 'controller_revision': 0},
    {'id': 8, 'symbol': 'RL_DOOR', 'name': 'DOOR', 'short_name': 'DOOR', 'color': '#ff66c4', 'runtime_supported': 1, 'default_provenance': 3, 'provenance_mask': 8, 'mode_mask': 1, 'trait_mask': 37, 'endpoint_policy': 1, 'suffix_anchor_policy': 6, 'preopen_mechanism_anchor_policy': 0, 'ride_mechanism_anchor_policy': 0, 'control_policy': 5, 'mechanism_policy': 0, 'effective_suffix': 8, 'field_bias_policy': 0, 'field_bias_ms': 0, 'controller_revision': 1},
    {'id': 9, 'symbol': 'RL_DOOR_DROP', 'name': 'DOOR_DROP', 'short_name': 'D_DROP', 'color': '#d4a600', 'runtime_supported': 0, 'default_provenance': 4, 'provenance_mask': 16, 'mode_mask': 6, 'trait_mask': 125, 'endpoint_policy': 2, 'suffix_anchor_policy': 2, 'preopen_mechanism_anchor_policy': 8, 'ride_mechanism_anchor_policy': 9, 'control_policy': 2, 'mechanism_policy': 1, 'effective_suffix': 2, 'field_bias_policy': 3, 'field_bias_ms': 0, 'controller_revision': 0},
    {'id': 10, 'symbol': 'RL_DOOR_SWIM', 'name': 'DOOR_SWIM', 'short_name': 'D_SWIM', 'color': '#5a9cff', 'runtime_supported': 0, 'default_provenance': 4, 'provenance_mask': 16, 'mode_mask': 2, 'trait_mask': 125, 'endpoint_policy': 5, 'suffix_anchor_policy': 0, 'preopen_mechanism_anchor_policy': 8, 'ride_mechanism_anchor_policy': 0, 'control_policy': 4, 'mechanism_policy': 1, 'effective_suffix': 4, 'field_bias_policy': 3, 'field_bias_ms': 0, 'controller_revision': 0},
    {'id': 11, 'symbol': 'RL_DOOR_HOOK', 'name': 'DOOR_HOOK', 'short_name': 'D_HOOK', 'color': '#ff5bbd', 'runtime_supported': 0, 'default_provenance': 4, 'provenance_mask': 16, 'mode_mask': 2, 'trait_mask': 125, 'endpoint_policy': 6, 'suffix_anchor_policy': 3, 'preopen_mechanism_anchor_policy': 8, 'ride_mechanism_anchor_policy': 0, 'control_policy': 3, 'mechanism_policy': 1, 'effective_suffix': 3, 'field_bias_policy': 3, 'field_bias_ms': 0, 'controller_revision': 0},
)
ACTION_BY_ID = {entry['id']: entry for entry in ACTIONS}
ACTION_NAMES = {entry['id']: entry['name'] for entry in ACTIONS}
ACTION_SHORT_NAMES = {entry['id']: entry['short_name'] for entry in ACTIONS}
ACTION_COLORS = {entry['id']: entry['color'] for entry in ACTIONS}

PROVENANCE_NAMES = {0: 'PROVEN', 1: 'OBSERVED', 2: 'ADJUSTED', 3: 'DECLARED', 4: 'CONTRACTED'}
MODE_NAMES = {0: 'NONE', 1: 'PREOPEN', 2: 'RIDE'}
REASON_SYMBOLS = {0: 'RLR_OK', 1: 'RLR_UNKNOWN_ACTION', 2: 'RLR_ACTION_DISABLED', 3: 'RLR_UNKNOWN_PROVENANCE', 4: 'RLR_PROVENANCE_FORBIDDEN', 5: 'RLR_BAD_INDEX', 6: 'RLR_SELF_LINK', 7: 'RLR_TOMBSTONE_ENDPOINT', 8: 'RLR_BAD_COST', 9: 'RLR_BAD_ENDPOINT_POLICY', 10: 'RLR_NONFINITE_ANCHOR', 11: 'RLR_BAD_ANCHOR_POLICY', 12: 'RLR_BAD_CONTROL_POLICY', 13: 'RLR_BAD_MODE', 14: 'RLR_NONZERO_TAIL', 15: 'RLR_NONZERO_RESERVED', 32: 'RLR_BAD_RUN_CONTROL', 33: 'RLR_BAD_JUMP_CONTROL', 34: 'RLR_BAD_DROP_CONTROL', 35: 'RLR_BAD_HOOK_CONTROL', 36: 'RLR_BAD_SWIM_CONTROL', 37: 'RLR_BAD_DECLARED_CONTROL', 38: 'RLR_BAD_TELEPORT_REACH', 39: 'RLR_BAD_DOOR_REACH', 40: 'RLR_BAD_MECHANISM_ANCHOR', 41: 'RLR_BAD_SWEEP_CLEAR', 64: 'RLR_MECHANISM_UNRESOLVED', 65: 'RLR_MECHANISM_AMBIGUOUS', 66: 'RLR_DOOR_TEAM_UNSAFE', 67: 'RLR_APPROACH_REPLAY_FAILED', 68: 'RLR_RIDE_REPLAY_FAILED', 69: 'RLR_SUFFIX_REPLAY_FAILED', 70: 'RLR_COST_MISMATCH', 71: 'RLR_CLEAR_MISMATCH', 72: 'RLR_TOP_WINDOW_SHORT', 73: 'RLR_SUPPORT_MISMATCH', 74: 'RLR_UNSUPPORTED_ACTIVATOR', 96: 'RLR_LIVE_SOURCE_MISMATCH', 97: 'RLR_LIVE_TOUCH_MISMATCH', 98: 'RLR_LIVE_DOOR_SET_MISMATCH', 99: 'RLR_LIVE_SUPPORT_MISMATCH', 100: 'RLR_LIVE_TIMING_MISMATCH', 101: 'RLR_LIVE_PERTURBED', 102: 'RLR_RECOVERY_UNSAFE', 103: 'RLR_ACTION_TIMEOUT'}
REASON_MESSAGES = {0: 'ok', 1: 'unknown action', 2: 'action disabled', 3: 'unknown provenance', 4: 'provenance forbidden', 5: 'bad seed index', 6: 'self link', 7: 'tombstone endpoint', 8: 'bad cost', 9: 'bad endpoint policy', 10: 'non-finite anchor', 11: 'bad anchor policy', 12: 'bad control policy', 13: 'bad compound mode', 14: 'nonzero legacy tail', 15: 'nonzero reserved field', 32: 'bad run control', 33: 'bad jump control', 34: 'bad drop control', 35: 'bad hook control', 36: 'bad swim control', 37: 'bad declared control', 38: 'bad teleport reach', 39: 'bad door reach', 40: 'bad mechanism anchor', 41: 'bad sweep-clear time', 64: 'mechanism unresolved', 65: 'mechanism ambiguous', 66: 'door team unsafe', 67: 'approach replay failed', 68: 'ride replay failed', 69: 'suffix replay failed', 70: 'cost mismatch', 71: 'sweep-clear mismatch', 72: 'TOP window too short', 73: 'support mismatch', 74: 'unsupported activator', 96: 'live source mismatch', 97: 'live trigger touch mismatch', 98: 'live door set mismatch', 99: 'live support mismatch', 100: 'live timing mismatch', 101: 'live action perturbed', 102: 'recovery unsafe', 103: 'action timeout'}

WIRE_DIAGNOSTICS = (
    {'id': 0, 'symbol': 'RLW_OK', 'message': 'ok'},
    {'id': 1, 'symbol': 'RLW_INVALID_ARGUMENT', 'message': 'invalid argument'},
    {'id': 2, 'symbol': 'RLW_IO_ERROR', 'message': 'I/O error'},
    {'id': 3, 'symbol': 'RLW_BAD_MAGIC', 'message': 'bad magic'},
    {'id': 4, 'symbol': 'RLW_UNSUPPORTED_VERSION', 'message': 'unsupported version'},
    {'id': 5, 'symbol': 'RLW_BAD_HEADER_SIZE', 'message': 'bad header size'},
    {'id': 6, 'symbol': 'RLW_BAD_SEED_SIZE', 'message': 'bad seed size'},
    {'id': 7, 'symbol': 'RLW_BAD_LINK_SIZE', 'message': 'bad link size'},
    {'id': 8, 'symbol': 'RLW_BAD_COUNTS', 'message': 'bad counts'},
    {'id': 9, 'symbol': 'RLW_BAD_FILE_SIZE', 'message': 'bad file size'},
    {'id': 10, 'symbol': 'RLW_BAD_HEADER_CRC', 'message': 'bad header CRC'},
    {'id': 11, 'symbol': 'RLW_BAD_PAYLOAD_CRC', 'message': 'bad payload CRC'},
    {'id': 12, 'symbol': 'RLW_BAD_MAPNAME', 'message': 'bad map name'},
    {'id': 13, 'symbol': 'RLW_MAPNAME_MISMATCH', 'message': 'map name mismatch'},
    {'id': 14, 'symbol': 'RLW_BAD_ACTION_CONTRACT', 'message': 'bad action contract'},
    {'id': 15, 'symbol': 'RLW_BAD_PHYSICS_LAW', 'message': 'bad physics law'},
    {'id': 16, 'symbol': 'RLW_IDENTITY_UNAVAILABLE', 'message': 'identity unavailable'},
    {'id': 17, 'symbol': 'RLW_BSP_CHECKSUM_MISMATCH', 'message': 'BSP checksum mismatch'},
    {'id': 18, 'symbol': 'RLW_ENTITY_CRC_MISMATCH', 'message': 'entity CRC mismatch'},
    {'id': 19, 'symbol': 'RLW_PHYSICS_ID_MISMATCH', 'message': 'physics ID mismatch'},
    {'id': 20, 'symbol': 'RLW_BAD_SEED_RECORD', 'message': 'bad seed record'},
    {'id': 21, 'symbol': 'RLW_BAD_LINK_RECORD', 'message': 'bad link record'},
    {'id': 22, 'symbol': 'RLW_DUPLICATE_LINK', 'message': 'duplicate link'},
    {'id': 23, 'symbol': 'RLW_BAD_ROUTE_OWNERSHIP', 'message': 'bad route ownership'},
    {'id': 24, 'symbol': 'RLW_BAD_OBJECTIVE_CORE', 'message': 'bad objective core'},
    {'id': 25, 'symbol': 'RLW_ALLOCATION_FAILED', 'message': 'allocation failed'},
    {'id': 26, 'symbol': 'RLW_BAD_SIDECAR', 'message': 'bad sidecar'},
)
WIRE_DIAGNOSTIC_BY_ID = {entry['id']: entry for entry in WIRE_DIAGNOSTICS}
WIRE_DIAGNOSTIC_SYMBOLS = {entry['id']: entry['symbol'] for entry in WIRE_DIAGNOSTICS}
WIRE_DIAGNOSTIC_MESSAGES = {entry['id']: entry['message'] for entry in WIRE_DIAGNOSTICS}

def action_contract(action):
    if type(action) is not int:
        raise TypeError('action must be an integer')
    try:
        return ACTION_BY_ID[action]
    except KeyError as exc:
        raise ValueError(f'unknown rune action: {action!r}') from exc


def has_trait(action, trait):
    if (type(trait) is not int or trait <= 0 or
            trait & (trait - 1) or trait & ~ACTION_TRAIT_ALL_MASK):
        raise ValueError(f'unknown action trait: {trait!r}')
    return bool(action_contract(action)['trait_mask'] & trait)


def effective_has_trait(action, trait):
    # Policy classification only; never dispatch execution through the suffix.
    return has_trait(effective_suffix(action), trait)


def is_runtime_supported(action):
    # Support belongs to the outer record, never to its effective suffix.
    return bool(action_contract(action)['runtime_supported'])


def allows_provenance(action, provenance):
    entry = action_contract(action)
    return (type(provenance) is int and 0 <= provenance < PROVENANCE_COUNT and
            bool(entry['provenance_mask'] & (1 << provenance)))


def allows_mode(action, mode):
    entry = action_contract(action)
    return (type(mode) is int and 0 <= mode < COMPOUND_MODE_COUNT and
            bool(entry['mode_mask'] & (1 << mode)))


def effective_suffix(action):
    seen = set()
    current = action
    while True:
        if current in seen:
            raise ValueError('effective suffix cycle')
        seen.add(current)
        suffix = action_contract(current)['effective_suffix']
        if suffix == current:
            return current
        current = suffix


def uses_hook_policy(action):
    return effective_suffix(action) == RL_HOOK


def field_bias_ms(action, rope_bias_ms):
    if type(rope_bias_ms) is not int:
        raise TypeError('rope_bias_ms must be an integer')
    current = action
    seen = set()
    while True:
        if current in seen:
            raise ValueError('field bias inheritance cycle')
        seen.add(current)
        entry = action_contract(current)
        policy = entry['field_bias_policy']
        if policy == RLFB_NONE:
            return 0
        if policy == RLFB_FIXED:
            return entry['field_bias_ms']
        if policy == RLFB_ROPE_CVAR:
            return max(0, rope_bias_ms)
        if policy != RLFB_INHERIT:
            raise ValueError(f'unknown field bias policy: {policy}')
        current = entry['effective_suffix']
