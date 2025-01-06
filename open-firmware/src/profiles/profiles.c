#include "profiles.h"

const assist_profile_t g_profiles[PROFILE_COUNT] = {
    /* id,   powerW, current dA, speed cap (0 = unlimited) */
    { 0, 550, 180, 250 }, /* commute */
    { 1, 750, 220, 280 }, /* trail */
    { 2, 650, 200, 220 }, /* cargo */
    { 3, 400, 140, 160 }, /* rain */
    { 4, 250, 100,  80 }, /* valet */
};

/*
 * Piecewise-linear assist curves per profile (bounded, no malloc).
 * Speed curves output a power limit (W). Cadence curves output a Q15
 * multiplier applied to the speed-derived limit.
 */
const assist_curve_profile_t g_assist_curves[PROFILE_COUNT] = {
    /* commute */
    {
        .speed_curve = {6, {
            {   0, 120 }, {  50, 180 }, { 100, 260 },
            { 150, 360 }, { 200, 450 }, { 250, 550 },
        }},
        .cadence_curve = {5, {
            {   0, 19661 }, {  50, 26214 }, {  80, 32768 },
            { 110, 26214 }, { 140, 19661 },
        }},
    },
    /* trail */
    {
        .speed_curve = {6, {
            {   0, 180 }, {  60, 260 }, { 120, 420 },
            { 180, 560 }, { 220, 680 }, { 280, 750 },
        }},
        .cadence_curve = {4, {
            {  50, 24576 }, {  80, 32768 }, { 110, 29491 }, { 140, 20480 },
        }},
    },
    /* cargo */
    {
        .speed_curve = {5, {
            {   0, 150 }, {  80, 260 }, { 140, 420 }, { 200, 540 }, { 240, 650 },
        }},
        .cadence_curve = {3, {
            {  60, 32768 }, {  90, 32768 }, { 120, 24576 },
        }},
    },
    /* rain */
    {
        .speed_curve = {5, {
            {   0,  80 }, {  60, 140 }, { 120, 220 }, { 160, 320 }, { 200, 400 },
        }},
        .cadence_curve = {3, {
            {  50, 24576 }, {  80, 32768 }, { 110, 24576 },
        }},
    },
    /* valet */
    {
        .speed_curve = {4, {
            {   0,  40 }, {  40, 120 }, {  80, 200 }, { 120, 250 },
        }},
        .cadence_curve = {2, {
            {  60, 32768 }, { 100, 26214 },
        }},
    },
};
