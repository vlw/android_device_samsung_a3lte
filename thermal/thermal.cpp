/* SPDX-License-Identifier: Apache-2.0 */
#include <hardware/thermal.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>

namespace {
struct Sensor {
    const char* type;
    const char* name;
    temperature_type kind;
    float scale;
};
// Match thermal-engine.conf's physical CPU aliases. TSENS reports degrees,
// whereas the battery thermal zone reports millidegrees on this kernel.
constexpr Sensor sensors[] = {
    {"tsens_tz_sensor5", "cpu0-1", DEVICE_TEMPERATURE_CPU, 1.0f},
    {"tsens_tz_sensor4", "cpu2-3", DEVICE_TEMPERATURE_CPU, 1.0f},
    {"battery", "battery", DEVICE_TEMPERATURE_BATTERY, 0.001f},
};
constexpr size_t sensorCount = sizeof(sensors) / sizeof(sensors[0]);
constexpr size_t cpuCount = 4;
const char* const cpuNames[] = {"cpu0", "cpu1", "cpu2", "cpu3"};
hw_module_methods_t moduleMethods = {};

bool readLine(const char* path, char* value, size_t size) {
    FILE* file = std::fopen(path, "re");
    if (!file) return false;
    bool ok = std::fgets(value, size, file) != nullptr;
    std::fclose(file);
    if (ok) value[std::strcspn(value, "\r\n")] = '\0';
    return ok;
}

float readSensor(const Sensor& sensor) {
    for (unsigned i = 0; i < 64; ++i) {
        char path[128], type[80];
        std::snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%u/type", i);
        if (!readLine(path, type, sizeof(type)) || std::strcmp(type, sensor.type)) continue;
        std::snprintf(path, sizeof(path), "/sys/class/thermal/thermal_zone%u/temp", i);
        FILE* file = std::fopen(path, "re");
        if (!file) return UNKNOWN_TEMPERATURE;
        float raw;
        bool ok = std::fscanf(file, "%f", &raw) == 1;
        std::fclose(file);
        return ok ? raw * sensor.scale : UNKNOWN_TEMPERATURE;
    }
    return UNKNOWN_TEMPERATURE;
}

ssize_t getTemperatures(thermal_module*, temperature_t* list, size_t size) {
    if (list) {
        for (size_t i = 0; i < size && i < sensorCount; ++i) {
            list[i] = {sensors[i].kind, sensors[i].name, readSensor(sensors[i]),
                UNKNOWN_TEMPERATURE, UNKNOWN_TEMPERATURE, UNKNOWN_TEMPERATURE};
        }
    }
    // Thresholds are intentionally unknown: thermal-engine/kernel own the
    // protection policy; do not manufacture Android shutdown thresholds.
    return sensorCount;
}

ssize_t getCpuUsages(thermal_module*, cpu_usage_t* list) {
    if (!list) return cpuCount;
    long ticks = sysconf(_SC_CLK_TCK);
    if (ticks <= 0) return -EINVAL;
    for (size_t i = 0; i < cpuCount; ++i) list[i] = {cpuNames[i], 0, 0, false};
    FILE* file = std::fopen("/proc/stat", "re");
    if (!file) return -errno;
    char line[512];
    while (std::fgets(line, sizeof(line), file)) {
        unsigned cpu;
        unsigned long long user, nice, system, idle, wait, irq, softirq, steal;
        int count = std::sscanf(line, "cpu%u %llu %llu %llu %llu %llu %llu %llu %llu",
            &cpu, &user, &nice, &system, &idle, &wait, &irq, &softirq, &steal);
        if (count != 9 || cpu >= cpuCount) continue;
        uint64_t active = user + nice + system + irq + softirq + steal;
        list[cpu].active = active * 1000 / ticks;
        list[cpu].total = (active + idle + wait) * 1000 / ticks;
        char onlinePath[128], online[16];
        std::snprintf(onlinePath, sizeof(onlinePath), "/sys/devices/system/cpu/cpu%u/online", cpu);
        // /proc/stat retains offline CPUs as well. CPU0 has no online file
        // on kernels that do not permit hot-unplugging it.
        list[cpu].is_online = readLine(onlinePath, online, sizeof(online)) ? online[0] == '1' : cpu == 0;
    }
    bool failed = std::ferror(file);
    std::fclose(file);
    return failed ? -EIO : static_cast<ssize_t>(cpuCount);
}

// This passive-cooled phone has no FAN_RPM device representable in Thermal 1.0.
ssize_t getCoolingDevices(thermal_module*, cooling_device_t*, size_t) { return 0; }
} // namespace

extern "C" {
thermal_module_t HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = THERMAL_HARDWARE_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = THERMAL_HARDWARE_MODULE_ID,
        .name = "Samsung A3 temperature and CPU usage module",
        .author = "LineageOS",
        .methods = &moduleMethods,
    },
    .getTemperatures = getTemperatures,
    .getCpuUsages = getCpuUsages,
    .getCoolingDevices = getCoolingDevices,
};
}
