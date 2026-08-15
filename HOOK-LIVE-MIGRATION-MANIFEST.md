# Ordinary live HOOK migration manifest

- Base: `slipgate` at `1195d78b5d8113f4d839bc3a48be750dda15df0e`.
- Private accepted source bundle:
  `/var/tmp/lmctf6-hook-rebase.ryzIjP`, SHA-256
  `e4d9781a175bdd3f9902079d039f0a8ab1f9b57e5f0d5976737e4859217a5e73`.
- Private live-acceptance evidence:
  `/var/tmp/lmctf6-hook-live-accept.czK6jN/PRIVATE_LIVE_ACCEPTANCE_FINAL_MANIFEST.txt`,
  SHA-256 `6f5bd3ca838d8064f9a8bb2fe1d7c91c6753d4491a015775908fbcd00b9d413f`.
- Evidence scope: ordinary graph HOOK only; a private bot run observed 15 live
  adapter attach calls, 22 pull calls, and 15 release calls under source-free
  GDB probes. No production server/corpus run is claimed here.
- Pending: compound `DOOR_HOOK`, the full 181-map acceptance gate, and
  recomposition/revalidation of the separately accepted pre-HOOK DROP candidate.

## Exact promoted postimages

```text
06a0f391abc16d703e22b98ad91e96fda7b1010626a08252e15c00cb0c5b85df  GNUmakefile
2038a9e07806e5b507f314c702c73331875551986575d69614e17cd0ec616314  Makefile
fe96fd1f93eb2f4f1bf78a44b0ff5cb2e4ca71d1ebdceea0e11340076e959685  gravity.vcxproj
36629075a022571433faa0944478c2d841658007de4459457d865b93073f58d4  gravity.vcxproj.filters
64a10745b1fb59c218c0e050bfa8ff8a45be1d4e091415cf69eff6ebccd8d50c  p_view.c
83ce2976720790432d81c1e79ab87f42d6fbf807c70ded306a1faf0d6bbc7d82  slipgate/sg_arach.c
bfb33be8894413c19ab28a8d4fbf3dcfac87ba30d68896c0359ae48d9168f99d  slipgate/sg_bot.h
0b6bf91c413537d4ab7bbd551367b0161cfb58feb66b8276ffaad73e761eda74  slipgate/sg_client.c
2d0bd8e6be1c5e3801d891cec43f4378633dc6f20798f13d4fc728c173500046  slipgate/sg_move.c
7d25d7867b587b2d5925835be565519160d98152a1bd10c19f6f3a635bb5197e  slipgate/sg_move.h
3d048f71cb9fdbabb9d0547286cf3e4f361231cce1fd30c4c12a51dce54b41d7  slipgate/sg_replay.c
58c3ac99f73dbe52d93a0f107fe149ef6c1322d07a899f40d16d540a5c2429c9  slipgate/sg_replay.h
1cb8acc5daca209f794643915dc757d7d700dd9377c65730fed200443fa3e10a  slipgate/sg_hook_live.c
190c0936788fd6d3966f8b34eb4eb0effb6da4f6a4b300640806d1abed606282  slipgate/sg_hook_live.h
acd026556917ef572628fe3e8947f2bbc355f7a6dcd91b73fecb5205abca91dc  tests/sg_hook_live_test.c
0be1dcaafa4b9edf90563833470a7049ee01d5d17f8f45401ba9f1d3e49853ab  tests/test_hook_live_integration.py
symlink  sg_hook_live.c -> slipgate/sg_hook_live.c
```
