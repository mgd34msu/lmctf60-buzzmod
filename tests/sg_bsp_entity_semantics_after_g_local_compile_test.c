#include "../g_local.h"
#include "../slipgate/sg_bsp_entity_semantics.h"

typedef char sg_bsp_entity_semantics_after_g_local_compile_probe[
	sizeof(sg_bsp_entity_semantics_t) != 0U ? 1 : -1];

#ifndef world
#error "sg_bsp_entity_semantics.h must restore g_local.h's world macro"
#endif
