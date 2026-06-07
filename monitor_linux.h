#pragma once
#include <stdint.h>
#include <sys/socket.h>
#include <ifaddrs.h>

int  linux_start_monitor(uintptr_t cb_hnd);
void linux_stop_monitor(int sock);
