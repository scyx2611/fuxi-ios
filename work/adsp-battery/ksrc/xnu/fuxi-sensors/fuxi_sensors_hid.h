/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Minimal C ABI between bsd/dev/fuxi_sensors.c and iokit/Kernel/arm/hid/FuxiSensorsHid.cpp. */
#ifndef FUXI_SENSORS_HID_H
#define FUXI_SENSORS_HID_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Kind indices match direct sensors (0..5), plus SNS_HID_ORIENT (6) and SNS_HID_DEVMOTION (7). */
enum {
    FUXI_SENSOR_ACCEL = 0,
    FUXI_SENSOR_GYRO = 1,
    FUXI_SENSOR_MAG = 2,
    FUXI_SENSOR_LIGHT = 3,
    FUXI_SENSOR_PROXIMITY = 4,
    FUXI_SENSOR_PRESSURE = 5,
    FUXI_SENSOR_ORIENT = 6,
    FUXI_SENSOR_DEVMOTION = 7,
    FUXI_SENSOR_HID_KINDS = 8,
};

typedef void (*fuxi_sensors_demand_fn)(unsigned kind, uint32_t demand);

int fuxi_sensors_hid_setup(fuxi_sensors_demand_fn demand);
int fuxi_sensors_hid_publish(unsigned kind);
int fuxi_sensors_hid_node(const char *name);
void fuxi_sensors_hid_dispatch_sample(unsigned kind, uint64_t timestamp,
    int32_t value0, int32_t value1, int32_t value2);
void fuxi_sensors_hid_orientation(int state);
void fuxi_sensors_hid_orientation_replay(void);
void fuxi_sensors_hid_dispatch_compass(uint64_t timestamp, const uint8_t report[88]);
void fuxi_sensors_hid_counts(uint32_t *dispatched, uint32_t *unbound, uint32_t *restamped);

/* FastRPC reverse file service (bsd/dev/fuxi_sensors_rpc.c). */
int fuxi_sensors_rpc_start(uint32_t log_level);
void fuxi_sensors_rpc_log_smmu(const char *tag);

#ifdef __cplusplus
}
#endif
#endif
