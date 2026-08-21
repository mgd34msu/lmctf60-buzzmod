import pathlib
import re
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
        rejected = touch.index("SG_NoteItemRejected(other, ent);", pickup)
        note = touch.index("SG_NoteItemTaken(other, ent);", pickup)
        targets = touch.index("G_UseTargets(ent, other);", note)
        accepted = touch.index("if (!taken)\n\t\treturn;", targets)
        release = touch.index("G_FreeEdict(ent);", accepted)
        self.assertLess(pickup, note)
        self.assertLess(pickup, rejected)
        self.assertLess(rejected, targets)
        self.assertLess(note, targets)
        self.assertLess(targets, accepted)
        self.assertLess(accepted, release)

    def test_rejected_touch_retires_only_the_exact_commitment_owner(self):
        source = self.text("slipgate/sg_lead.c")
        start = source.index("void Lead_NoteItemRejected(")
        end = source.index("qboolean Lead_PickupTarget", start)
        body = source[start:end]
        self.assertIn("bot->ent != taker", body)
        self.assertIn("bot->lead_ent != item_ent", body)
        self.assertIn('Lead_Abort(bot, "pickup rejected");', body)

        caco = self.text("slipgate/sg_caco.c")
        start = caco.index("void SG_NoteItemRejected(")
        end = caco.index("static void Caco_Age", start)
        reject = caco[start:end]
        self.assertIn("Lead_NoteItemRejected(taker, item);", reject)
        self.assertNotIn("SG_Chat", reject)

    def test_spawned_powerup_homing_requires_local_clear_pickup_access(self):
        lead = self.text("slipgate/sg_lead.c")
        source = self.text("slipgate/sg_pickup_target.c")
        start = source.index("qboolean SG_LocalPickupTarget")
        end = source.index("qboolean SG_WeaponPickupRouteEligible", start)
        pickup = source[start:end]
        self.assertIn("160.0f * 160.0f", pickup)
        self.assertIn("fabsf(delta[2]) > 64.0f", pickup)
        self.assertIn("sg_host.trace(self->s.origin, self->mins, self->maxs", pickup)
        self.assertIn("trace.startsolid || trace.allsolid", pickup)
        self.assertIn("trace.ent != item", pickup)
        self.assertIn(
            "return SG_LocalPickupTarget(bot->ent, item, target);",
            lead,
        )

    def test_selected_weapon_route_finishes_at_the_physical_pickup(self):
        move = self.text("slipgate/sg_move.c")
        start = move.index("/* last resort: the goal itself, by belief */")
        end = move.index("if (!have_aim && SG_OrderedEscortDirectAimAllowed", start)
        terminal = move[start:end]
        self.assertIn(
            "SG_WeaponPickupTarget(bot, tc->strike_weapon_pursuit, aim)",
            terminal,
        )
        self.assertLess(
            terminal.index("SG_WeaponPickupTarget"),
            terminal.index("Lead_PickupTarget"),
        )

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
        pickup = source.index("SG_WeaponPickupTarget(")
        escort = source.index(
            "if (!have_aim && SG_OrderedEscortDirectAimAllowed(", pickup)
        carry = source.index("else if (!have_aim && role == SG_ROLE_CARRY)", escort)
        flag = source.index("SG_FlagStand(team, true)", carry)
        self.assertLess(pickup, escort)
        self.assertLess(escort, carry)
        self.assertLess(carry, flag)

    def test_item_planner_uses_the_game_pickup_admission_law(self):
        items = self.text("g_items.c")
        admission = items[items.index(
            "qboolean G_PowerupPickupEligible"):items.index(
            "void Drop_General")]
        self.assertIn("ITEM_INDEX(ent->item)", admission)
        self.assertIn("skill->value", admission)
        self.assertIn("IT_STAY_COOP", admission)
        pickup = admission[admission.index("qboolean Pickup_Powerup"):]
        self.assertIn("if (!G_PowerupPickupEligible(ent, other))", pickup)

        lead = self.text("slipgate/sg_lead.c")
        self.assertGreaterEqual(
            lead.count("G_PowerupPickupEligible"), 3)
        self.assertIn('Lead_Abort(bot, "powerup capacity")', lead)

        runes = self.text("g_runes.c")
        rune_law = runes[runes.index(
            "qboolean G_RunePickupEligible"):runes.index(
            "// BUZZKILL - TOSS THING")]
        self.assertIn("other->client->rune == NULL", rune_law)
        self.assertIn("if (G_RunePickupEligible(ent, other))", rune_law)

        price = self.text("slipgate/sg_price.c")
        admission = price[price.index(
            "static qboolean Detour_IdentityItemEligible"):price.index(
            "float Detour_Value")]
        self.assertIn("G_PowerupPickupEligible(item, tc->e)", admission)
        self.assertIn("G_RunePickupEligible(item, tc->e)", admission)
        self.assertIn(
            "Caco_ItemBelievedRouteableFor(tc->team, item)", admission)
        fields = self.text("slipgate/sg_fields.c")
        class_build_start = fields.index(
            "static void Class_Build(rune_t *r, int cls)\n{")
        class_build = fields[class_build_start:
                             fields.index("#ifdef SG_FIELDS_TEST",
                                          class_build_start)]
        self.assertIn("Caco_ItemBelievedRouteable(e)", class_build)
        self.assertIn("Class_PerItem(cls)", class_build)
        goal = self.text("slipgate/sg_goal.c")
        ammo_start = goal.index("const int *SG_CollectibleAmmoField")
        ammo = goal[ammo_start:goal.index(
            "const int *SG_CollectibleArmorField", ammo_start)]
        self.assertIn("SG_CombatHeldAmmoTag(bot->ent)", ammo)
        self.assertIn("SG_AmmoRouteAdmission(item->item->tag, held_ammo_tag",
                      ammo)
        self.assertIn("G_AmmoPickupEligible(item, bot->ent)", ammo)
        weapon_start = goal.index("const int *SG_CollectibleWeaponField")
        weapon = goal[weapon_start:goal.index(
            "const int *SG_CollectibleHealthField", weapon_start)]
        self.assertIn("SG_CombatWeaponState(bot->ent, &weapon)", weapon)
        self.assertIn("SG_WeaponUpgradeRouteAdmission(weapon.available_tier,",
                      weapon)
        self.assertIn("SG_CombatWeaponPickupTier(item)", weapon)
        self.assertIn("SG_ItemGainSourceCost(gains[i], best_gain)", weapon)
        self.assertIn("sg_weapon_collectible_cost[bi][i] != costs[i]", weapon)
        defense = self.text("slipgate/sg_pickup_target.c")
        self.assertIn("SG_WeaponPickupRouteEligible(item, bot->ent)", defense)
        self.assertNotIn("SG_WeaponUpgradeRouteAdmission", defense)
        health_start = goal.index("const int *SG_CollectibleHealthField")
        health = goal[health_start:ammo_start]
        self.assertIn("G_HealthPickupGain(item, bot->ent)", health)
        self.assertIn("SG_HealthClassRouteAdmission(", health)
        self.assertIn('strcmp(item->classname, "item_health_mega") == 0',
                      health)
        self.assertIn("SG_MegaOn(), G_HealthPickupEligible(item, bot->ent)",
                      health)
        self.assertIn("SG_ItemGainSourceCost(gains[i], best_gain)", health)
        self.assertIn("sg_health_collectible_cost[bi][i] != costs[i]", health)
        armor_start = goal.index("const int *SG_CollectibleArmorField")
        armor = goal[armor_start:goal.index(
            "static qboolean DefenseSupplyFindTarget", armor_start)]
        self.assertIn("G_ArmorPickupGain(item, bot->ent)", armor)
        self.assertIn("SG_ItemGainSourceCost(gains[i], best_gain)", armor)
        self.assertIn("sg_armor_collectible_cost[bi][i] != costs[i]", armor)

        health_gain = items[items.index("int G_HealthPickupGain"):
                            items.index("qboolean Pickup_Health")]
        self.assertIn("ent->style & HEALTH_IGNORE_MAX", health_gain)
        self.assertIn("other->max_health - other->health", health_gain)
        armor_admission = items[items.index("qboolean G_ArmorPickupEligible"):
                                items.index("qboolean Pickup_Armor")]
        shard = armor_admission.index("ent->item->tag == ARMOR_SHARD")
        info = armor_admission.index("if (!ent->item->info)")
        self.assertLess(shard, info)
        self.assertIn("int G_ArmorPickupGain", armor_admission)
        detour = price[price.index("float Detour_Value"):price.index(
            "float Mega_Detour")]
        self.assertIn("Detour_IdentityItemEligible(tc, cls, kent)", detour)

        mega_admission = price[price.index(
            "static qboolean Detour_MegaEligible"):price.index(
            "float Detour_Value")]
        self.assertIn("Caco_ItemBelievedUpFor(tc->team, item)", mega_admission)
        self.assertIn("G_HealthPickupEligible(item, tc->e)", mega_admission)
        mega = price[price.index("float Mega_Detour"):price.index(
            "float Surface_At")]
        self.assertIn("Detour_MegaEligible(tc, kent)", mega)

        combat = self.text("slipgate/sg_combat.c")
        rune_entity = combat[combat.index(
            "static float Rune_EntityWorth"):combat.index(
            "void SG_CombatWeights")]
        self.assertIn("case RUNE_HASTE:", rune_entity)
        self.assertIn("case RUNE_DAMAGE:", rune_entity)
        self.assertIn("case RUNE_RESIST:", rune_entity)
        self.assertIn("case RUNE_REGEN:", rune_entity)
        self.assertIn("case RUNE_VAMP:", rune_entity)
        self.assertIn("class_worth * exact / best", rune_entity)
        self.assertIn("SG_RuneRouteWorth(tc->e,", detour)
        self.assertIn("item_worth / (1.0f +", detour)

    def test_rune_handoff_uses_effective_strike_escort_at_both_boundaries(self):
        for relative in ("slipgate/sg_goal.c", "slipgate/sg_descend.c"):
            source = self.text(relative)
            call = source.index("SG_RuneHandoffEligible(")
            body = source[call:call + 240]
            self.assertIn("tc->strike_active", body)
            self.assertIn("tc->escort_mission", body)
            self.assertIn("SG_RuneHandoffCarrierAllowed(team,", source)
            self.assertIn("ClientHasFlag(", source)
            self.assertIn("client < game.maxclients", source)

        policy = self.text("slipgate/sg_rune_handoff_policy.h")
        self.assertIn("if (strike_active)", policy)
        self.assertIn("return escort_mission;", policy)
        self.assertIn("carrier_team == team && carrying_flag", policy)
        self.assertIn("!receiver_has_rune", policy)

        descend = self.text("slipgate/sg_descend.c")
        self.assertIn("SG_RuneHandoffTossPathAllowed(carrier_distance,",
                      descend)
        self.assertIn("toss_path_clear = SG_CanSee(e, ce->s.origin,",
                      descend)

    def test_rune_handoff_route_blocks_optional_objective_replacement(self):
        source = self.text("slipgate/sg_goal.c")
        start = source.index("if (sg_cv.runetoss->value &&")
        end = source.index("if (SG_DefenseSupplyActive(bot))", start)
        handoff = source[start:end]
        self.assertIn("tc->rune_handoff_route = true;", handoff)
        self.assertIn(
            "SG_RuneHandoffAllowsOptional(tc->rune_handoff_route)",
            handoff,
        )
        self.assertIn(
            "!SG_RuneHandoffAllowsOptional(",
            handoff,
        )
        optional_guards = re.findall(
            r"SG_RuneHandoffAllowsOptional\(\s*tc->rune_handoff_route\s*\)",
            handoff,
        )
        self.assertEqual(len(optional_guards), 3)
        self.assertIn("route_pure = tc->rune_handoff_route;", handoff)

    def test_tactic_cache_is_wired_to_objective_identity(self):
        source = self.text("slipgate/sg_goal.c")
        start = source.index("static int tac_fields")
        end = source.index("if (SG_DefenseSupplyActive(bot))", start)
        tactics = source[start:end]
        root = tactics.index("SG_FieldKey(SG_Rune(), goal_field)")
        refresh = tactics.index("SG_TacticCacheNeedsRefresh(&cache)")
        publish = tactics.index("tac_goal[bi] = goal")
        flood = tactics.index("Field_Flood(SG_Rune(), tac_fields[bi]")

        self.assertLess(root, refresh)
        self.assertLess(refresh, publish)
        self.assertLess(publish, flood)

    def test_rune_handoff_route_retires_enemy_flag_pressure(self):
        goal = self.text("slipgate/sg_goal.c")
        move = self.text("slipgate/sg_move.c")
        bot = self.text("slipgate/sg_bot.h")
        self.assertRegex(bot, r"\bqboolean\s+rune_handoff_route\b")
        self.assertIn("tc->rune_handoff_route = false;", goal)
        self.assertIn("tc->rune_handoff_route = true;", goal)
        self.assertIn(
            "tc->strike_pressure = SG_RuneHandoffEnemyPressure(", goal
        )
        approach = goal[goal.index("qboolean Think_ApproachBand"):
                        goal.index("void Think_InterceptField")]
        self.assertIn("if (tc->rune_handoff_route)", approach)
        self.assertIn("bot->rally_since = 0.0f;", approach)
        terminal_start = move.index("/* last resort: the goal itself")
        terminal_end = move.index("CAPTURE THROUGH", terminal_start)
        terminal = move[terminal_start:terminal_end]
        handoff = terminal.index("tc->rune_handoff_route")
        pressure = terminal.index("tc->strike_pressure")
        self.assertLess(handoff, pressure)
        self.assertIn("SG_TerminalFieldSeed(SG_Rune(), goal_field", terminal)
        ribbon_start = move.index("SG_RouteRibbonAllowed(")
        ribbon = move[ribbon_start:move.index("l->action == RL_RUN",
                                              ribbon_start)]
        self.assertIn("tc->route_pure", ribbon)

    def test_mega_detour_finishes_at_the_selected_pickup(self):
        goal = self.text("slipgate/sg_goal.c")
        move = self.text("slipgate/sg_move.c")
        bot = self.text("slipgate/sg_bot.h")
        self.assertRegex(bot, r"\bint\s+mega_target_ent;")
        self.assertIn("tc->mega_target_ent = -1;", goal)
        self.assertIn(
            "Mega_Detour(tc, bot->seed, goal_field, &tc->mega_target_ent)",
            goal,
        )
        terminal = move[move.index("SG_WeaponPickupTarget(") :]
        pickup = terminal.index("SG_MegaPickupTarget(tc, aim)")
        flag = terminal.index("role == SG_ROLE_CARRY")
        self.assertLess(pickup, flag)

    def test_rune_handoff_binds_the_immediate_toss_and_submitted_view(self):
        source = self.text("slipgate/sg_descend.c")
        start = source.index("if (sg_cv.runetoss->value &&")
        end = source.index("if (role == SG_ROLE_CARRY)", start)
        handoff = source[start:end]
        aim = handoff.index("SG_RuneHandoffAim(")
        live_yaw = handoff.index("e->client->v_angle[YAW] = ry;", aim)
        live_pitch = handoff.index("e->client->v_angle[PITCH] = 0.0f;", aim)
        command_yaw = handoff.index("cmd->angles[YAW] = ANGLE2SHORT(ry)", aim)
        command_pitch = handoff.index("cmd->angles[PITCH] = ANGLE2SHORT(0.0f)", aim)
        toss = handoff.index("Drop_Rune(e, e->client->rune->item);", aim)
        self.assertLess(aim, live_yaw)
        self.assertLess(live_yaw, live_pitch)
        self.assertLess(live_pitch, command_yaw)
        self.assertLess(command_yaw, command_pitch)
        self.assertLess(command_pitch, toss)

        runes = self.text("g_runes.c")
        drop = runes[runes.index("void Drop_Rune("):runes.index(
            "void Toss_Rune(")]
        self.assertIn("ctf_TossEnt(ent, dropped);", drop)
        ctf = self.text("g_ctffunc.c")
        toss_body = ctf[ctf.index("void ctf_TossEnt("):ctf.index(
            "void Drop_Flag_Think")]
        self.assertIn("AngleVectors (startent->client->v_angle", toss_body)

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

    def test_delayed_callout_is_bound_to_the_queuing_client_life(self):
        source = self.text("slipgate/sg_caco.c")
        queue = source[
            source.index("static void Caco_Queue("):
            source.index("static void Caco_Speak(void)")
        ]
        speak = source[
            source.index("static void Caco_Speak(void)"):
            source.index("static void Caco_ScanFlags", source.index(
                "static void Caco_Speak(void)"))
        ]
        reset = source[
            source.index("void Caco_ResetClient("):
            source.index("float\t\tsg_caco_railshot[2][SG_DMG_CLIENTS]")
        ]
        self.assertIn(
            "c->speaker_ctfid = speaker->client->ctf.ctfid;", queue
        )
        self.assertIn(
            "SG_CalloutSpeakerCurrent(c->speaker_ctfid,", speak
        )
        self.assertIn("caco_callout[t][k].speaker == ci", reset)

        chat = self.text("slipgate/sg_chat.c")
        queue = chat[
            chat.index("static qboolean Chat_QueueArm("):
            chat.index("static void Chat_Queue(")
        ]
        flush = chat[
            chat.index("static void Chat_Flush(void)"):
            chat.index("static qboolean Chat_LocNameSkip")
        ]
        radio_queue = chat[
            chat.index("static void Chat_RadioQueue("):
            chat.index("static void Chat_RadioSay(")
        ]
        radio_say = chat[
            chat.index("static void Chat_RadioSay("):
            chat.index("static void Chat_RadioTaken", chat.index(
                "static void Chat_RadioSay("))
        ]
        self.assertIn(
            "q->speaker_ctfid = speaker->client->ctf.ctfid;", queue
        )
        self.assertIn("Chat_OurBot(sp) && Chat_Playing(sp)", flush)
        self.assertIn(
            "SG_CalloutSpeakerCurrent(held.speaker_ctfid,", flush
        )
        self.assertNotIn("if (!sp->client ||", flush)
        self.assertIn(
            "q->speaker_ctfid = speaker->client->ctf.ctfid;", radio_queue
        )
        self.assertIn(
            "SG_CalloutSpeakerCurrent(q->speaker_ctfid,", radio_say
        )

    def test_chat_texture_does_not_consume_gameplay_randomness(self):
        source = self.text("slipgate/sg_chat.c")
        policy = self.text("slipgate/sg_chat_random.h")

        self.assertNotIn("random()", source)
        self.assertNotIn("rand()", source)
        self.assertIn("uint32_t\trandom_state;", source)
        self.assertIn("SG_ChatRandomInitial(", source)
        self.assertIn("speaker->client->ctf.ctfid", source)
        self.assertIn("Chat_RandomUnit(speaker)", source)
        self.assertIn("Chat_RandomBounded(speaker", source)
        self.assertIn("SG_ChatRandomNext", policy)
        self.assertNotIn("rand(", policy)


if __name__ == "__main__":
    unittest.main()
