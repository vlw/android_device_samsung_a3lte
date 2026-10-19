// SPDX-License-Identifier: Apache-2.0
#include <errno.h>
#include <limits.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#ifndef NETLINK_CAP_ACK
#define NETLINK_CAP_ACK 10
#endif
#ifndef NLM_F_CAPPED
#define NLM_F_CAPPED 0x100
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s errno=%d\n", __LINE__, #x, errno); return 1; } } while (0)
int main(void) {
    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
    CHECK(fd >= 0);
    struct timeval timeout = {.tv_sec = 3};
    CHECK(!setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)));
    struct sockaddr_nl peer = {.nl_family = AF_NETLINK};
    CHECK(!bind(fd, (struct sockaddr*)&peer, sizeof(peer)));
    int value = -1;
    socklen_t len = sizeof(value);
    int initial = getsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &value, &len);
    printf("Initial CAP_ACK: ret=%d value=%d len=%u errno=%d\n", initial, value, len, errno);
    CHECK(!initial && (value == 0 || value == 1));
    CHECK(setsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &value, 1) == -1 && errno == EINVAL);
    for (int cap = 0; cap <= 1; ++cap) {
        CHECK(!setsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &cap, sizeof(cap)));
        len = sizeof(value);
        CHECK(!getsockopt(fd, SOL_NETLINK, NETLINK_CAP_ACK, &value, &len) && value == cap);
        struct {
            struct nlmsghdr header;
            struct ifinfomsg info;
        } request = {
            .header = {.nlmsg_len = sizeof(request), .nlmsg_type = RTM_GETLINK,
                .nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK, .nlmsg_seq = 1234},
            .info = {.ifi_family = AF_UNSPEC, .ifi_index = INT_MAX},
        };
        CHECK(sendto(fd, &request, sizeof(request), 0, (struct sockaddr*)&peer, sizeof(peer)) == sizeof(request));
        char buffer[512];
        ssize_t bytes = recv(fd, buffer, sizeof(buffer), 0);
        CHECK(bytes >= (ssize_t)NLMSG_LENGTH(sizeof(struct nlmsgerr)));
        struct nlmsghdr *reply = (struct nlmsghdr*)buffer;
        CHECK(reply->nlmsg_type == NLMSG_ERROR && reply->nlmsg_seq == request.header.nlmsg_seq);
        struct nlmsgerr *error = NLMSG_DATA(reply);
        CHECK(error->error == -ENODEV);
        CHECK(!memcmp(&error->msg, &request.header, sizeof(request.header)));
        CHECK(!!(reply->nlmsg_flags & NLM_F_CAPPED) == cap);
        CHECK(reply->nlmsg_len == NLMSG_LENGTH(sizeof(*error) + (cap ? 0 : sizeof(request.info))));
        if (!cap) CHECK(!memcmp(&error->msg, &request, sizeof(request)));
        printf("PASS CAP_ACK=%d: error=%d reply=%u bytes, original header preserved\n", cap, error->error, reply->nlmsg_len);
    }
    close(fd);
}
