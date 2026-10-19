// SPDX-License-Identifier: Apache-2.0
// Read the existing Android tracker; never reattach/reset its BPF programs.
#include <optional>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <linux/bpf.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <cputimeinstate.h>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static uint64_t sum(const std::vector<std::vector<uint64_t>>& times) {
    uint64_t total = 0;
    for (const auto& policy : times) for (uint64_t ns : policy) total += ns;
    return total;
}
static uint64_t cpuTime() {
    timespec ts{};
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts)) _exit(2);
    return uint64_t(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}
static bool checkCounter(const char* name) {
    char path[256];
    std::snprintf(path, sizeof(path), "/sys/fs/bpf/map_time_in_state_%s", name);
    union bpf_attr attr{};
    attr.pathname = reinterpret_cast<uintptr_t>(path);
    int fd = syscall(__NR_bpf, BPF_OBJ_GET, &attr, sizeof(attr));
    if (fd < 0) return false;
    uint32_t key = 0, value = 0;
    attr = {};
    attr.map_fd = fd;
    attr.key = reinterpret_cast<uintptr_t>(&key);
    attr.value = reinterpret_cast<uintptr_t>(&value);
    bool ok = !syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &attr, sizeof(attr));
    if (ok)
        std::printf("%s: %u\n", name, value);
    close(fd);
    return ok && value <= 4;
}
int main() {
    CHECK(getuid() == 0);
    CHECK(android::bpf::isTrackingUidTimesSupported());
    auto frequencies = android::bpf::getCpuFreqs();
    CHECK(frequencies && !frequencies->empty());
    CHECK(checkCounter("nr_active_map"));
    CHECK(checkCounter("policy_nr_active_map"));
    for (const auto& policy : *frequencies) {
        std::printf("CPU policy frequencies (kHz):");
        for (uint32_t khz : policy) std::printf(" %u", khz);
        std::puts("");
    }
    constexpr uid_t testUid = 29000;
    auto before = android::bpf::getUidCpuFreqTimes(testUid);
    CHECK(before);
    int gate[2];
    CHECK(pipe2(gate, O_CLOEXEC) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        close(gate[1]);
        char go;
        if (read(gate[0], &go, 1) != 1) _exit(2);
        close(gate[0]);
        if (setgid(testUid) || setuid(testUid)) _exit(2);
        const uint64_t start = cpuTime();
        volatile uint64_t value = 1;
        while (cpuTime() - start < 600000000) {
            for (unsigned i = 0; i < 10000; ++i) value = value * 1664525 + 1013904223;
        }
        _exit(0);
    }
    close(gate[0]);
    bool tracked = android::bpf::startTrackingProcessCpuTimes(child) &&
        android::bpf::startAggregatingTaskCpuTimes(child, 7);
    // Release and reap the child even if registration fails.
    CHECK(write(gate[1], "x", 1) == 1);
    close(gate[1]);
    int status;
    rusage usage{};
    CHECK(wait4(child, &status, 0, &usage) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(tracked);
    auto after = android::bpf::getUidCpuFreqTimes(testUid);
    CHECK(after && sum(*after) >= sum(*before));
    uint64_t delta = sum(*after) - sum(*before);
    uint64_t actual = uint64_t(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000000000 +
        uint64_t(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1000;
    std::printf("UID %u: BPF +%llu ns, wait4 CPU %llu ns\n", testUid,
        static_cast<unsigned long long>(delta), static_cast<unsigned long long>(actual));
    CHECK(actual > 500000000 && delta > actual * 7 / 10 && delta < actual * 13 / 10);
    auto tasks = android::bpf::getAggregatedTaskCpuFreqTimes(child, {7});
    CHECK(tasks && tasks->count(7));
    uint64_t taskTime = sum(tasks->at(7));
    std::printf("PID %d / aggregation 7: %llu ns\n", child, static_cast<unsigned long long>(taskTime));
    CHECK(taskTime > actual * 7 / 10 && taskTime < actual * 13 / 10);
    auto concurrent = android::bpf::getUidConcurrentTimes(testUid);
    CHECK(concurrent && !concurrent->active.empty());
    uint64_t active = 0;
    for (auto ns : concurrent->active) {
        std::printf("concurrent bucket: %llu ns\n", static_cast<unsigned long long>(ns));
        active += ns;
    }
    CHECK(checkCounter("nr_active_map"));
    CHECK(checkCounter("policy_nr_active_map"));
    CHECK(active > 0);
    CHECK(active == sum(*after));
    std::puts("PASS real UID/frequency/concurrency accounting versus independent CPU usage");
}
