from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BOT = (ROOT / "slipgate/sg_bot.h").read_text()
ARACH = (ROOT / "slipgate/sg_arach.c").read_text()
CLIENT = (ROOT / "slipgate/sg_client.c").read_text()
PMOVE = (ROOT / "p_client.c").read_text()
MAIN = (ROOT / "g_main.c").read_text()


def section(source: str, start: str, end: str) -> str:
    first = source.index(start)
    return source[first : source.index(end, first)]


def test_one_typed_current_position_owner() -> None:
    assert "sg_localization_subject_t localization_subject;" in BOT
    assert "sg_localized_player_state_t localized_state;" in BOT
    assert "\tint\t\t\tseed;" not in BOT
    assert "last_origin" not in BOT
    assert "seedless_active" not in BOT
    assert "bot->seed" not in ARACH
    assert "Think_TrackSeed" not in ARACH
    assert "Think_Seedless" not in ARACH


def test_bot_only_pmove_observation() -> None:
    pmove = section(PMOVE, "\t\t// perform a pmove", "\n\t\t// save results")
    bot_branch = section(pmove, "\t\tif (SG_OwnsBot(ent))", "\n\t\telse")
    human_branch = pmove[pmove.index("\n\t\telse") :]
    assert "SG_HostLawProductionPmove" in bot_branch
    assert "SG_BotLocalizationObservePmove(ent, &request, &result);" in bot_branch
    assert "SG_BotLocalizationObservePmove" not in human_branch
    assert "gi.Pmove (&pm);" in human_branch
    assert "SG_HumanSpeedPmoveBegin" in human_branch
    assert "SG_HumanSpeedPmoveEnd" in human_branch
    assert "SG_HumanTracePmove" in human_branch


def test_lifecycle_resets_and_neutral_recovery() -> None:
    assert CLIENT.count("SG_BotLocalizationReset(bot);") >= 2
    assert "SG_BotLocalizationFrameBegin(bot);" in ARACH
    assert "SG_BotLocalizationFrameEnd(&sg_bots[i]);" in ARACH
    assert "(void)SG_BotLocalizationProviderSet(NULL);" in ARACH
    assert "(void)SG_BotLocalizationProviderSet(NULL);" in MAIN
    recovery = section(
        ARACH,
        "\tif (SG_BotLocalizationCell(bot) < 0 ||",
        "\n\t/*\n\t * The precision case",
    )
    assert "StrategyInterrupt" in recovery
    assert "memset(&tc.cmd, 0, sizeof(tc.cmd));" in recovery
    assert "ClientThink(e, &tc.cmd);" in recovery
    assert "Cmd_Kill_f" not in recovery


if __name__ == "__main__":
    test_one_typed_current_position_owner()
    test_bot_only_pmove_observation()
    test_lifecycle_resets_and_neutral_recovery()
    print("bot localization integration checks passed")
