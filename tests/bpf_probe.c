/* SPDX-License-Identifier: GPL-2.0-only */
/* Run as root after booting the BPF kernel. No persistent configuration changes. */
#include <errno.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/bpf.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SO_COOKIE
#define SO_COOKIE 57
#endif
#define PTR(p) ((__u64)(uintptr_t)(p))
#define I(c, d, s, o, v) ((struct bpf_insn){.code=(c), .dst_reg=(d), .src_reg=(s), .off=(o), .imm=(v)})
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s: %s\n", __LINE__, #x, strerror(errno)); goto out; } } while (0)
struct counters { uint64_t cookie; uint32_t uid, pad; uint64_t packets; };
static int bpf_cmd(enum bpf_cmd cmd, union bpf_attr *attr)
{
    return syscall(__NR_bpf, cmd, attr, sizeof(*attr));
}
static int cookie(int fd, uint64_t *value)
{
    socklen_t len = sizeof(*value);
    return getsockopt(fd, SOL_SOCKET, SO_COOKIE, value, &len) || len != sizeof(*value);
}
static int load(const struct bpf_insn *insns, size_t count, char *log, size_t len)
{
    static const char license[] = "GPL";
    union bpf_attr a = {0};
    a.prog_type = BPF_PROG_TYPE_CGROUP_SKB;
    a.insn_cnt = count; a.insns = PTR(insns); a.license = PTR(license);
    a.log_buf = PTR(log); a.log_size = len; a.log_level = 1;
    return bpf_cmd(BPF_PROG_LOAD, &a);
}
static int netd_probe(void)
{
    typedef int (*tag_fn)(int, uint32_t, uid_t);
    void *lib = dlopen("libnetd_client.so", RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    tag_fn tag = (tag_fn)dlsym(lib, "tagSocket");
    if (!tag) return 1;
    union bpf_attr a = {0};
    a.pathname = PTR("/sys/fs/bpf/map_netd_cookie_tag_map");
    int map = bpf_cmd(BPF_OBJ_GET, &a);
    if (map < 0) { perror("open netd cookie map"); return 1; }
    for (int i = 0; i < 4; ++i) {
        int family = i & 1 ? AF_INET6 : AF_INET;
        int type = i & 2 ? SOCK_DGRAM : SOCK_STREAM;
        int fd = socket(family, type | SOCK_CLOEXEC, 0);
        uint64_t c = 0;
        struct { uint32_t uid, tag; } value = {0};
        if (fd < 0 || cookie(fd, &c) || tag(fd, 0xa3000u + i, getuid())) {
            fprintf(stderr, "FAIL tag socket family=%d type=%d errno=%d\n", family, type, errno);
            if (fd >= 0) close(fd);
            close(map); return 1;
        }
        memset(&a, 0, sizeof(a)); a.map_fd = map; a.key = PTR(&c); a.value = PTR(&value);
        if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) || value.uid != getuid() || value.tag != 0xa3000u + i) {
            fprintf(stderr, "FAIL netd cookie tag contents uid=%u tag=%x\n", value.uid, value.tag);
            close(fd); close(map); return 1;
        }
        close(fd);
        int gone = 0;
        for (int n = 0; n < 100; ++n) {
            if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) < 0 && errno == ENOENT) { gone = 1; break; }
            usleep(20000);
        }
        if (!gone) { fprintf(stderr, "FAIL socket destruction cleanup family=%d type=%d\n", family, type); close(map); return 1; }
        printf("PASS netd tag/open/automatic destruction cleanup: %s %s\n", family == AF_INET ? "IPv4" : "IPv6", type == SOCK_STREAM ? "TCP" : "UDP");
    }
    close(map);
    memset(&a, 0, sizeof(a)); a.pathname = PTR("/sys/fs/bpf/map_netd_app_uid_stats_map");
    map = bpf_cmd(BPF_OBJ_GET, &a);
    if (map < 0) return 1;
    uint32_t uid = getuid();
    struct { uint64_t rx_packets, rx_bytes, tx_packets, tx_bytes; } before = {0}, after = {0};
    memset(&a, 0, sizeof(a)); a.map_fd = map; a.key = PTR(&uid); a.value = PTR(&before);
    if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) && errno != ENOENT) { close(map); return 1; }
    int rx = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    int tx = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in dst = {.sin_family=AF_INET, .sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof(dst);
    struct timeval timeout = {.tv_sec=2};
    char data[32];
    if (rx < 0 || tx < 0 || bind(rx, (void *)&dst, sizeof(dst)) || getsockname(rx, (void *)&dst, &len) ||
        setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
        sendto(tx, "account", 7, 0, (void *)&dst, sizeof(dst)) != 7 || recv(rx, data, sizeof(data), 0) != 7) {
        fprintf(stderr, "FAIL netd accounting UDP exchange: %s\n", strerror(errno));
        if (rx >= 0) close(rx);
        if (tx >= 0) close(tx);
        close(map); return 1;
    }
    close(rx); close(tx); a.value = PTR(&after);
    if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) || after.tx_packets <= before.tx_packets ||
        after.rx_packets <= before.rx_packets || after.tx_bytes < before.tx_bytes + 35 ||
        after.rx_bytes < before.rx_bytes + 35) {
        fprintf(stderr, "FAIL netd real packet/byte accounting\n"); close(map); return 1;
    }
    printf("PASS netd UDP accounting: RX +%llu packets/+%llu bytes, TX +%llu packets/+%llu bytes\n",
        (unsigned long long)(after.rx_packets-before.rx_packets), (unsigned long long)(after.rx_bytes-before.rx_bytes),
        (unsigned long long)(after.tx_packets-before.tx_packets), (unsigned long long)(after.tx_bytes-before.tx_bytes));
    close(map); dlclose(lib); return 0;
}
/* Check the interface map as well as the cgroup/UID accounting path. */
static int iface_probe(void)
{
    union bpf_attr a = {0};
    a.pathname = PTR("/sys/fs/bpf/map_netd_iface_stats_map");
    int map = bpf_cmd(BPF_OBJ_GET, &a);
    if (map < 0) { perror("open interface map"); return 1; }
    uint32_t index = 1; /* Linux loopback interface. */
    for (int family_index = 0; family_index < 2; ++family_index) {
        struct { uint64_t rp, rb, tp, tb; } before = {0}, after = {0};
        memset(&a, 0, sizeof(a)); a.map_fd = map; a.key = PTR(&index); a.value = PTR(&before);
        if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) && errno != ENOENT) { close(map); return 1; }
        int family = family_index ? AF_INET6 : AF_INET;
        int rx = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        int tx = socket(family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        struct sockaddr_storage address = {0};
        struct sockaddr_in *v4 = (void *)&address;
        struct sockaddr_in6 *v6 = (void *)&address;
        socklen_t len;
        if (family == AF_INET) {
            v4->sin_family = AF_INET; v4->sin_addr.s_addr = htonl(INADDR_LOOPBACK); len = sizeof(*v4);
        } else {
            v6->sin6_family = AF_INET6; v6->sin6_addr = in6addr_loopback; len = sizeof(*v6);
        }
        struct timeval timeout = {.tv_sec=2};
        char data[16];
        int failed = rx < 0 || tx < 0 || bind(rx, (void *)&address, len) ||
            getsockname(rx, (void *)&address, &len) ||
            setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) ||
            sendto(tx, "account", 7, 0, (void *)&address, len) != 7 || recv(rx, data, sizeof(data), 0) != 7;
        if (rx >= 0) close(rx);
        if (tx >= 0) close(tx);
        if (failed) { perror("interface UDP exchange"); close(map); return 1; }
        a.value = PTR(&after);
        if (bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a)) { perror("read interface stats"); close(map); return 1; }
        uint64_t bytes = family == AF_INET ? 35 : 55;
        printf("%s interface %s: RX +%llu packets/+%llu bytes, TX +%llu packets/+%llu bytes\n",
            after.rp > before.rp && after.tp > before.tp && after.rb >= before.rb + bytes && after.tb >= before.tb + bytes ? "PASS" : "FAIL",
            family == AF_INET ? "IPv4" : "IPv6", (unsigned long long)(after.rp-before.rp),
            (unsigned long long)(after.rb-before.rb), (unsigned long long)(after.tp-before.tp), (unsigned long long)(after.tb-before.tb));
        if (after.rp <= before.rp || after.tp <= before.tp || after.rb < before.rb + bytes || after.tb < before.tb + bytes) { close(map); return 1; }
    }
    close(map); return 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !strcmp(argv[1], "--iface")) return iface_probe();
    if (argc > 1 && !strcmp(argv[1], "--netd")) return netd_probe();
    int rc = 1, s = -1, dupfd = -1, other = -1, map = -1, prog = -1, pinned = -1;
    int cg = -1, tasks = -1, attached = 0, made_cgroup = 0, made_pin = 0, pipefd[2] = {-1, -1};
    uint64_t first = 0, again = 0, second = 0, child_cookie = 0;
    uint32_t key = 0;
    char path[128], pin[128], log[65536] = {0};
    struct counters initial = {0}, result = {0};
    union bpf_attr a = {0};
    snprintf(path, sizeof(path), "%s/a3lte_probe_%d", argc > 1 ? argv[1] : "/sys/fs/cgroup", getpid());
    snprintf(pin, sizeof(pin), "/sys/fs/bpf/a3lte_probe_%d", getpid());
    s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0); CHECK(s >= 0);
    dupfd = dup(s); CHECK(dupfd >= 0);
    other = socket(AF_INET6, SOCK_DGRAM | SOCK_CLOEXEC, 0); CHECK(other >= 0);
    CHECK(!cookie(s, &first) && first);
    CHECK(!cookie(dupfd, &again) && again == first);
    CHECK(!cookie(other, &second) && second && second != first);
    socklen_t short_len = 4;
    errno = 0;
    CHECK(getsockopt(s, SOL_SOCKET, SO_COOKIE, &again, &short_len) == -1 && errno == EINVAL);
    puts("PASS SO_COOKIE: stable, unique, dup, short-buffer rejection");
    a.map_type = BPF_MAP_TYPE_HASH; a.key_size = sizeof(key);
    a.value_size = sizeof(initial); a.max_entries = 8;
    map = bpf_cmd(BPF_MAP_CREATE, &a); CHECK(map >= 0);
    memset(&a, 0, sizeof(a)); a.map_fd = map; a.key = PTR(&key); a.value = PTR(&initial);
    CHECK(!bpf_cmd(BPF_MAP_UPDATE_ELEM, &a));
    a.value = PTR(&result); CHECK(!bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a));
    CHECK(!memcmp(&initial, &result, sizeof(result)));
    memset(&a, 0, sizeof(a)); a.bpf_fd = map; a.pathname = PTR(pin);
    CHECK(!bpf_cmd(BPF_OBJ_PIN, &a)); made_pin = 1;
    a.bpf_fd = 0; pinned = bpf_cmd(BPF_OBJ_GET, &a); CHECK(pinned >= 0);
    puts("PASS hash map update/lookup and bpffs pin/reopen");
    struct bpf_insn invalid[] = { I(BPF_JMP | BPF_EXIT, 0, 0, 0, 0) };
    errno = 0; CHECK(load(invalid, 1, log, sizeof(log)) < 0);
    puts("PASS verifier rejects uninitialized return register");
    struct bpf_insn insns[] = {
        I(BPF_ALU64 | BPF_MOV | BPF_X, 6, 1, 0, 0),
        I(BPF_ST | BPF_MEM | BPF_W, 10, 0, -4, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_X, 2, 10, 0, 0),
        I(BPF_ALU64 | BPF_ADD | BPF_K, 2, 0, 0, -4),
        I(BPF_LD | BPF_DW | BPF_IMM, 1, BPF_PSEUDO_MAP_FD, 0, map), I(0,0,0,0,0),
        I(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_map_lookup_elem),
        I(BPF_JMP | BPF_JEQ | BPF_K, 0, 0, 10, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_X, 7, 0, 0, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_X, 1, 6, 0, 0),
        I(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_socket_cookie),
        I(BPF_STX | BPF_MEM | BPF_DW, 7, 0, 0, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_X, 1, 6, 0, 0),
        I(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_get_socket_uid),
        I(BPF_STX | BPF_MEM | BPF_W, 7, 0, 8, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_K, 1, 0, 0, 1),
        I(BPF_STX | BPF_XADD | BPF_DW, 7, 1, 16, 0),
        I(BPF_ALU64 | BPF_MOV | BPF_K, 0, 0, 0, 1),
        I(BPF_JMP | BPF_EXIT, 0, 0, 0, 0),
    };
    memset(log, 0, sizeof(log)); prog = load(insns, sizeof(insns)/sizeof(insns[0]), log, sizeof(log));
    if (prog < 0) fprintf(stderr, "%s\n", log);
    CHECK(prog >= 0);
    CHECK(!mkdir(path, 0700)); made_cgroup = 1;
    cg = open(path, O_DIRECTORY | O_RDONLY | O_CLOEXEC); CHECK(cg >= 0);
    memset(&a, 0, sizeof(a)); a.target_fd = cg; a.attach_bpf_fd = prog;
    a.attach_type = BPF_CGROUP_INET_EGRESS;
    if (bpf_cmd(BPF_PROG_ATTACH, &a)) {
        if (errno == EPERM) {
            puts("SKIP isolated cgroup override: protected parent program; use --netd to verify active accounting");
            rc = 0; goto out;
        }
        CHECK(0);
    }
    attached = 1;
    CHECK(!pipe(pipefd));
    pid_t pid = fork(); CHECK(pid >= 0);
    if (!pid) {
        char target[160], value[32];
        snprintf(target, sizeof(target), "%s/cgroup.procs", path);
        int fd = open(target, O_WRONLY | O_CLOEXEC);
        int n = snprintf(value, sizeof(value), "%d", getpid());
        if (fd < 0 || write(fd, value, n) != n) _exit(2);
        close(fd);
        int udp = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        uint64_t c = 0;
        struct sockaddr_in dst = {.sin_family=AF_INET, .sin_port=htons(9), .sin_addr.s_addr=htonl(INADDR_LOOPBACK)};
        if (udp < 0 || cookie(udp, &c) || sendto(udp, "probe", 5, 0, (void *)&dst, sizeof(dst)) != 5) _exit(3);
        if (write(pipefd[1], &c, sizeof(c)) != sizeof(c)) _exit(4);
        close(udp); _exit(0);
    }
    close(pipefd[1]); pipefd[1] = -1;
    int status = 0; CHECK(waitpid(pid, &status, 0) == pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    CHECK(read(pipefd[0], &child_cookie, sizeof(child_cookie)) == sizeof(child_cookie));
    memset(&a, 0, sizeof(a)); a.map_fd = map; a.key = PTR(&key); a.value = PTR(&result);
    CHECK(!bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a));
    CHECK(result.cookie == child_cookie && result.uid == getuid() && result.packets >= 1);
    puts("PASS cgroup v2 egress: real UDP packet, matching SO_COOKIE/UID, atomic 64-bit packet count");
    a.value = 0;
    CHECK(!bpf_cmd(BPF_MAP_DELETE_ELEM, &a));
    a.value = PTR(&result);
    errno = 0; CHECK(bpf_cmd(BPF_MAP_LOOKUP_ELEM, &a) < 0 && errno == ENOENT);
    puts("PASS map deletion"); rc = 0;
out:
    if (attached) { memset(&a, 0, sizeof(a)); a.target_fd=cg; a.attach_type=BPF_CGROUP_INET_EGRESS; bpf_cmd(BPF_PROG_DETACH, &a); }
    if (cg >= 0) close(cg);
    if (made_cgroup) rmdir(path);
    if (made_pin) unlink(pin);
    int fds[] = {s, dupfd, other, map, prog, pinned, tasks, pipefd[0], pipefd[1]};
    for (size_t i = 0; i < sizeof(fds)/sizeof(fds[0]); ++i) if (fds[i] >= 0) close(fds[i]);
    return rc;
}
