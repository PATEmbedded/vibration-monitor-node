#include "vmn/node.h"

#include <math.h>
#include <string.h>

#include "vmn/signal_sim.h"

typedef struct {
    uint16_t min;
    uint16_t max;
} range_t;

static const range_t k_limits[VMN_HR_COUNT] = {
    [VMN_HR_SLAVE_ADDRESS] = {1u, 247u},
    [VMN_HR_SHAFT_SPEED] = {50u, 1000u},
    [VMN_HR_SOURCE] = {0u, (uint16_t)(VMN_CONDITION_COUNT - 1)},
    [VMN_HR_INTERVAL_MS] = {100u, 10000u},
};

static uint16_t *holding_slot(vmn_settings_t *s, uint16_t reg)
{
    switch (reg) {
    case VMN_HR_SLAVE_ADDRESS:
        return &s->slave_address;
    case VMN_HR_SHAFT_SPEED:
        return &s->shaft_speed_dhz;
    case VMN_HR_SOURCE:
        return &s->source;
    case VMN_HR_INTERVAL_MS:
        return &s->interval_ms;
    default:
        return NULL;
    }
}

static uint8_t read_holding(void *ctx, uint16_t reg, uint16_t *value)
{
    vmn_node_t *node = ctx;
    const uint16_t *slot = holding_slot(&node->settings, reg);
    if (slot == NULL) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    *value = *slot;
    return VMN_MODBUS_OK;
}

static uint8_t write_holding(void *ctx, uint16_t reg, uint16_t value, bool commit)
{
    vmn_node_t *node = ctx;
    uint16_t *slot = holding_slot(&node->settings, reg);
    if (slot == NULL) {
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    if (value < k_limits[reg].min || value > k_limits[reg].max) {
        return VMN_MODBUS_ILLEGAL_VALUE;
    }
    if (commit) {
        if (reg == VMN_HR_SLAVE_ADDRESS && value != *slot) {
            node->address_changed = true;
        }
        *slot = value;
    }
    return VMN_MODBUS_OK;
}

static uint16_t low16(uint32_t v)
{
    return (uint16_t)(v & 0xFFFFu);
}

static uint8_t read_input(void *ctx, uint16_t reg, uint16_t *value)
{
    const vmn_node_t *node = ctx;
    const vmn_results_t *r = &node->results;
    const vmn_modbus_stats_t *st = node->modbus_stats;
    switch (reg) {
    case VMN_IR_FW_VERSION:
        *value = VMN_FW_VERSION;
        break;
    case VMN_IR_FRAME_COUNT:
        *value = r->frame_count;
        break;
    case VMN_IR_CLASS:
        *value = r->cls;
        break;
    case VMN_IR_CONFIDENCE:
        *value = r->confidence_permille;
        break;
    case VMN_IR_RMS:
        *value = r->rms_milli;
        break;
    case VMN_IR_PEAK:
        *value = r->peak_milli;
        break;
    case VMN_IR_CREST:
        *value = r->crest_centi;
        break;
    case VMN_IR_KURTOSIS:
        *value = r->kurtosis_centi;
        break;
    case VMN_IR_HARMONIC_1X:
    case VMN_IR_HARMONIC_2X:
    case VMN_IR_HARMONIC_3X:
        *value = r->harmonic_milli[reg - VMN_IR_HARMONIC_1X];
        break;
    case VMN_IR_FRAMES_OK:
        *value = st != NULL ? low16(st->frames_ok) : 0u;
        break;
    case VMN_IR_CRC_ERRORS:
        *value = st != NULL ? low16(st->crc_errors) : 0u;
        break;
    case VMN_IR_EXCEPTIONS:
        *value = st != NULL ? low16(st->exceptions) : 0u;
        break;
    case VMN_IR_UART_OVERRUNS:
        *value = low16(node->uart_overruns);
        break;
    default:
        return VMN_MODBUS_ILLEGAL_ADDRESS;
    }
    return VMN_MODBUS_OK;
}

void vmn_node_init(vmn_node_t *node)
{
    memset(node, 0, sizeof(*node));
    node->settings.slave_address = 1u;
    node->settings.shaft_speed_dhz = 250u;
    node->settings.source = VMN_CONDITION_HEALTHY;
    node->settings.interval_ms = 500u;
}

vmn_modbus_map_t vmn_node_modbus_map(vmn_node_t *node)
{
    const vmn_modbus_map_t map = {read_holding, read_input, write_holding, node};
    return map;
}

float vmn_node_shaft_hz(const vmn_settings_t *settings)
{
    return (float)settings->shaft_speed_dhz / 10.0f;
}

int vmn_node_analyse(const vmn_settings_t *settings, uint32_t *rng, vmn_workspace_t *ws,
                     vmn_features_t *features, vmn_prediction_t *prediction)
{
    const float shaft_hz = vmn_node_shaft_hz(settings);
    vmn_sim_params_t params;
    vmn_sim_default_params((vmn_condition_t)settings->source, shaft_hz, &params);
    vmn_sim_generate(&params, rng, ws->frame, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ);
    if (vmn_features_compute(ws->frame, VMN_FRAME_LEN, VMN_SAMPLE_RATE_HZ, shaft_hz, ws->re, ws->im,
                             features) != 0) {
        return -1;
    }
    vmn_classify(features->values, prediction);
    return 0;
}

static uint16_t scaled(float value, float factor)
{
    const float v = floorf(value * factor + 0.5f);
    return v <= 0.0f ? 0u : (v >= 65535.0f ? 65535u : (uint16_t)v);
}

void vmn_node_store_results(vmn_node_t *node, const vmn_features_t *features,
                            const vmn_prediction_t *prediction)
{
    vmn_results_t *r = &node->results;
    r->frame_count++;
    r->cls = (uint16_t)prediction->cls;
    r->confidence_permille = scaled(prediction->confidence, 1000.0f);
    r->rms_milli = scaled(features->rms, 1000.0f);
    r->peak_milli = scaled(features->peak, 1000.0f);
    r->crest_centi = scaled(features->crest, 100.0f);
    r->kurtosis_centi = scaled(features->kurtosis, 100.0f);
    for (int h = 0; h < 3; ++h) {
        r->harmonic_milli[h] = scaled(features->harmonic[h], 1000.0f);
    }
}
