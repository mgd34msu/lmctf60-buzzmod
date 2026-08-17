#!/usr/bin/env python3
"""Focused integration pins for the native POV lock command and input order."""

from pathlib import Path
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]


def within(text: str, start: str, end: str) -> str:
    begin = text.index(start)
    finish = text.index(end, begin)
    return text[begin:finish]


def main() -> None:
    commands = (ROOT / "g_cmds.c").read_text()
    dispatch = within(commands, "void ClientCommand", "if (level.intermissiontime)")
    assert "if (POVLock_CommandNameIs(cmd))" in dispatch
    assert "Cmd_POVLock_f(ent);" in dispatch
    assert "Cmd_POVLock_f(ent);\n\t\treturn;" in dispatch
    assert "record pov" not in dispatch
    assert "svc_stufftext" not in dispatch
    assert "povlock starts one connected SG bot recording" in commands
    chase_command = within(
        commands, "static void Cmd_ChaseCam_f", "/*\n=================\nClientCommand"
    )
    assert "pov_record_active" in chase_command
    assert "ChaseNext(ent);" in chase_command
    assert "ChasePrev(ent);" in chase_command
    assert "GetChaseTarget(ent);" in chase_command
    assert "POVLock_Clear(ent);" in chase_command
    assert "record pov" not in chase_command
    assert 'Q_stricmp(cmd, "chasecam") == 0' in dispatch
    assert "Cmd_ChaseCam_f(ent);\n\t\treturn;" in dispatch

    client = (ROOT / "p_client.c").read_text()
    think = within(client, "void ClientThink", "void PingAlert")
    assert think.index("if (povlock_frame)") < think.index(
        "else if (ent->client->chase_target)"
    )
    assert "povrecord_frame = client->pov_record_active;" in think
    assert think.index("if (povrecord_frame)") < think.index(
        "POVLock_SuppressInput(client);"
    )
    assert "POVLock_SuppressInput(client);" in think
    assert "if (client->resp.spectator && !povrecord_frame)" in think
    attack = within(think, "if (client->latched_buttons & BUTTON_ATTACK)",
                    "if (client->resp.spectator && !povrecord_frame)")
    assert "if (client->povlock_active)" in attack
    assert "POVLock_Clear(ent);" in attack
    assert "if (other->client->povlock_active" in think

    put_client = within(client, "void PutClientInServer", "ClientBeginDeathmatch")
    assert put_client.index("POVLock_TargetRespawning(ent);") < put_client.index(
        "memset (client, 0, sizeof(*client));"
    )
    assert put_client.index("client->ctf.ctfid = unique_id++;") < put_client.index(
        "POVLock_TargetSpawned(ent);"
    )
    disconnect = within(client, "void ClientDisconnect", "edict_t\t*pm_passent")
    assert "POVLock_ViewerDisconnected(ent);" in disconnect
    rail_terminal = within(
        client, "qboolean POVLock_HandleRespawnTerminal", "void respawn"
    )
    assert "matchstate != MATCH_RAILGUN_INPLAY" in rail_terminal
    assert "POVLock_TargetWillNotRespawn(target);" in rail_terminal
    player_die = within(client, "void player_die", "void body_die")
    assert player_die.index("if (!self->deadflag)") < player_die.index(
        "POVLock_HandleRespawnTerminal(self)"
    ) < player_die.index("SG_CompoundGuardGamePlayerDie(self)")
    respawn = within(client, "void respawn", "void spectator_respawn")
    assert respawn.index("POVLock_HandleRespawnTerminal(self)") < respawn.index(
        "self->movetype = MOVETYPE_NOCLIP;"
    ) < respawn.index("return;")

    view = (ROOT / "p_view.c").read_text()
    end_frame = within(view, "void ClientEndServerFrame", "void Client_Show_High_Scores")
    assert end_frame.index("SV_CalcBlend (ent);") < end_frame.rindex(
        "POVLock_EndFrame(ent);"
    )
    assert end_frame.index("G_SetStats (ent);") < end_frame.index(
        "POVLock_EndFrame(ent);\n\t\treturn;"
    )
    assert end_frame.rindex("POVLock_EndFrame(ent);") > end_frame.rindex(
        "if (GamePaused())"
    )

    chase = (ROOT / "g_chase.c").read_text()
    command = within(chase, "qboolean POVLock_Command(", "qboolean POVLock_Update(")
    assert 'POVLock_Send(viewer, "record pov\\n");' not in command
    copy = within(chase, "static qboolean POVLock_CopyTargetState", "void POVLock_UpdateFollowers")
    assert copy.index("viewer->client->ps = target->client->ps;") < copy.index(
        "viewer->client->pov_record_sent = true;"
    ) < copy.index('POVLock_Send(viewer, "record pov\\n");')
    assert "int jump_held = viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD;" in copy
    assert "if (!viewer->client->pov_record_active)" in copy
    assert "viewer->client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;" in copy
    assert "viewer->client->ps.pmove.pm_flags |= jump_held;" in copy
    assert "follower_end_frame &&" in copy
    scrub = within(chase, "static void POVLock_ScrubOrdinaryViewer", "static void POVLock_ClearInstant")
    assert "POVLock_IsViewerEndpoint(viewer)" in scrub
    assert "viewer->client->pov_record_active" in scrub
    assert "memset(&viewer->client->ps, 0" in scrub
    assert "PM_SPECTATOR" in scrub
    assert "STAT_SPECTATOR" in scrub
    assert "viewer->movetype = MOVETYPE_NOCLIP;" in scrub
    assert "viewer->solid = SOLID_NOT;" in scrub
    clear_instant = within(chase, "static void POVLock_ClearInstant", "static void POVLock_Send")
    assert "ordinary_in_eyes" in clear_instant
    assert "POVLock_ScrubOrdinaryViewer(ent, jump_held);" in clear_instant
    ordinary_target = within(chase, "static qboolean Chase_TargetAllowed", "static void Chase_SetTarget")
    assert "!candidate->deadflag" in ordinary_target
    assert "SG_BotPOVIdentity(candidate, &sg_slot, &sg_instance)" in ordinary_target
    ordinary_set = within(chase, "static void Chase_SetTarget", "void UpdateChaseCam")
    assert "POVLock_ClearInstant(viewer);" in ordinary_set
    assert "viewer->client->povlock_active" in ordinary_set
    assert "!viewer->client->pov_record_active" in ordinary_set
    assert "pov_record_active = true" not in ordinary_set
    assert "pov_record_pending = true" not in ordinary_set
    legacy = within(chase, "static edict_t *POVLock_LegacyTarget", "void POVLock_Clear")
    assert "sg_instance != viewer->client->pov_record_sg_instance" in legacy
    retired = within(chase, "void POVLock_SGInstanceRetired", "void POVLock_StopAll")
    assert "viewer->client->pov_record_sg_slot == sg_slot" in retired
    assert "viewer->client->pov_record_sg_instance == instance_token" in retired
    assert "POVLock_ClearInstant(viewer);" in retired
    assert "viewer->client->chase_target = NULL;" in retired

    sg_client = (ROOT / "slipgate" / "sg_client.c").read_text()
    add_bot = within(sg_client, "qboolean SG_AddBotTeam", "int SG_RemoveBots")
    assert add_bot.index("ClientBegin(ent);") < add_bot.index(
        "SG_BotPOVInstanceAssign(&sg_bots[slot])"
    ) < add_bot.index("sg_bots[slot].active = true;")
    reset = within(sg_client, "static void BotSlot_Reset", "static const char *sg_names")
    assert "POVLock_SGInstanceRetired(slot, instance_token);" in reset
    assert reset.index("SG_BotPOVInstanceReset(bot);") < reset.index(
        "memset(bot, 0, sizeof(*bot));"
    )

    identity = (ROOT / "slipgate" / "sg_pov_identity.c").read_text()
    assert "qboolean SG_BotPOVInstanceAssign" in identity
    assert "qboolean SG_BotPOVIdentity" in identity
    assert "edict_t *SG_BotPOVResolve" in identity

    server_commands = (ROOT / "g_svcmds.c").read_text()
    pov_admin = within(server_commands, "static void SVCmd_POVRecord_f", "ServerCommand")
    assert "gi.argc() != 4" in pov_admin
    assert 'strcmp(gi.argv(2), "off") == 0' in pov_admin
    assert "POVRecord_AdminDirective(spectator, target, stop)" in pov_admin
    assert 'Q_stricmp (cmd, "povrecord")' in server_commands

    for make_name in ("GNUmakefile", "Makefile"):
        make_text = (ROOT / make_name).read_text()
        assert "pov-session-production-test" in make_text
        assert "pov_session_client_under_test" in make_text
        assert "pov_session_identity_under_test" in make_text

    namespace = {"msb": "http://schemas.microsoft.com/developer/msbuild/2003"}
    source_path = r"slipgate\sg_pov_identity.c"
    header_path = r"slipgate\sg_pov_identity.h"
    project_root = ET.parse(ROOT / "gravity.vcxproj").getroot()
    project_items = [
        item
        for item in project_root.findall(".//*[@Include]")
        if item.attrib["Include"] in (source_path, header_path)
    ]
    assert len(project_items) == 2
    assert len(
        project_root.findall(f".//msb:ClCompile[@Include='{source_path}']", namespace)
    ) == 1
    assert len(
        project_root.findall(f".//msb:ClInclude[@Include='{header_path}']", namespace)
    ) == 1

    filters_root = ET.parse(ROOT / "gravity.vcxproj.filters").getroot()
    filter_source = filters_root.findall(
        f".//msb:ClCompile[@Include='{source_path}']", namespace
    )
    filter_header = filters_root.findall(
        f".//msb:ClInclude[@Include='{header_path}']", namespace
    )
    assert len(filter_source) == 1
    assert len(filter_header) == 1
    assert filter_source[0].find("msb:Filter", namespace).text == "Source Files"
    assert filter_header[0].find("msb:Filter", namespace).text == "Header Files"

    main = (ROOT / "g_main.c").read_text()
    exit_level = within(main, "void ExitLevel", "void G_RunFrame")
    assert exit_level.index("POVLock_StopAll();") < exit_level.index(
        '"gamemap \\"%s\\"\\n"'
    ) < exit_level.index("gi.AddCommandString (command);")


if __name__ == "__main__":
    main()
