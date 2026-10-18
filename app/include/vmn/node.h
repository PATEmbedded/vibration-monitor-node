#ifndef VMN_NODE_H
#define VMN_NODE_H

#include <stdbool.h>
#include <stdint.h>

#include "vmn/classifier.h"
#include "vmn/config.h"
#include "vmn/features.h"
#include "vmn/modbus_rtu.h"

/*
 * The node's application logic without RTOS or hardware: settings, results, the Modbus register
 * map and one analysis step. The firmware adds tasks, locking and the UART; the host tests use
 * this module directly.
 */

#define VMN_FW_VERSION 0x0100u /**< major in the high byte, minor in the low byte */

/** Holding registers (read/write). */
enum {
    VMN_HR_SLAVE_ADDRESS = 0,  /**< 1..247, takes effect after the reply */
    VMN_HR_SHAFT_SPEED = 1,    /**< 0.1 Hz units, 50..1000 (5 to 100 Hz) */
    VMN_HR_SOURCE = 2,         /**< signal source, 0..3: simulated condition, see vmn_condition_t */
    VMN_HR_INTERVAL_MS = 3,    /**< time between analyses, 100..10000 ms */
    VMN_HR_COUNT
};

/** Input registers (read-only). */
enum {
    VMN_IR_FW_VERSION = 0,
    VMN_IR_FRAME_COUNT = 1,    /**< analyses since start-up, wraps at 65536 */
    VMN_IR_CLASS = 2,          /**< 0 healthy, 1 imbalance, 2 misalignment, 3 bearing fault */
    VMN_IR_CONFIDENCE = 3,     /**< per mille */
    VMN_IR_RMS = 4,            /**< x 1000 */
    VMN_IR_PEAK = 5,           /**< x 1000 */
    VMN_IR_CREST = 6,          /**< x 100 */
    VMN_IR_KURTOSIS = 7,       /**< x 100 */
    VMN_IR_HARMONIC_1X = 8,    /**< x 1000 */
    VMN_IR_HARMONIC_2X = 9,    /**< x 1000 */
    VMN_IR_HARMONIC_3X = 10,   /**< x 1000 */
    VMN_IR_FRAMES_OK = 11,     /**< Modbus frames accepted (low 16 bits) */
    VMN_IR_CRC_ERRORS = 12,
    VMN_IR_EXCEPTIONS = 13,
    VMN_IR_UART_OVERRUNS = 14,
    VMN_IR_COUNT
};

typedef struct {
    uint16_t slave_address;
    uint16_t shaft_speed_dhz;
    uint16_t source;
    uint16_t interval_ms;
} vmn_settings_t;

typedef struct {
    uint16_t frame_count;
    uint16_t cls;
    uint16_t confidence_permille;
    uint16_t rms_milli;
    uint16_t peak_milli;
    uint16_t crest_centi;
    uint16_t kurtosis_centi;
    uint16_t harmonic_milli[3];
} vmn_results_t;

typedef struct {
    vmn_settings_t settings;
    vmn_results_t results;
    const vmn_modbus_stats_t *modbus_stats; /**< shown in the input registers; may be NULL */
    uint32_t uart_overruns;
    bool address_changed;                   /**< set when a new slave address is committed */
} vmn_node_t;

/** Buffers for one analysis step; 12 KB, so keep it static. */
typedef struct {
    float frame[VMN_FRAME_LEN];
    float re[VMN_FRAME_LEN];
    float im[VMN_FRAME_LEN];
} vmn_workspace_t;

void vmn_node_init(vmn_node_t *node);

/** Register-map callbacks bound to node, for vmn_modbus_init. */
vmn_modbus_map_t vmn_node_modbus_map(vmn_node_t *node);

/** Shaft frequency in Hz from the settings. */
float vmn_node_shaft_hz(const vmn_settings_t *settings);

/**
 * One analysis step: acquire a frame from the selected source, compute the features and classify.
 * rng is the simulator's random state. Returns 0 on success.
 */
int vmn_node_analyse(const vmn_settings_t *settings, uint32_t *rng, vmn_workspace_t *ws,
                     vmn_features_t *features, vmn_prediction_t *prediction);

/** Stores a result in the input registers, scaled to integers, and counts the frame. */
void vmn_node_store_results(vmn_node_t *node, const vmn_features_t *features,
                            const vmn_prediction_t *prediction);

#endif
