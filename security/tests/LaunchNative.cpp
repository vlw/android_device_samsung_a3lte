// SPDX-License-Identifier: Apache-2.0
// Reproduce init's UID/groups/domain for the one fixed service during live tests.
#include <grp.h>
#include <unistd.h>
#include <selinux/selinux.h>
#include <cstdio>
int main() {
    gid_t groups[]={1000,1026}; // system, drmrpc
    if (getuid()!=0 || setgroups(2,groups) || setgid(1000) || setuid(1000) ||
        setcon("u:r:hal_keymaster_default:s0")) { perror("HAL credentials"); return 1; }
    execl("/vendor/bin/hw/vendor.a3tee.keymaster@3.0-service",
          "vendor.a3tee.keymaster@3.0-service",nullptr);
    perror("HAL exec"); return 1;
}
