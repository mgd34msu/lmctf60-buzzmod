# Weapon effect profile boundary

The selected design keeps one immutable physical profile catalog and one
profile-free static-geometry query. The query carries the accepted artifact,
BSP, schema, source-set, and visibility identities plus RUNE cell/phase
references. A single visibility or occlusion result can therefore serve
several weapon families without copying geometry into per-weapon records.

One alternative put per-family visibility records in the catalog. It would
duplicate static geometry and bind weapon balance changes to RUNE construction.
Another kept every physical fact in one flat record. That made invalid mixes,
such as a BFG secondary using the core splash law, too easy to express. The
selected profile keeps ammo, timing, and splash laws in separate value types.
The shared query keeps the BSP authoritative and leaves weapon selection
downstream.

Static evidence never authorizes firing. Every catalog profile requires the
live boundary, and `SG_WeaponPrefireAllowed` accepts only a matching,
authenticated, accepted host trace from the exact muzzle and pose. Existing
combat callers remain unchanged.

The catalog records base physical laws through constants consumed by the host
fire paths themselves. The resolver applies deathmatch, `CTF_WEAP_BALANCE`,
quad, rail-match, and fast-switch inputs in host order, then binds the result to
explicit build and physics identities. An effect query rejects an unbound base
profile or an identity mismatch. Tactical
engagement ranges are not physical ray limits and are deliberately absent. The
catalog instead records the host's 8192-unit hitscan trace, projectile lifetime
or retirement facts where the host has one, collision half-extents, variable
projectile/ammo counts, and the BFG's distinct periodic 2048-unit penetrating
ray effect. Balanced rail spread traces remain separate from its penetrating
main ray. Splash records distinguish ordinary half-distance, balanced
normalized-linear, and BFG square-root kernels plus owner handling. Plasma
records the host's 10-cell ready threshold, 1-cell live-fire threshold, actual
debit, and global impact-time quad dependency without treating quad as a
shot-bound fact.
Armor, power-armor, friendly-fire, weapon-state, and inventory decisions remain
runtime inputs rather than being frozen into static geometry.
