# TagWorld tool maps (v1.2)

Train maps emit abstract adapter events; Nerva scores actions from relation structure only.

| Map | Role | Layout |
|-----|------|--------|
| A (`TOOL_A`, `tool_pressure`) | Train | Doorway chokepoint left |
| B (`TOOL_B`) | Train | Doorway chokepoint right |
| C (`TOOL_C`) | Train | Corridor bend chokepoint |
| D (`TOOL_D`) | Held-out eval | Same choke geometry as A (held out of train rotation) |
| E (`TOOL_E`) | Held-out eval | Same walls as A, block start south of doorway |
| F (`TOOL_F`) | Held-out eval | Same as A, safe zone at (1,5) |

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
