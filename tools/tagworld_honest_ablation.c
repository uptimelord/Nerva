// SPDX-License-Identifier: Apache-2.0
// One-at-a-time knob ablation for honest map-H frozen learn/eval.

#include "tagworld.h"

#include "nerva_config.h"
#include "nerva_engine.h"

#include <stdio.h>
#include <string.h>

typedef struct AblationKnob {
    const char *name;
    void (*apply)(TagWorldConfig *tw, NervaConfig *nc, int variant);
    int variant_count;
    const char *(*label)(int variant);
} AblationKnob;

typedef struct {
    double eval_sum;
    double base_sum;
    double learn_sum;
    int n;
} AblationAgg;

static void base_tw(TagWorldConfig *cfg) {
    tagworld_config_defaults(cfg);
    cfg->map_id = TAGWORLD_MAP_TOOL_H;
    cfg->honest = true;
    cfg->pure_feedback = true;
    cfg->online_frozen_eval = true;
    cfg->mode = TAGWORLD_MODE_ACTION;
    cfg->fast = true;
    cfg->online_learn_episodes = 1000u;
    cfg->online_eval_episodes = 200u;
    cfg->online_explore_pct = 15u;
    cfg->online_anneal_episodes = 50u;
    cfg->online_coverage_episodes = 0u;
    cfg->max_ticks = 64u;
}

static void base_nc(NervaConfig *nc) {
    *nc = nerva_config_default();
}

static const char *lbl_learn(int v) {
    static const char *l[] = {"200", "500", "1000", "2000", "5000"};
    return l[v];
}
static const char *lbl_eval(int v) {
    static const char *l[] = {"100", "200", "400"};
    return l[v];
}
static const char *lbl_explore(int v) {
    static const char *l[] = {"0", "5", "15", "30", "50"};
    return l[v];
}
static const char *lbl_anneal(int v) {
    static const char *l[] = {"0", "25", "50", "100", "200"};
    return l[v];
}
static const char *lbl_ticks(int v) {
    static const char *l[] = {"32", "64", "128"};
    return l[v];
}
static const char *lbl_ltp(int v) {
    static const char *l[] = {"8", "16", "32", "64"};
    return l[v];
}
static const char *lbl_ltd(int v) {
    static const char *l[] = {"-6", "-12", "-24", "-48"};
    return l[v];
}

static void apply_learn(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)nc;
    const uint32_t vals[] = {200u, 500u, 1000u, 2000u, 5000u};
    tw->online_learn_episodes = vals[v];
}
static void apply_eval(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)nc;
    const uint32_t vals[] = {100u, 200u, 400u};
    tw->online_eval_episodes = vals[v];
}
static void apply_explore(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)nc;
    const uint32_t vals[] = {0u, 5u, 15u, 30u, 50u};
    tw->online_explore_pct = vals[v];
}
static void apply_anneal(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)nc;
    const uint32_t vals[] = {0u, 25u, 50u, 100u, 200u};
    tw->online_anneal_episodes = vals[v];
}
static void apply_ticks(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)nc;
    const uint32_t vals[] = {32u, 64u, 128u};
    tw->max_ticks = vals[v];
}
static void apply_ltp(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)tw;
    const nerva_q8_8_t vals[] = {8, 16, 32, 64};
    nc->ltp_delta_q8_8 = vals[v];
}
static void apply_ltd(TagWorldConfig *tw, NervaConfig *nc, int v) {
    (void)tw;
    const nerva_q8_8_t vals[] = {-6, -12, -24, -48};
    nc->ltd_delta_q8_8 = vals[v];
}

static const AblationKnob kKnobs[] = {
    {"learn_eps", apply_learn, 5, lbl_learn},
    {"eval_eps", apply_eval, 3, lbl_eval},
    {"explore_pct", apply_explore, 5, lbl_explore},
    {"anneal_eps", apply_anneal, 5, lbl_anneal},
    {"max_ticks", apply_ticks, 3, lbl_ticks},
    {"ltp_delta", apply_ltp, 4, lbl_ltp},
    {"ltd_delta", apply_ltd, 4, lbl_ltd},
};

static const uint32_t kSeeds[] = {1u, 5u, 11u, 17u, 23u};

static int run_frozen(const TagWorldConfig *tw, const NervaConfig *nc, uint32_t seed,
                      TagWorldFrozenResult *out) {
    TagWorldConfig cfg = *tw;
    NervaConfig nerva = *nc;
    cfg.seed = seed;

    NervaEngine e;
    if (nerva_engine_init(&e, nerva) != 0) {
        return -1;
    }
    e.cfg.ltp_delta_q8_8 = nerva.ltp_delta_q8_8;
    e.cfg.ltd_delta_q8_8 = nerva.ltd_delta_q8_8;

    int rc = tagworld_run_frozen_result(&e, &cfg, out);
    nerva_engine_free(&e);
    return rc;
}

static void agg_add(AblationAgg *a, const TagWorldFrozenResult *r) {
    a->eval_sum += r->eval.escape_rate;
    a->base_sum += r->eval.baseline_escape_rate;
    a->learn_sum += r->learn.escape_rate;
    a->n++;
}

