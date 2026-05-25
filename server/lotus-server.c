/*
 * SPDX-FileCopyrightText: 2025 Võ Ngô Hoàng Thành <thanhpy2009@gmail.com>
 * SPDX-FileCopyrightText: 2026 Nguyễn Hoàng Kỳ  <nhktmdzhg@gmail.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "lotus-server.h"

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <linux/uinput.h>
#include <libinput.h>
#include <libudev.h>

static atomic_bool g_running = true;

static void signal_handler(int sig) {
    (void)sig;
    atomic_store(&g_running, false);
}

static int open_restricted(const char *path, int flags, void *user_data) {
    (void)user_data;
    int fd = open(path, flags);
    return fd < 0 ? -errno : fd;
}

static void close_restricted(int fd, void *user_data) {
    (void)user_data;
    close(fd);
}

static const struct libinput_interface li_interface = {
    .open_restricted  = open_restricted,
    .close_restricted = close_restricted,
};

static int make_abstract_socket(int type, const char *name, socklen_t *out_len) {
    int fd = socket(AF_UNIX, type, 0);
    if (fd < 0) return -1;
    struct sockaddr_un addr = {};
    addr.sun_family = AF_UNIX;
    size_t nlen = strlen(name);
    addr.sun_path[0] = '\0';
    memcpy(&addr.sun_path[1], name, nlen);
    socklen_t len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + nlen + 1);
    if (out_len) *out_len = len;
    if (bind(fd, (struct sockaddr *)&addr, len) != 0) { close(fd); return -1; }
    return fd;
}

static int uinput_init(void) {
    int fd = open("/dev/uinput", O_WRONLY);
    if (fd < 0) return -1;

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_KEYBIT, KEY_BACKSPACE);

    struct uinput_setup usetup = {};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strncpy(usetup.name, "Lotus-Uinput-Server", UINPUT_MAX_NAME_SIZE - 1);

    if (ioctl(fd, UI_DEV_SETUP, &usetup) < 0 || ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    sleep(1);
    return fd;
}

static bool is_backspace_packet(const struct input_event *buf, int count) {
    if (count <= 0 || count % 4 != 0) return false;
    for (int i = 0; i < count; i += 4) {
        if (buf[i].type != EV_KEY || buf[i].code != KEY_BACKSPACE || buf[i].value != 1 ||
            buf[i + 1].type != EV_SYN || buf[i + 1].code != SYN_REPORT ||
            buf[i + 2].type != EV_KEY || buf[i + 2].code != KEY_BACKSPACE || buf[i + 2].value != 0 ||
            buf[i + 3].type != EV_SYN || buf[i + 3].code != SYN_REPORT) {
            return false;
        }
    }
    return true;
}

static void sleep_ms(int delay_ms) {
    if (delay_ms <= 0) return;
    usleep((useconds_t)delay_ms * 1000);
}

static void write_backspace_packet(int uinput_fd, struct input_event *buf, int count) {
    int delay_bs_ms = (int)buf[0].time.tv_sec;
    int commit_delay_ms = (int)buf[0].time.tv_usec;
    int bs_count = count / 4;

    for (int i = 0; i < count; i++) {
        buf[i].time.tv_sec = 0;
        buf[i].time.tv_usec = 0;
    }

    for (int bs = 0; bs < bs_count; bs++) {
        for (int i = bs * 4; i < (bs + 1) * 4; i++) {
            ssize_t w = write(uinput_fd, &buf[i], sizeof(buf[i]));
            (void)w;
        }
        if (bs + 1 < bs_count) {
            sleep_ms((bs + 2 == bs_count) ? commit_delay_ms : delay_bs_ms);
        }
    }
}

int main(void) {
    struct sigaction sa = {};
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT,  &sa, NULL);

    struct sched_param sp = { .sched_priority = 10 };
    sched_setscheduler(0, SCHED_FIFO, &sp);

    int uinput_fd = uinput_init();
    if (uinput_fd < 0) { perror("uinput_init"); return 1; }

    socklen_t kb_len, mouse_server_len;
    int kb_fd = make_abstract_socket(SOCK_DGRAM, KB_SOCKET_NAME, &kb_len);
    if (kb_fd < 0) { perror("kb socket"); return 1; }

    int mouse_server_fd = make_abstract_socket(SOCK_SEQPACKET | SOCK_NONBLOCK, MOUSE_SOCKET_NAME, &mouse_server_len);
    if (mouse_server_fd < 0) { perror("mouse socket"); return 1; }
    listen(mouse_server_fd, 5);

    struct udev *udev = udev_new();
    if (!udev) return 1;
    struct libinput *li = libinput_udev_create_context(&li_interface, NULL, udev);
    if (!li || libinput_udev_assign_seat(li, "seat0") != 0) return 1;

    int mouse_client_fd = -1;

    /* fds: 0=kb(dgram) 1=libinput 2=mouse_server 3=mouse_client */
    struct pollfd fds[4] = {
        { kb_fd,           POLLIN, 0 },
        { libinput_get_fd(li), POLLIN, 0 },
        { mouse_server_fd, POLLIN, 0 },
        { -1,              POLLIN, 0 },
    };

    while (atomic_load(&g_running)) {
        int ret = poll(fds, 4, -1);
        if (ret < 0) { if (errno == EINTR) continue; break; }

        /* KB: receive input_event, write to uinput */
        if (fds[0].revents & POLLIN) {
            struct input_event buf[64];
            struct sockaddr_un client_addr;
            socklen_t clen = sizeof(client_addr);
            ssize_t n = recvfrom(kb_fd, buf, sizeof(buf), 0,
                                 (struct sockaddr *)&client_addr, &clen);
            if (n > 0 && n % (ssize_t)sizeof(struct input_event) == 0) {
                int count = (int)(n / (ssize_t)sizeof(struct input_event));
                if (is_backspace_packet(buf, count)) {
                    write_backspace_packet(uinput_fd, buf, count);
                } else {
                    for (int i = 0; i < count; i++) {
                        ssize_t w = write(uinput_fd, &buf[i], sizeof(buf[i]));
                        (void)w;
                    }
                }
                /* ack: only reaches client if it bound a return address */
                sendto(kb_fd, "A", 1, MSG_DONTWAIT,
                       (struct sockaddr *)&client_addr, clen);
            }
        }

        /* libinput: mouse click → notify client */
        if (fds[1].revents & POLLIN) {
            libinput_dispatch(li);
            struct libinput_event *event;
            while ((event = libinput_get_event(li)) != NULL) {
                enum libinput_event_type type = libinput_event_get_type(event);
                if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
                    struct libinput_event_pointer *p = libinput_event_get_pointer_event(event);
                    if (libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED
                        && mouse_client_fd >= 0)
                    {
                        if (send(mouse_client_fd, "C", 1, MSG_NOSIGNAL | MSG_DONTWAIT) <= 0) {
                            close(mouse_client_fd);
                            mouse_client_fd = -1;
                            fds[3].fd = -1;
                        }
                    }
                } else if (type == LIBINPUT_EVENT_DEVICE_ADDED) {
                    struct libinput_device *dev = libinput_event_get_device(event);
                    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
                        libinput_device_config_tap_set_enabled(dev, LIBINPUT_CONFIG_TAP_ENABLED);
                        libinput_device_config_tap_set_button_map(dev, LIBINPUT_CONFIG_TAP_MAP_LRM);
                    }
                }
                libinput_event_destroy(event);
            }
        }

        /* mouse server: accept new client */
        if (fds[2].revents & POLLIN) {
            int new_fd = accept4(mouse_server_fd, NULL, NULL, SOCK_NONBLOCK);
            if (new_fd >= 0) {
                if (mouse_client_fd >= 0) close(mouse_client_fd);
                mouse_client_fd = new_fd;
                fds[3].fd = mouse_client_fd;
            }
        }

        /* mouse client: handle disconnect */
        if (fds[3].fd >= 0 && (fds[3].revents & (POLLHUP | POLLERR))) {
            close(mouse_client_fd);
            mouse_client_fd = -1;
            fds[3].fd = -1;
        }
    }

    if (mouse_client_fd >= 0) close(mouse_client_fd);
    close(mouse_server_fd);
    close(kb_fd);
    ioctl(uinput_fd, UI_DEV_DESTROY);
    close(uinput_fd);
    libinput_unref(li);
    udev_unref(udev);
    return 0;
}
