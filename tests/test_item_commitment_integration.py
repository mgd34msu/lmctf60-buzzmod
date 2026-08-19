import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ItemCommitmentIntegrationTest(unittest.TestCase):
    def text(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_successful_touch_is_the_authoritative_pickup_signal(self):
        source = self.text("g_items.c")
        start = source.index("void Touch_Item(")
        end = source.index("void drop_temp_touch", start)
        touch = source[start:end]
        pickup = touch.index("taken = ent->item->pickup(ent, other);")
        note = touch.index("SG_NoteItemTaken(other, ent);", pickup)
        targets = touch.index("G_UseTargets(ent, other);", note)
        accepted = touch.index("if (!taken)\n\t\treturn;", targets)
        release = touch.index("G_FreeEdict(ent);", accepted)
        self.assertLess(pickup, note)
        self.assertLess(note, targets)
        self.assertLess(targets, accepted)
        self.assertLess(accepted, release)

    def test_major_static_pickup_closes_lead_before_belief_bookkeeping(self):
        source = self.text("slipgate/sg_caco.c")
        start = source.index("void SG_NoteItemTaken(")
        end = source.index("static ", start)
        body = source[start:end]
        dropped = body.index("DROPPED_ITEM | DROPPED_PLAYER_ITEM")
        major = body.index("SG_ChatItemMajor(item)")
        lead = body.index("Lead_NoteItemTaken(taker, item);")
        communication = body.index(
            "disposition != SG_ITEM_PICKUP_COMMIT_AND_COMMUNICATE", lead
        )
        team = body.index("takerteam = taker->client->ctf.teamnum;")
        self.assertLess(dropped, major)
        self.assertLess(major, lead)
        self.assertLess(lead, communication)
        self.assertLess(communication, team)
        self.assertIn("SG_ItemPickupDisposition(true,", body)
        self.assertNotIn("pers.inventory", body)

    def test_spawned_phase_keeps_the_route_and_bounds_clock_inference(self):
        source = self.text("slipgate/sg_lead.c")
        active = source[source.index("/* ------------------------------------------------ an errand in progress */") :]
        spawn = active.index("if (b->believed_up)")
        transition = active.index("bot->lead_state = SG_LEAD_SPAWNED;", spawn)
        inference = active.index("SG_LEAD_INFER_GRACE", transition)
        waiting_clock = active.index(
            "if (bot->lead_state == SG_LEAD_WAITING &&\n"
            "\t\t    b->believed_respawn_time <= 0.0f)",
            inference,
        )
        flood = active.index("if (!Lead_Flood(", waiting_clock)
        self.assertLess(spawn, transition)
        self.assertLess(transition, inference)
        self.assertLess(inference, waiting_clock)
        self.assertLess(waiting_clock, flood)

    def test_wait_standoff_releases_when_the_item_spawns(self):
        source = self.text("slipgate/sg_descend.c")
        self.assertIn(
            "if (bot->lead_ent > 0 && bot->lead_state == SG_LEAD_WAITING &&\n"
            "\t    goal_field[bot->seed] < SG_LEAD_STANDOFF)",
            source,
        )

    def test_terminal_homing_precedes_role_fallback(self):
        source = self.text("slipgate/sg_move.c")
        pickup = source.index("if (Lead_PickupTarget(bot, aim))")
        escort = source.index("if (!have_aim && ordered_escort)", pickup)
        carry = source.index("else if (!have_aim && role == SG_ROLE_CARRY)", escort)
        flag = source.index("SG_FlagStand(team, true)", carry)
        self.assertLess(pickup, escort)
        self.assertLess(escort, carry)
        self.assertLess(carry, flag)

    def test_only_plain_run_authority_is_retired(self):
        source = self.text("slipgate/sg_lead.c")
        start = source.index("static void Lead_RetireRoute")
        end = source.index("void Lead_Abort", start)
        body = source[start:end]
        self.assertIn("r->links[bot->commit_link].action == RL_RUN", body)
        self.assertIn("bot->commit_link = -1;", body)
        self.assertNotIn("RL_TELE", body)
        self.assertNotIn("RL_DOOR", body)
        self.assertNotIn("RL_LIFT", body)

    def test_flag_intelligence_preempts_cosmetic_chat_but_stamps_budget(self):
        source = self.text("slipgate/sg_chat.c")
        start = source.index("qboolean SG_ChatSayTeam(")
        end = source.index("static qboolean Chat_SayEx", start)
        body = source[start:end]
        admission = body.index("SG_ChatTopicBlocksOnBotGap(topic)")
        topic_gate = body.index("SG_ChatTopicStampsBotGap(topic)", admission)
        emit = body.index('SG_BotClientCommand(cl, "say_team"', topic_gate)
        stamp = body.index("chat_bot[cl].next_team =", emit)
        self.assertLess(admission, topic_gate)
        self.assertLess(topic_gate, emit)
        self.assertLess(emit, stamp)
        self.assertIn("level.time < chat_bot[cl].next_team", body)
        self.assertIn("level.time < chat_teamsaid", body)

        caco = self.text("slipgate/sg_caco.c")
        speak = caco[caco.index("static void Caco_Speak(void)") :]
        self.assertIn(
            "SG_ChatSayTeam(sp, c->line, SG_CHAT_TOPIC_CACO)", speak
        )


if __name__ == "__main__":
    unittest.main()