static void run_knob_variant(const TagWorldConfig *tw0, const NervaConfig *nc0,
                             const AblationKnob *knob, int variant, AblationAgg *out) {
    TagWorldConfig tw;
    NervaConfig nc;
    base_tw(&tw);
    base_nc(&nc);
    tw = *tw0;
    nc = *nc0;
    knob->apply(&tw, &nc, variant);

    memset(out, 0, sizeof(*out));
    for (size_t si = 0; si < sizeof(kSeeds) / sizeof(kSeeds[0]); ++si) {
        TagWorldFrozenResult result;
        if (run_frozen(&tw, &nc, kSeeds[si], &result) != 0) {
            continue;
        }
        agg_add(out, &result);
    }
}

static void run_component_ablation(const TagWorldConfig *tw0, const NervaConfig *nc0,
                                   FILE *out) {
    fprintf(out, "\n=== component ablation (baseline config, 5 seeds) ===\n");
    fprintf(out, "seed,learned_eval,baseline,ablated_eval,learned_push,ablated_push\n");

    for (size_t si = 0; si < sizeof(kSeeds) / sizeof(kSeeds[0]); ++si) {
        TagWorldConfig tw = *tw0;
        NervaConfig nc = *nc0;
        tw.seed = kSeeds[si];

        NervaEngine e;
        if (nerva_engine_init(&e, nc) != 0) {
            continue;
        }
        e.cfg.ltp_delta_q8_8 = nc.ltp_delta_q8_8;
        e.cfg.ltd_delta_q8_8 = nc.ltd_delta_q8_8;

        TagWorldNerva tn;
        TagWorldFrozenResult result;
        if (tagworld_run_frozen_result(&e, &tw, &result) != 0) {
            nerva_engine_free(&e);
            continue;
        }
        tagworld_nerva_init(&e, &tn);
        tagworld_ablate_learned_policy_edges(&e, &tn);

        TagWorldMetrics ablated;
        if (tagworld_run_frozen_eval_only(&e, &tn, &tw, &ablated) != 0) {
            nerva_engine_free(&e);
            continue;
        }

        fprintf(out, "%u,%.4f,%.4f,%.4f,%llu,%llu\n", kSeeds[si], result.eval.escape_rate,
                result.eval.baseline_escape_rate, ablated.escape_rate,
                (unsigned long long)result.eval.action_push_doorway_count,
                (unsigned long long)ablated.action_push_doorway_count);
        nerva_engine_free(&e);
    }
}

int main(void) {
    TagWorldConfig tw0;
    NervaConfig nc0;
    base_tw(&tw0);
    base_nc(&nc0);

    FILE *out = stdout;
    fprintf(out, "honest_map_h_ablation_oat,seeds=1,5,11,17,23\n");
    fprintf(out, "baseline: learn=1000 eval=200 explore=15 anneal=50 ticks=64 ltp=16 ltd=-12\n");
    fprintf(out, "knob,variant,label,eval_mean,baseline_mean,delta_mean,learn_mean,seeds\n");

    AblationAgg baseline;
    memset(&baseline, 0, sizeof(baseline));
    for (size_t si = 0; si < sizeof(kSeeds) / sizeof(kSeeds[0]); ++si) {
        TagWorldFrozenResult result;
        if (run_frozen(&tw0, &nc0, kSeeds[si], &result) == 0) {
            agg_add(&baseline, &result);
        }
    }
    if (baseline.n > 0) {
        fprintf(out, "BASELINE,-,default,%.4f,%.4f,%.4f,%.4f,%d\n",
                baseline.eval_sum / baseline.n, baseline.base_sum / baseline.n,
                (baseline.eval_sum - baseline.base_sum) / baseline.n,
                baseline.learn_sum / baseline.n, baseline.n);
    }

    double best_delta = -1.0;
    char best_knob[32] = "";
    char best_label[24] = "";

    for (size_t ki = 0; ki < sizeof(kKnobs) / sizeof(kKnobs[0]); ++ki) {
        const AblationKnob *knob = &kKnobs[ki];
        for (int v = 0; v < knob->variant_count; ++v) {
            AblationAgg agg;
            run_knob_variant(&tw0, &nc0, knob, v, &agg);
            if (agg.n == 0) {
                continue;
            }
            double eval_m = agg.eval_sum / agg.n;
            double base_m = agg.base_sum / agg.n;
            double delta_m = eval_m - base_m;
            double learn_m = agg.learn_sum / agg.n;
            fprintf(out, "%s,%d,%s,%.4f,%.4f,%.4f,%.4f,%d\n", knob->name, v, knob->label(v),
                    eval_m, base_m, delta_m, learn_m, agg.n);
            if (delta_m > best_delta) {
                best_delta = delta_m;
                snprintf(best_knob, sizeof(best_knob), "%s=%s", knob->name, knob->label(v));
                snprintf(best_label, sizeof(best_label), "%s", knob->label(v));
                (void)best_label;
            }
        }
    }

    fprintf(out, "\nBEST_DELTA_KNOB,%s,delta=%.4f\n", best_knob, best_delta);
    run_component_ablation(&tw0, &nc0, out);
    return 0;
}
