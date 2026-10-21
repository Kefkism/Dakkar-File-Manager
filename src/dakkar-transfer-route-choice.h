/* Dakkar: deterministic destination-pane routing policy.
 * This small, dependency-free C unit is also compiled by the native
 * routing tests. Actual GTK/GFile object lifetime checks happen in the
 * caller (nemo-progress-ui-handler.c).
 */
#ifndef DAKKAR_TRANSFER_ROUTE_CHOICE_H
#define DAKKAR_TRANSFER_ROUTE_CHOICE_H
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    unsigned int match;    /* 3: receiving view; 2: exact GFile; 1: ancestor */
    size_t path_length;
    bool origin_window;
} DakkarTransferRouteRank;

/* 1 = candidate better, 0 = indistinguishable, -1 = worse.
 * Focus is deliberately NOT an input: it may change after job creation.
 * Two candidates with equal rank must not be arbitrarily assigned a tab. */
static inline int
dakkar_transfer_route_compare (DakkarTransferRouteRank candidate,
                              DakkarTransferRouteRank current)
{
    if (candidate.match != current.match)
        return candidate.match > current.match ? 1 : -1;
    if (candidate.path_length != current.path_length)
        return candidate.path_length > current.path_length ? 1 : -1;
    if (candidate.origin_window != current.origin_window)
        return candidate.origin_window ? 1 : -1;
    return 0;
}

/* This decision state is used unchanged by the GTK routing code and by
 * native C tests. No selected tab exists while ambiguity is true. */
typedef struct {
    DakkarTransferRouteRank best;
    bool has_candidate;
    bool ambiguous;
} DakkarTransferRouteChoice;

/* Returns true only when the caller must replace its current best slot. */
static inline bool
dakkar_transfer_route_consider (DakkarTransferRouteChoice *choice,
                              DakkarTransferRouteRank candidate)
{
    int comparison = choice->has_candidate ?
        dakkar_transfer_route_compare (candidate, choice->best) : 1;
    if (comparison > 0) {
        choice->best = candidate;
        choice->has_candidate = true;
        choice->ambiguous = false;
        return true;
    }
    if (comparison == 0)
        choice->ambiguous = true;
    return false;
}
#endif
