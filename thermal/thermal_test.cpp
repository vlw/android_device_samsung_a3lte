// SPDX-License-Identifier: Apache-2.0
#include <hardware/thermal.h>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
int main() {
    const hw_module_t* common = nullptr;
    CHECK(hw_get_module(THERMAL_HARDWARE_MODULE_ID, &common) == 0 && common);
    CHECK(common->methods != nullptr);
    auto module = const_cast<thermal_module_t*>(reinterpret_cast<const thermal_module_t*>(common));
    CHECK(module->getTemperatures(module, nullptr, 0) == 3);
    temperature_t temps[3]{};
    temps[1].name = "sentinel";
    CHECK(module->getTemperatures(module, temps, 1) == 3);
    CHECK(temps[1].name[0] == 's');
    CHECK(module->getTemperatures(module, temps, 3) == 3);
    for (const auto& temp : temps) {
        std::printf("%s: %.3f C\n", temp.name, temp.current_value);
        CHECK(temp.current_value > -20 && temp.current_value < 120);
    }
    CHECK(module->getCpuUsages(module, nullptr) == 4);
    cpu_usage_t before[4]{}, after[4]{};
    CHECK(module->getCpuUsages(module, before) == 4);
    usleep(200000);
    CHECK(module->getCpuUsages(module, after) == 4);
    for (unsigned i = 0; i < 4; ++i) {
        std::printf("%s: active=%llu total=%llu ms online=%d\n", after[i].name,
            static_cast<unsigned long long>(after[i].active),
            static_cast<unsigned long long>(after[i].total), after[i].is_online);
        CHECK(after[i].active <= after[i].total && after[i].total > 0);
        CHECK(after[i].active >= before[i].active && after[i].total >= before[i].total);
    }
    CHECK(module->getCoolingDevices(module, nullptr, 0) == 0);
    std::puts("PASS module ABI, sensor units, bounded output, real CPU counters");
}
