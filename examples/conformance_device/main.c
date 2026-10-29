/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "conformance_device.h"
#include "conformance_profiles.h"
#include "iolinki/phy_virtual.h"

static void usage(const char* argv0)
{
    printf("Usage: %s --phy <tty|pty> [--profile full|config|pdout-only|minimal] [--nvm <path>]\n",
           argv0);
    printf("  The process reads control commands from stdin:\n");
    printf("    event <code> <error|warning|notification>\n");
    printf("    clearevent <code>\n");
    printf("    pdvalid <0|1>\n");
    printf("    localchange\n");
    printf("    powercycle\n");
    printf("    resetinfo\n");
    printf("    factoryreset\n");
    printf("    backtobox\n");
    printf("    dump\n");
    printf("    quit\n");
}

static int parse_event_type(const char* name, iolink_event_type_t* out)
{
    if (strcmp(name, "error") == 0) {
        *out = IOLINK_EVENT_TYPE_ERROR;
        return 0;
    }
    if (strcmp(name, "warning") == 0) {
        *out = IOLINK_EVENT_TYPE_WARNING;
        return 0;
    }
    if (strcmp(name, "notification") == 0) {
        *out = IOLINK_EVENT_TYPE_NOTIFICATION;
        return 0;
    }
    return -1;
}

static const char* reset_reason_name(conformance_reset_reason_t reason)
{
    switch (reason) {
        case CONFORMANCE_RESET_DEVICE:
            return "device";
        case CONFORMANCE_RESET_APPLICATION:
            return "application";
        case CONFORMANCE_RESET_FACTORY:
            return "factory";
        case CONFORMANCE_RESET_BACK_TO_BOX:
            return "backtobox";
        case CONFORMANCE_RESET_POWER_ON:
        default:
            return "power_on";
    }
}

static void print_reset_observation(const conformance_device_t* app)
{
    const conformance_reset_observation_t* obs = conformance_device_reset_observation(app);
    if (obs == NULL) {
        printf("resetinfo=err\n");
        return;
    }
    printf(
        "resetinfo=startup=%lu device=%lu application=%lu factory=%lu backtobox=%lu "
        "reason=%s defaults=%d\n",
        (unsigned long) obs->startup_count, (unsigned long) obs->device_reset_count,
        (unsigned long) obs->application_reset_count, (unsigned long) obs->factory_reset_count,
        (unsigned long) obs->back_to_box_count, reset_reason_name(obs->last_reset_reason),
        conformance_device_is_default_state(app) ? 1 : 0);
}

static void handle_command(conformance_device_t* app, const iolink_phy_api_t* phy,
                           const char* nvm_path, const char* line, bool* quit)
{
    char command[32] = {0};
    char arg1[32] = {0};
    char arg2[32] = {0};
    const int fields = sscanf(line, "%31s %31s %31s", command, arg1, arg2);
    if (fields <= 0) {
        return;
    }
    if (strcmp(command, "quit") == 0) {
        *quit = true;
    }
    else if ((strcmp(command, "event") == 0) && (fields >= 3)) {
        iolink_event_type_t type = IOLINK_EVENT_TYPE_WARNING;
        if (parse_event_type(arg2, &type) == 0) {
            const unsigned long code = strtoul(arg1, NULL, 0);
            if (conformance_device_inject_event(app, (uint16_t) code, type) == 0) {
                printf("event injected: 0x%04lX\n", code);
            }
        }
    }
    else if ((strcmp(command, "clearevent") == 0) && (fields >= 2)) {
        const unsigned long code = strtoul(arg1, NULL, 0);
        printf("clearevent=%d\n", conformance_device_clear_event(app, (uint16_t) code));
    }
    else if ((strcmp(command, "pdvalid") == 0) && (fields >= 2)) {
        conformance_device_set_pd_valid(app, arg1[0] != '0');
        printf("pdvalid=%s\n", arg1[0] != '0' ? "true" : "false");
    }
    else if (strcmp(command, "localchange") == 0) {
        printf("localchange=%d\n", conformance_device_local_change(app));
    }
    else if (strcmp(command, "powercycle") == 0) {
        const int rc = conformance_device_power_cycle(app, phy, nvm_path);
        printf("powercycle=%d startup=%lu reason=power_on\n", rc,
               (unsigned long) conformance_device_reset_observation(app)->startup_count);
    }
    else if (strcmp(command, "resetinfo") == 0) {
        print_reset_observation(app);
    }
    else if (strcmp(command, "factoryreset") == 0) {
        conformance_device_note_factory_reset(app);
        print_reset_observation(app);
    }
    else if (strcmp(command, "backtobox") == 0) {
        conformance_device_note_back_to_box(app);
        print_reset_observation(app);
    }
    else if (strcmp(command, "dump") == 0) {
#if IOLINK_FEATURE_DATA_STORAGE
        printf("state=%d ds_state=%d counter=%u config1=%zu config3=%zu\n",
               (int) iolink_device_get_state(&app->device), (int) app->device.dll.ds.state,
               (unsigned) app->counter, app->config1_len, app->config3_len);
#else
        printf("state=%d ds_state=n/a counter=%u config1=%zu config3=%zu\n",
               (int) iolink_device_get_state(&app->device), (unsigned) app->counter,
               app->config1_len, app->config3_len);
#endif
        print_reset_observation(app);
    }
    else {
        printf("unknown command\n");
    }
}

int main(int argc, char** argv)
{
    (void) setvbuf(stdout, NULL, _IOLBF, 0);
    const char* phy_path = NULL;
    const char* nvm_path = "/tmp/iolink-conformance.nvm";
    const char* profile_name = "full";

    for (int i = 1; i < argc; i++) {
        if ((strcmp(argv[i], "--phy") == 0) && ((i + 1) < argc)) {
            phy_path = argv[++i];
        }
        else if ((strcmp(argv[i], "--nvm") == 0) && ((i + 1) < argc)) {
            nvm_path = argv[++i];
        }
        else if ((strcmp(argv[i], "--profile") == 0) && ((i + 1) < argc)) {
            profile_name = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        else {
            usage(argv[0]);
            return -1;
        }
    }

    if (phy_path == NULL) {
        usage(argv[0]);
        return -1;
    }
    const conformance_profile_t* profile = conformance_profile_from_name(profile_name);
    if (profile == NULL) {
        fprintf(stderr, "Unknown profile: %s\n", profile_name);
        return -1;
    }

    iolink_phy_virtual_set_port(phy_path);
    const iolink_phy_api_t* phy = iolink_phy_virtual_get();

    conformance_device_t app;
    if (conformance_device_init(&app, profile, phy, nvm_path) != 0) {
        fprintf(stderr, "Failed to initialize conformance device\n");
        return -1;
    }

    printf("conformance device ready: profile=%s phy=%s nvm=%s\n", profile->name, phy_path,
           nvm_path);
    printf("pd_in=%u pd_out=%u\n", (unsigned) profile->stack.pd_in_len,
           (unsigned) profile->stack.pd_out_len);

    bool quit = false;
    while (!quit) {
        conformance_device_process(&app);
        (void) conformance_device_tick(&app, false);

        struct pollfd pfd;
        pfd.fd = STDIN_FILENO;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (poll(&pfd, 1, 0) > 0) {
            char line[128];
            if (fgets(line, sizeof(line), stdin) != NULL) {
                handle_command(&app, phy, nvm_path, line, &quit);
            }
        }
        usleep(1000);
    }

    conformance_device_close(&app);
    return 0;
}
