#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'
production_sources='slipgate/sg_rune_compact_wire.c slipgate/sg_rune_compact_model.c slipgate/sg_rune_compact_source_surface_catalog.c slipgate/sg_rune_compact_weapon_catalog.c slipgate/sg_rune_compact_analytic.c slipgate/sg_rune_compact_static.c'
emitter_sources="tests/sg_rune_compact_wire_test.c $production_sources"
reader_sources="tools/runecompactread.c $production_sources slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c"

cd "$repo_dir"
gcc $strict -I. $emitter_sources -Wl,--wrap=calloc -lm \
	-o "$tmp_dir/emit-rune"
gcc $strict -I. tests/runecompactread_fixture.c \
	-o "$tmp_dir/runecompactread-fixture"
"$tmp_dir/emit-rune" --emit "$tmp_dir/production.rune"
"$tmp_dir/emit-rune" --emit-invalid-provenance \
	"$tmp_dir/invalid-provenance.rune"
"$tmp_dir/runecompactread-fixture" "$tmp_dir/production.rune" \
	"$tmp_dir/truncated.rune" "$tmp_dir/trailing.rune" \
	"$tmp_dir/noncanonical.rune"

expected='{"identity":{"bsp_sha256":"5a00000000000000000000000000000000000000000000000000000000000000","bsp_bytes":1024,"bsp_checksum":257,"entity_crc32":258,"entity_semantics_id":514,"physics_abi_id":771,"collision_law_id":12337,"pmove_law_id":12338,"gravity_law_id":12339,"hook_law_id":12340,"mechanism_law_id":772,"weapon_law_id":773,"construction_id":774,"schema_id":1028,"producer_identity":5787775626031351122,"weapon_profile_catalog_id":13132152608774997061,"source_counts":{"model_count":2,"leaf_count":3,"area_count":4,"plane_count":5,"brush_count":2,"brush_side_count":2,"entity_count":32},"standing_hull":{"mins":[-128,-128,-192],"maxs":[128,128,256]},"crouching_hull":{"mins":[-128,-128,-192],"maxs":[128,128,128]},"physics":{"gravity_bits":1120403456,"ground_acceleration_bits":1092616192,"air_acceleration_bits":1065353216,"water_acceleration_bits":1082130432,"hook_acceleration_bits":1148846080,"external_acceleration_bits":1150681088,"water_drag_bits":1056964608,"max_velocity_bits":1145569280,"frame_ms":8,"substep_ms":1}},"counts":{"identity":1,"cells":2,"facets":2,"incidences":3,"cell_incidences":3,"vertices":4,"portals":1,"movement_capabilities":2,"movement_states":2,"movement_fibers":2,"movement_hook_targets":1,"movement_fiber_function_refs":24,"movement_angular_schedules":0,"movement_runtime":1,"response_fragments":2,"response_halfspaces":0,"response_patches":2,"response_target_vertices":6,"response_splits":2,"response_facts":2,"response_candidate_groups":1,"response_source_endpoint_groups":1,"response_source_endpoint_members":2,"response_target_endpoint_groups":1,"response_target_endpoint_members":2,"response_seal":1,"static_occluders":1,"weapon_profiles":14,"weapon_kernels":28,"weapon_function_refs":553,"weapon_attachments":4,"weapon_relation_spans":4,"weapon_relation_refs":4,"analytic_functions":8,"analytic_input_dimensions":0,"analytic_constants":8,"analytic_affines":0,"analytic_affine_slopes":0,"analytic_polynomials":0,"analytic_polynomial_coefficients":0,"analytic_ballistics":0,"analytic_piecewise":0,"analytic_piecewise_clauses":0,"mechanisms":2,"mechanism_controllers":2,"mechanism_edges":2,"transitions":1,"landmarks":2,"landmark_cells":2,"facet_annotations":1,"portal_mechanisms":1,"source_surfaces":3,"source_surface_vertices":12,"mechanism_authorities":1,"mechanism_authority_controllers":1,"mechanism_authority_topology_edges":1,"mechanism_authority_transitions":1}}'

expected="${expected%??},\"mechanism_authority_transition_static_indices\":1,\"static_transition_authority_indices\":1}}"

reject() {
	reader=$1
	image=$2
	diagnostic=$3
	if "$reader" "$image" > "$tmp_dir/reject.out" \
		2> "$tmp_dir/reject.err"
	then
		printf '%s\n' "reader accepted invalid artifact: $image" >&2
		return 1
	fi
	if test -s "$tmp_dir/reject.out" ||
		! grep -F "$diagnostic" "$tmp_dir/reject.err" > /dev/null
	then
		printf '%s\n' "reader did not report artifact rejection: $image" >&2
		return 1
	fi
}

exercise() {
	reader=$1
	actual=$("$reader" "$tmp_dir/production.rune")
	if test "$actual" != "$expected"
	then
		printf '%s\n' 'reader JSON summary changed' >&2
		return 1
	fi
	reject "$reader" "$tmp_dir/truncated.rune" 'truncated image'
	reject "$reader" "$tmp_dir/trailing.rune" 'invalid wire format'
	reject "$reader" "$tmp_dir/noncanonical.rune" 'nonzero reserved byte'
	reject "$reader" "$tmp_dir/invalid-provenance.rune" \
		'invalid compact model'
	if "$reader" > /dev/null 2> "$tmp_dir/usage.err" ||
		! test -s "$tmp_dir/usage.err"
	then
		printf '%s\n' 'reader accepted a missing artifact path' >&2
		return 1
	fi
}

for cc in gcc clang
do
	$cc $strict -I. $reader_sources -lm -o "$tmp_dir/runecompactread-$cc"
	exercise "$tmp_dir/runecompactread-$cc"
done

printf '%s\n' 'run_runecompactread_test: ok'
