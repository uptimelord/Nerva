# ChatWorld-lite Gate

**Status:** Draft
**World:** `worlds/chatworld/`

ChatWorld-lite tests whether Nerva can learn a small dialogue policy from surface text events,
candidate action traces, and outcome feedback.

This is not a TextWorld clone and not a chatbot demo. The adapter must not become the brain.

## Boundary

Allowed adapter emissions:

- surface tokens
- token positions
- punctuation
- speaker and turn boundary
- candidate response frames
- candidate memory write/read actions
- outcome feedback after response

Forbidden adapter emissions:

- intent labels
- slot labels
- semantic parse labels
- correct answer labels
- scripted responder decisions
- fallback unknown replies
- runtime LLM/API teacher

`RESP_UNKNOWN` is a normal response frame only when selected by learned graph support. If all response
scores are zero in frozen eval, the world must return `NO_SUPPORTED_RESPONSE`.

## Stage 1 - Surface Adapter Discipline

Promote if:

- tokenization of `my name is Ada` emits only surface token/position/speaker/turn events
- no node name contains `INTENT`, `SLOT`, `CORRECT`, `ANSWER_LABEL`, or `FACT_QUERY`
- response candidates are present but start at zero weight

Kill if:

- the adapter emits semantic labels such as `INTENT_PROVIDE_NAME` or `SLOT_NAME=Ada`
- the world can answer without a selected response or memory action trace

## Stage 2 - Tiny Dialogue Policy

Training language games:

- greeting: `hello` -> `RESP_GREET`
- acknowledgement: `thanks` -> `RESP_ACK`
- unknown handling: unsupported question -> `RESP_UNKNOWN`

Promote if:

- after training, frozen eval on held-out surface variants beats majority/random frame baseline by
  at least 20 percentage points
- frozen eval applies zero mutations
- zero-score eval produces `NO_SUPPORTED_RESPONSE`, not fallback `RESP_UNKNOWN`
- ablation of learned response edges drops eval behavior

Kill if:

- success requires scripted phrase matching at response time
- success depends on default unknown fallback

## Stage 3 - Surface-Bound Memory Actions

Training language games:

- identity memory: `my name is Ada`; `what is my name`
- fact memory: `poodle is dog`; `what is poodle`
- correction: `my name is Ada`; `no my name is Grace`; `what is my name`

Promote if:

- Nerva selects surface-bound `MEM_WRITE_PAIR` and `MEM_READ_PAIR` actions from candidate traces
- held-out utterances/entities answer correctly in frozen eval
- correction weakens or overrides the old memory path and answers the corrected value
- eval mutation count is zero
- fallback count is zero
- traces show input tokens -> fired edges -> selected action/frame -> outcome feedback -> queued
  mutations during training

Kill if:

- the adapter emits semantic memory keys directly
- answer rendering uses hidden correct labels
- runtime DeepSeek/LLM output is used to answer

## Claim Discipline

```text
Supported:     small dialogue policy and surface-bound memory/action-frame selection from surface
               text events plus outcome feedback in ChatWorld-lite.
Not supported: ChatGPT-like open-domain conversation; free-form prose generation; semantic parsing;
               broad language understanding; runtime LLM assistance.
Evidence:      gate metrics, unit tests, frozen eval traces, mutation counts, ablation.
Residuals:     candidate frames and zero-weight edge skeleton are predeclared; early memory binding
               is surface-bound and mechanical.
```
