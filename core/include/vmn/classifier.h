#ifndef VMN_CLASSIFIER_H
#define VMN_CLASSIFIER_H

#include <stdint.h>

#include "vmn/features.h"

#define VMN_CLASS_COUNT 4

typedef struct {
    int cls;                            /**< index into the class names, see vmn_class_name */
    float confidence;                   /**< softmax probability of cls, 0 to 1 */
    int32_t logits[VMN_CLASS_COUNT];    /**< raw integer scores */
} vmn_prediction_t;

/** Standardises the features and quantises them to the model's int8 input. */
void vmn_classifier_quantize_input(const float features[VMN_FEATURE_COUNT], int8_t q[VMN_FEATURE_COUNT]);

/** Integer-only inference: int8 weights and activations, int32 accumulators. */
void vmn_classifier_logits(const int8_t q[VMN_FEATURE_COUNT], int32_t logits[VMN_CLASS_COUNT]);

/** Quantises, runs the model and picks the class with the highest score. */
void vmn_classify(const float features[VMN_FEATURE_COUNT], vmn_prediction_t *out);

/** "healthy", "imbalance", "misalignment" or "bearing fault"; "unknown" for a bad index. */
const char *vmn_class_name(int cls);

#endif
