# TagWorld tool maps (v1.2)

Train maps emit abstract adapter events; Nerva scores actions from relation structure only.

| Map | Role | Layout |
|-----|------|--------|
| A (`TOOL_A`, `tool_pressure`) | Train | Doorway chokepoint left |
| B (`TOOL_B`) | Train | Doorway chokepoint right |
| C (`TOOL_C`) | Train | Corridor bend chokepoint |
| D (`TOOL_D`) | Held-out eval | West choke at (2,3), distinct from A/B/C |
| D' (`TOOL_D_ALIAS`) | Invariance eval | Same geometry/spawns as D, different map id |
| E (`TOOL_E`) | Held-out eval | A walls, block start south of doorway |
| F (`TOOL_F`) | Held-out eval | A walls, safe zone at (1,5) |

Abstract events (adapter output, not policy logic):

- `CHOKEPOINT_DETECTED`
- `SEEKER_ROUTE_USES_CHOKEPOINT`
- `BLOCK_CAN_REACH_CHOKEPOINT`
- `BLOCK_AT_CHOKEPOINT`
- `PATH_BLOCKED_BY_TOOL`

Policy path under generalization:

```text
SEEKER_ROUTE_USES_CHOKEPOINT + BLOCK_CAN_REACH_CHOKEPOINT
  → ACTION_PUSH_BLOCK_TO_DOORWAY
  → BLOCK_AT_CHOKEPOINT → PATH_BLOCKED_BY_TOOL
  → ACTION_RUN_TO_SAFE → ESCAPED
```
