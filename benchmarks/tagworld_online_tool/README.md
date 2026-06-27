# v1.1.2 Online Tool Acquisition

**Status:** PROMOTED (`v1.1.2`) — see [experiments/v1.1.2_tagworld_online_tool_promote](../../experiments/v1.1.2_tagworld_online_tool_promote/README.md)

## Goal

Prove push selection rises from **episode outcome feedback** without direct action-context pretrain.

## Allowed pretrain (world dynamics only)

```text
BLOCK_AT_DOORWAY → PATH_BLOCKED
DOORWAY_OPEN     → PATH_OPEN
PATH_BLOCKED     → RUNNER_ESCAPED
PATH_OPEN        → RUNNER_CAUGHT
```

## Forbidden pretrain

```text
DOORWAY_OPEN / PATH_OPEN / SEEKER_NEAR → ACTION_PUSH_BLOCK_TO_DOORWAY
PATH_BLOCKED → ACTION_RUN_TO_SAFE
ACTION_PUSH → BLOCK_AT_DOORWAY context loops
Combined doorway/path → push context training
Post-pretrain policy snapshot restore during eval
```

## Promote if

- All action enable edges start at weight zero after dynamics-only pretrain
- Push doorway selection in last 20 episodes exceeds first 20 episodes (200-episode run)
- Trained last-20-episode escape exceeds first-20-episode escape (200-episode run)
- Seed 1 full-run escape rate ≥ random valid-action baseline
- No hardcoded map-specific push branch in `tagworld_nerva_select_action`
- Trace shows push acquired mid-run, not injected by pretrain

## Kill if

- Push selection does not rise over episodes after 200+ episodes
- Escape rate fails to beat random by ≥20 pp on any gate seed
- Action edges are nonzero after dynamics-only pretrain
- Policy depends on frozen snapshot restore (same mechanism as v1.1.1)

## Tests to add

- `test_tagworld_online_action_edges_zero_after_pretrain`
- `test_tagworld_online_push_increases_over_episodes`
- `test_tagworld_online_beats_random_baseline`

## Prior

- [v1.1.1 Tool-Action Pressure](../tagworld_tool_action/README.md) — pretrain-learned frozen policy
