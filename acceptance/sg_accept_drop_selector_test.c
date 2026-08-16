/* Host-free positive/negative exercise of the private C selector itself. */
#include <stdio.h>
#include <stdlib.h>

#define SG_ACCEPT_DROP 1
#define SG_ACCEPT_DROP_LEGACY_A 0
#include "slipgate/sg_accept_drop.c"

static int ReadFile(const char *path, unsigned char **bytes_out,
	size_t *size_out)
{
	FILE *file;
	long length;
	unsigned char *bytes;

	if (!path || !bytes_out || !size_out || !(file = fopen(path, "rb")))
		return 0;
	if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
	    fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}
	bytes = malloc((size_t)length);
	if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
	{
		free(bytes);
		fclose(file);
		return 0;
	}
	fclose(file);
	*bytes_out = bytes;
	*size_out = (size_t)length;
	return 1;
}

static int WireTailIsCanonicalDrop(const sg_rune_v3_link_t *link)
{
	int axis;

	if (!link || link->sweep_clear_ms != 0 || link->mode != RLCM_NONE ||
	    link->reserved != 0)
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (AcceptFloatBits(link->mechanism_anchor[axis]) != UINT32_C(0))
			return 0;
	return 1;
}

int main(int argc, char **argv)
{
	unsigned char *encoded = NULL;
	size_t encoded_size = 0;
	sg_rune_v3_header_t header;
	sg_rune_v3_seed_t *wire_seeds = NULL;
	sg_rune_v3_link_t *wire_links = NULL;
	uint64_t *keys = NULL;
	uint8_t *marks = NULL;
	sg_rune_v3_workspace_t workspace;
	rune_seed_t *native_seeds = NULL;
	rune_link_t *native_links = NULL;
	rune_t rune;
	rune_wire_diagnostic_t diagnostic;
	static const int expected_links[SG_ACCEPT_DROP_CASE_COUNT] = {
		7666, 15, 8556, 85, 66
	};
	int found[SG_ACCEPT_DROP_CASE_COUNT];
	int selector_index;
	int ok = 0;
	uint32_t index;

	if (argc != 2 || !ReadFile(argv[1], &encoded, &encoded_size))
	{
		fprintf(stderr, "usage: %s exact-v3-rune\n", argv[0]);
		goto done;
	}
	if (SG_RuneV3DecodeHeader(encoded, SG_RUNE_V3_HEADER_BYTES, &header) !=
	    RLW_OK)
		goto done;
	wire_seeds = calloc(header.num_seeds, sizeof(*wire_seeds));
	wire_links = calloc(header.num_links, sizeof(*wire_links));
	keys = calloc(header.num_links, sizeof(*keys));
	marks = calloc(header.num_seeds, sizeof(*marks));
	native_seeds = calloc(header.num_seeds, sizeof(*native_seeds));
	native_links = calloc(header.num_links, sizeof(*native_links));
	if (!wire_seeds || !wire_links || !keys || !marks || !native_seeds ||
	    !native_links)
		goto done;
	workspace.link_keys = keys;
	workspace.link_key_capacity = header.num_links;
	workspace.source_marks = marks;
	workspace.source_mark_capacity = header.num_seeds;
	diagnostic = SG_RuneV3Decode(encoded, encoded_size, NULL, &header,
	    wire_seeds, header.num_seeds, wire_links, header.num_links, &workspace);
	if (diagnostic != RLW_OK)
	{
		fprintf(stderr, "decode failed: %d\n", (int)diagnostic);
		goto done;
	}
	memset(&rune, 0, sizeof(rune));
	rune.v3_header = header;
	rune.hdr.magic = (int)header.magic;
	rune.hdr.version = header.version;
	rune.hdr.num_seeds = (int)header.num_seeds;
	rune.hdr.num_links = (int)header.num_links;
	memcpy(rune.hdr.mapname, header.map_name, sizeof(rune.hdr.mapname));
	rune.seeds = native_seeds;
	rune.links = native_links;
	for (index = 0; index < header.num_seeds; index++)
	{
		VectorCopy(wire_seeds[index].origin, native_seeds[index].origin);
		native_seeds[index].area_hint = wire_seeds[index].area_hint;
		native_seeds[index].flags = wire_seeds[index].flags;
	}
	for (index = 0; index < header.num_links; index++)
	{
		native_links[index].from = (int)wire_links[index].source;
		native_links[index].to = (int)wire_links[index].destination;
		native_links[index].action = wire_links[index].action;
		native_links[index].provenance = wire_links[index].provenance;
		native_links[index].min_speed = wire_links[index].min_speed;
		native_links[index].heading = wire_links[index].heading;
		native_links[index].heading_slack = wire_links[index].heading_slack;
		native_links[index].exit_speed = wire_links[index].exit_speed;
		native_links[index].cost_ms = wire_links[index].cost_ms;
		VectorCopy(wire_links[index].suffix_anchor, native_links[index].anchor);
	}
	for (selector_index = 0; selector_index < SG_ACCEPT_DROP_CASE_COUNT;
	     selector_index++)
	{
		const sg_accept_drop_selector_t *selector =
		    &accept_selectors[selector_index];

		found[selector_index] = AcceptFindLink(&rune, selector);
		if (found[selector_index] != expected_links[selector_index] ||
		    !WireTailIsCanonicalDrop(&wire_links[found[selector_index]]) ||
		    !AcceptFixtureMatches(&rune, selector))
		{
			fprintf(stderr, "selector mismatch case=%d link=%d fixture=%d\n",
			    selector_index + 1, found[selector_index],
			    AcceptFixtureMatches(&rune, selector));
			goto done;
		}
		native_links[found[selector_index]].provenance ^= 1U;
		if (AcceptFindLink(&rune, selector) != -1)
		{
			fprintf(stderr, "tuple mutation was not rejected case=%d\n",
			    selector_index + 1);
			goto done;
		}
		native_links[found[selector_index]].provenance ^= 1U;
		if (selector->fixture_kind != SGAD_FIXTURE_NONE)
		{
			rune_seed_t *fixture = &native_seeds[selector->fixture_seed];
			short saved_area = fixture->area_hint;
			short saved_flags = fixture->flags;
			float saved_origin = fixture->origin[0];

			fixture->area_hint ^= 1;
			if (AcceptFixtureMatches(&rune, selector))
			{
				fprintf(stderr, "fixture area mutation accepted case=%d\n",
				    selector_index + 1);
				goto done;
			}
			fixture->area_hint = saved_area;
			fixture->flags ^= 1;
			if (AcceptFixtureMatches(&rune, selector))
			{
				fprintf(stderr, "fixture flags mutation accepted case=%d\n",
				    selector_index + 1);
				goto done;
			}
			fixture->flags = saved_flags;
			fixture->origin[0] += 1.0f;
			if (AcceptFixtureMatches(&rune, selector))
			{
				fprintf(stderr, "fixture origin mutation accepted case=%d\n",
				    selector_index + 1);
				goto done;
			}
			fixture->origin[0] = saved_origin;
		}
	}
	rune.v3_header.header_crc32 ^= UINT32_C(1);
	if (AcceptFindLink(&rune, &accept_selectors[0]) != -1)
	{
		fprintf(stderr, "header mutation was not rejected\n");
		goto done;
	}
	printf("selector-selftest ok cases=%d links=%d/%d/%d/%d/%d "
	       "fixtures=exact payload=%08x header=%08x\n",
	    SG_ACCEPT_DROP_CASE_COUNT, found[0], found[1], found[2], found[3],
	    found[4], header.payload_crc32, header.header_crc32);
	ok = 1;

done:
	free(native_links);
	free(native_seeds);
	free(marks);
	free(keys);
	free(wire_links);
	free(wire_seeds);
	free(encoded);
	return ok ? 0 : 1;
}
