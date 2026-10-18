/*
 * Host command-line tool around the core, used by the Python tests to compare the C code with
 * the reference in tools/vmnlib.py.
 *
 *   vmn_cli features <condition 0-3> <shaft_hz> <seed>   15 feature values on one line
 *   vmn_cli classify <condition 0-3> <shaft_hz> <seed>   class index and confidence
 *   vmn_cli logits                                      reads int8 vectors from stdin, one per
 *                                                       line, and prints the int32 scores
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vmn/classifier.h"
#include "vmn/config.h"
#include "vmn/features.h"
#include "vmn/fft.h"
#include "vmn/prng.h"
#include "vmn/signal_sim.h"

static float s_frame[VMN_FRAME_LEN];
static float s_re[VMN_FRAME_LEN];
static float s_im[VMN_FRAME_LEN];

static int usage(void)
{
    fputs("usage: vmn_cli features|classify <condition> <shaft_hz> <seed>\n"
          "       vmn_cli logits < vectors\n",
          stderr);
    return 2;
}

static int frame_features(int argc, char **argv, float *shaft_hz, vmn_features_t *f)
{
    if (argc != 5) {
        return -1;
    }
    const long condition = strtol(argv[2], NULL, 10);
    *shaft_hz = strtof(argv[3], NULL);
    const unsigned long seed = strtoul(argv[4], NULL, 10);
    if (condition < 0 || condition >= VMN_CONDITION_COUNT || *shaft_hz <= 0.0f) {
        return -1;
    }
    vmn_sim_params_t params;
    vmn_sim_default_params((vmn_condition_t)condition, *shaft_hz, &params);
    uint32_t rng = vmn_prng_seed((uint32_t)seed);
    vmn_sim_generate(&params, &rng, s_frame, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ);
    return vmn_features_compute(s_frame, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, *shaft_hz, s_re, s_im, f);
}

static int cmd_logits(void)
{
    char line[512];
    while (fgets(line, sizeof line, stdin) != NULL) {
        int8_t q[VMN_FEATURE_COUNT];
        char *p = line;
        for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
            char *end = NULL;
            const long v = strtol(p, &end, 10);
            if (end == p || v < -128 || v > 127) {
                fprintf(stderr, "bad vector: %s", line);
                return 1;
            }
            q[i] = (int8_t)v;
            p = end;
        }
        int32_t logits[VMN_CLASS_COUNT];
        vmn_classifier_logits(q, logits);
        for (int c = 0; c < VMN_CLASS_COUNT; ++c) {
            printf(c ? " %ld" : "%ld", (long)logits[c]);
        }
        putchar('\n');
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        return usage();
    }
    vmn_fft_init();
    if (strcmp(argv[1], "logits") == 0) {
        return cmd_logits();
    }
    float shaft_hz = 0.0f;
    vmn_features_t f;
    if (strcmp(argv[1], "features") == 0) {
        if (frame_features(argc, argv, &shaft_hz, &f) != 0) {
            return usage();
        }
        for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
            printf(i ? " %.9g" : "%.9g", (double)f.values[i]);
        }
        putchar('\n');
        return 0;
    }
    if (strcmp(argv[1], "classify") == 0) {
        if (frame_features(argc, argv, &shaft_hz, &f) != 0) {
            return usage();
        }
        vmn_prediction_t pred;
        vmn_classify(f.values, &pred);
        printf("%d %.4f %s\n", pred.cls, (double)pred.confidence, vmn_class_name(pred.cls));
        return 0;
    }
    return usage();
}
