#include "vmn/classifier.h"

#include <math.h>

#include "model_data.h"

_Static_assert(VMN_MODEL_INPUTS == VMN_FEATURE_COUNT, "the model must take every feature");
_Static_assert(VMN_MODEL_CLASSES == VMN_CLASS_COUNT, "the model must score every class");
_Static_assert(VMN_MODEL_SHIFT1 >= 1 && VMN_MODEL_SHIFT1 <= 62, "requantisation shift out of range");

static const char *const k_class_names[VMN_CLASS_COUNT] = {"healthy", "imbalance", "misalignment",
                                                           "bearing fault"};

void vmn_classifier_quantize_input(const float features[VMN_FEATURE_COUNT], int8_t q[VMN_FEATURE_COUNT])
{
    for (int i = 0; i < VMN_FEATURE_COUNT; ++i) {
        const float z = (features[i] - vmn_model_mean[i]) / vmn_model_std[i];
        const float s = floorf(z / VMN_MODEL_INPUT_SCALE + 0.5f);
        q[i] = (int8_t)(s > 127.0f ? 127 : (s < -127.0f ? -127 : (int32_t)s));
    }
}

void vmn_classifier_logits(const int8_t q[VMN_FEATURE_COUNT], int32_t logits[VMN_CLASS_COUNT])
{
    int8_t hidden[VMN_MODEL_HIDDEN];

    /* Dense + ReLU, then requantise: hidden = round(acc * M1), M1 = VMN_MODEL_M1 / 2^SHIFT1. */
    for (int j = 0; j < VMN_MODEL_HIDDEN; ++j) {
        int32_t acc = vmn_model_b1[j];
        for (int i = 0; i < VMN_MODEL_INPUTS; ++i) {
            acc += (int32_t)q[i] * (int32_t)vmn_model_w1[j * VMN_MODEL_INPUTS + i];
        }
        if (acc < 0) {
            acc = 0;
        }
        const int64_t scaled = ((int64_t)acc * VMN_MODEL_M1 + ((int64_t)1 << (VMN_MODEL_SHIFT1 - 1))) >>
                               VMN_MODEL_SHIFT1;
        hidden[j] = (int8_t)(scaled > 127 ? 127 : scaled);
    }

    /* Output layer; the scores stay in int32. */
    for (int c = 0; c < VMN_MODEL_CLASSES; ++c) {
        int32_t acc = vmn_model_b2[c];
        for (int j = 0; j < VMN_MODEL_HIDDEN; ++j) {
            acc += (int32_t)hidden[j] * (int32_t)vmn_model_w2[c * VMN_MODEL_HIDDEN + j];
        }
        logits[c] = acc;
    }
}

void vmn_classify(const float features[VMN_FEATURE_COUNT], vmn_prediction_t *out)
{
    int8_t q[VMN_FEATURE_COUNT];
    vmn_classifier_quantize_input(features, q);
    vmn_classifier_logits(q, out->logits);

    int best = 0;
    for (int c = 1; c < VMN_CLASS_COUNT; ++c) {
        if (out->logits[c] > out->logits[best]) {
            best = c;
        }
    }
    float sum = 0.0f;
    for (int c = 0; c < VMN_CLASS_COUNT; ++c) {
        sum += expf((float)(out->logits[c] - out->logits[best]) * VMN_MODEL_OUTPUT_SCALE);
    }
    out->cls = best;
    out->confidence = 1.0f / sum;
}

const char *vmn_class_name(int cls)
{
    return cls >= 0 && cls < VMN_CLASS_COUNT ? k_class_names[cls] : "unknown";
}
