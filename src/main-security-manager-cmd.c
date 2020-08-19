/*
 * Copyright (C) 2018 "IoT.bzh"
 * Author José Bollo <jose.bollo@iot.bzh>
 * Author Arthur Guyader <arthur.guyader@iot.bzh>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>

#include "log.h"
#include "security-manager.h"
#include "utils.h"

#define DEFAULT_CACHE_SIZE 5000

#define _ECHO_ 'e'
#define _HELP_ 'h'
#define _SOCKET_ 's'
#define _VERSION_ 'v'

static const char shortopts[] = "c:ehs:v";

static const struct option longopts[] = {{"echo", 0, NULL, _ECHO_},
                                         {"help", 0, NULL, _HELP_},
                                         {"socket", 1, NULL, _SOCKET_},
                                         {"version", 0, NULL, _VERSION_},
                                         {NULL, 0, NULL, 0}};

static const char helptxt[] =
    "\n"
    "usage: security-manager-cmd [options]... [action [arguments]]\n"
    "\n"
    "otpions:\n"
    "	-s, --socket xxx      set the base xxx for sockets\n"
    "	-e, --echo            print the evaluated command\n"
    "	-h, --help            print this help and exit\n"
    "	-v, --version         print the version and exit\n"
    "\n"
    "When action is given, security-manager-cmd performs the action and exits.\n"
    "Otherwise security-manager-cmd continuously read its input to get the actions.\n"
    "For a list of actions type 'security-manager-cmd help'.\n"
    "\n";

static const char versiontxt[] = "security-manager-cmd version 0.1\n";

static const char help_log_text[] =
    "\n"
    "Command: log [on|off]\n"
    "\n"
    "With the 'on' or 'off' arguments, set the logging state to what required.\n"
    "In all cases, prints the logging state.\n"
    "\n"
    "Examples:\n"
    "\n"
    "  log on                  activates the logging\n"
    "\n";

static const char help_clean_text[] =
    "\n"
    "Command: clean\n"
    "\n"
    "Clean the actual handle of application\n"
    "\n";

static const char help_id_text[] =
    "\n"
    "Command: id app_id\n"
    "\n"
    "Set the id of the application\n"
    "\n"
    "Example : id agl-service-can-low-level\n"
    "\n";

static const char help_path_text[] =
    "\n"
    "Command: path path path_type\n"
    "\n"
    "Add a path for the application\n"
    "\n"
    "Path type value :\n"
    "   - lib\n"
    "   - conf\n"
    "   - exec\n"
    "   - icon\n"
    "   - data\n"
    "   - http\n"
    "   - log\n"
    "   - tmp\n"
    "\n"
    "Example : path /tmp/file tmp\n"
    "\n";

static const char help_permission_text[] =
    "\n"
    "Command: permission permission\n"
    "\n"
    "Add a permission for the application\n"
    "WARNING : You need to set id before\n"
    "\n"
    "Example : permission urn:AGL:permission::partner:scope-platform\n"
    "\n";

static const char help_install_text[] =
    "\n"
    "Command: install\n"
    "\n"
    "Install application\n"
    "WARNING : You need to set id before\n"
    "\n";

static const char help_uninstall_text[] =
    "\n"
    "Command: uninstall\n"
    "\n"
    "Uninstall application\n"
    "WARNING : You need to set id before\n"
    "\n";

static const char help__text[] =
    "\n"
    "Commands are: log, clean, display, id, path, permission, install, uninstall, quit, help\n"
    "Type 'help command' to get help on the command\n"
    "\n"
    "Example 'help log' to get help on log\n"
    "\n";

static const char help_quit_text[] =
    "\n"
    "Command: quit\n"
    "\n"
    "Quit the program\n"
    "\n";

static const char help_help_text[] =
    "\n"
    "Command: help [command]\n"
    "\n"
    "Gives help on the command.\n"
    "\n"
    "Available commands: log, clean, display, id, path, permission, install, uninstall, quit, help\n"
    "\n";

static security_manager_t *security_manager = NULL;
static char buffer[4000] = {0};
static char *str[40] = {0};
static size_t bufill = 0;
static int nstr = 0;
static int echo = 0;
static int last_status = 0;
static int id_set = 0;

int plink(int ac, char **av, int *used, int maxi) {
    int r = 0;

    if (maxi < ac)
        ac = maxi;
    while (r < ac && strcmp(av[r], ";")) r++;

    *used = r + (r < ac);
    return r;
}

int do_clean(int ac, char **av) {
    int uc, rc;
    int n = plink(ac, av, &uc, 1);

    if (n < 1) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    last_status = rc = security_manager_clean(security_manager);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        id_set = 0;
        LOG("clean success");
    }

    return uc;
}

int do_display(int ac, char **av) {
    int uc, rc;
    int n = plink(ac, av, &uc, 1);

    if (n < 1) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    last_status = rc = security_manager_display(security_manager);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    }

    return uc;
}

int do_id(int ac, char **av) {
    int uc, rc;
    char *id = NULL;
    int n = plink(ac, av, &uc, 2);

    if (n < 2) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    if (strlen(av[1]) <= 0) {
        ERROR("bad argument %s", av[1]);
        last_status = -EINVAL;
        return uc;
    }

    id = av[1];
    last_status = rc = security_manager_set_id(security_manager, id);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        id_set = 1;
        LOG("id %s", rc ? "set" : "already set");
    }

    return uc;
}

int do_path(int ac, char **av) {
    int uc, rc;
    char *path = NULL;
    char *path_type = NULL;
    int n = plink(ac, av, &uc, 3);

    if (n < 3) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    if (strlen(av[1]) <= 0) {
        ERROR("bad argument %s", av[1]);
        last_status = -EINVAL;
        return uc;
    }

    path = av[1];
    path_type = av[2];

    last_status = rc = security_manager_add_path(security_manager, path, path_type);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        LOG("add path '%s' with type %s", path, path_type);
    }

    return uc;
}

int do_permission(int ac, char **av) {
    int uc, rc;
    char *permission = NULL;
    int n = plink(ac, av, &uc, 2);

    if (!id_set) {
        LOG("set id before set permission");
        return uc;
    }

    if (n < 2) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    if (strlen(av[1]) <= 0) {
        ERROR("bad argument %s", av[1]);
        last_status = -EINVAL;
        return uc;
    }

    permission = av[1];

    last_status = rc = security_manager_add_permission(security_manager, permission);
    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        LOG("add permission %s", permission);
    }

    return uc;
}

int do_install(int ac, char **av) {
    int uc, rc;
    int n = plink(ac, av, &uc, 1);

    if (n < 1) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    last_status = rc = security_manager_install(security_manager);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        LOG("install success");
    }

    return uc;
}

int do_uninstall(int ac, char **av) {
    int uc, rc;
    int n = plink(ac, av, &uc, 1);

    if (n < 1) {
        ERROR("not enough arguments");
        last_status = -EINVAL;
        return uc;
    }

    last_status = rc = security_manager_uninstall(security_manager);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        LOG("uninstall success");
    }

    return uc;
}

int do_log(int ac, char **av) {
    int uc, rc;
    int on = 0, off = 0;
    int n = plink(ac, av, &uc, 2);

    if (n > 1) {
        on = !strcmp(av[1], "on");
        off = !strcmp(av[1], "off");
        if (!on && !off) {
            fprintf(stderr, "bad argument '%s'\n", av[1]);
            return uc;
        }
    }

    last_status = rc = security_manager_log(security_manager, on, off);

    if (rc < 0) {
        ERROR("%s", strerror(-rc));
    } else {
        LOG("logging %s", rc ? "on" : "off");
    }

    return uc;
}

int do_help(int ac, char **av) {
    if (ac > 1 && !strcmp(av[1], "log"))
        fprintf(stdout, "%s", help_log_text);
    else if (ac > 1 && !strcmp(av[1], "quit"))
        fprintf(stdout, "%s", help_quit_text);
    else if (ac > 1 && !strcmp(av[1], "help"))
        fprintf(stdout, "%s", help_help_text);
    else if (ac > 1 && !strcmp(av[1], "clean"))
        fprintf(stdout, "%s", help_clean_text);
    else if (ac > 1 && !strcmp(av[1], "id"))
        fprintf(stdout, "%s", help_id_text);
    else if (ac > 1 && !strcmp(av[1], "path"))
        fprintf(stdout, "%s", help_path_text);
    else if (ac > 1 && !strcmp(av[1], "permission"))
        fprintf(stdout, "%s", help_permission_text);
    else if (ac > 1 && !strcmp(av[1], "install"))
        fprintf(stdout, "%s", help_install_text);
    else if (ac > 1 && !strcmp(av[1], "uninstall"))
        fprintf(stdout, "%s", help_uninstall_text);
    else {
        fprintf(stdout, "%s", help__text);
        return 1;
    }
    return 2;
}

int do_any(int ac, char **av) {
    if (!ac)
        return 0;

    if (!strcmp(av[0], "log"))
        return do_log(ac, av);

    if (!strcmp(av[0], "clean"))
        return do_clean(ac, av);

    if (!strcmp(av[0], "display"))
        return do_display(ac, av);

    if (!strcmp(av[0], "id"))
        return do_id(ac, av);

    if (!strcmp(av[0], "path"))
        return do_path(ac, av);

    if (!strcmp(av[0], "permission"))
        return do_permission(ac, av);

    if (!strcmp(av[0], "install"))
        return do_install(ac, av);

    if (!strcmp(av[0], "uninstall"))
        return do_uninstall(ac, av);

    if (!strcmp(av[0], "quit"))
        exit(0);

    if (!strcmp(av[0], "help") || !strcmp(av[0], "?"))
        return do_help(ac, av);

    fprintf(stderr, "unknown command %s (try help)\n", av[0]);
    return 1;
}

void do_all(int ac, char **av, int quit) {
    int rc;

    if (echo) {
        for (rc = 0; rc < ac; rc++) fprintf(stdout, "%s%s", rc ? " " : "", av[rc]);
        fprintf(stdout, "\n");
    }
    while (ac) {
        last_status = 0;
        rc = do_any(ac, av);
        if (quit && (rc <= 0 || last_status < 0))
            exit(1);
        ac -= rc;
        av += rc;
    }
}

int main(int ac, char **av) {
    int opt;
    int rc;
    int help = 0;
    int version = 0;
    int error = 0;
    char *socket = NULL;
    struct pollfd fds[2];
    char *p;

    setlinebuf(stdout);

    /* scan arguments */
    for (;;) {
        opt = getopt_long(ac, av, shortopts, longopts, NULL);
        if (opt == -1)
            break;

        switch (opt) {
            case _ECHO_:
                echo = 1;
                break;
            case _HELP_:
                help = 1;
                break;
            case _SOCKET_:
                socket = optarg;
                break;
            case _VERSION_:
                version = 1;
                break;
            default:
                error = 1;
                break;
        }
    }

    /* handles help, version, error */
    if (help) {
        fprintf(stdout, helptxt);
        return 0;
    }

    if (version) {
        fprintf(stdout, versiontxt);
        return 0;
    }

    if (error)
        return 1;

    /* initialize server */
    signal(SIGPIPE, SIG_IGN); /* avoid SIGPIPE! */
    rc = security_manager_create(&security_manager, socket);
    if (rc < 0) {
        fprintf(stderr, "initialization failed: %s\n", strerror(-rc));
        return 1;
    }

    LOG("security_manager_create success");

    if (optind < ac) {
        do_all(ac - optind, av + optind, 1);
        return 0;
    }

    fcntl(0, F_SETFL, O_NONBLOCK);
    bufill = 0;
    fds[0].fd = 0;
    fds[0].events = fds[1].events = POLLIN;
    for (;;) {
        rc = poll(fds, 2, -1);
        if (fds[0].revents & POLLIN) {
            rc = (int)read(0, &buffer[bufill], sizeof buffer - bufill);
            if (rc == 0)
                break;
            if (rc > 0) {
                bufill += (size_t)rc;
                while ((p = memchr(buffer, '\n', bufill))) {
                    /* process one line */
                    *p++ = 0;
                    str[nstr = 0] = strtok(buffer, " \t");
                    while (str[nstr]) str[++nstr] = strtok(NULL, " \t");
                    do_all(nstr, str, 0);
                    bufill -= (size_t)(p - buffer);
                    if (!bufill)
                        break;
                    memmove(buffer, p, bufill);
                }
            }
        }
    }
    return 0;
}