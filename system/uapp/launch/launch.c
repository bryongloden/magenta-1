// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <launchpad/launchpad.h>
#include <launchpad/vmo.h>
#include <magenta/processargs.h>
#include <magenta/syscalls.h>
#include <mxio/util.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void option_usage(FILE* out,
                         const char* option, const char* description) {
    fprintf(out, "\t%-16s%s\n", option, description);
}

static _Noreturn void usage(const char* progname, bool error) {
    FILE* out = error ? stderr : stdout;
    fprintf(out, "Usage: %s [OPTIONS] [--] PROGRAM [ARGS...]\n", progname);
    option_usage(out, "-b", "use basic ELF loading, no PT_INTERP support");
    option_usage(out, "-d FD", "pass FD with the same descriptor number");
    option_usage(out, "-d FD:NEWFD", "pass FD as descriptor number NEWFD");
    option_usage(out, "-e VAR=VALUE", "pass environment variable");
    option_usage(out, "-f FILE", "execute FILE but pass PROGRAM as argv[0]");
    option_usage(out, "-F FD", "execute FD");
    option_usage(out, "-h", "display this usage message and exit");
    option_usage(out, "-l",
                 "pass mxio_loader_service handle in main bootstrap message");
    option_usage(out, "-L", "force initial loader bootstrap message");
    option_usage(out, "-r", "send mxio filesystem root");
    option_usage(out, "-s", "shorthand for -r -d 0 -d 1 -d 2");
    option_usage(out, "-v FILE", "send VMO of FILE as EXEC_VMO handle");
    option_usage(out, "-V FD", "send VMO of FD as EXEC_VMO handle");
    exit(error ? 1 : 0);
}

static _Noreturn void fail(const char* call, mx_status_t status) {
    fprintf(stderr, "%s failed: %d\n", call, status);
    exit(1);
}

static void check(const char* call, mx_status_t status) {
    if (status < 0)
        fail(call, status);
}

int main(int argc, char** argv) {
    const char** env = NULL;
    const char** tmpenv = NULL;
    size_t envsize = 0;
    const char* program = NULL;
    int program_fd = -1;
    bool basic = false;
    bool send_root = false;
    struct fd { int from, to; } *fds = NULL;
    struct fd { int from, to; } *tmpfds = NULL
    size_t nfds = 0;
    bool send_loader_message = false;
    bool pass_loader_handle = false;
    const char* exec_vmo_file = NULL;
    int exec_vmo_fd = -1;

    for (int opt; (opt = getopt(argc, argv, "bd:e:f:F:hlLrsv:")) != -1;) {
        switch (opt) {
        case 'b':
            basic = true;
            break;
        case 'd':;
            int from, to;
            switch (sscanf(optarg, "%u:%u", &from, &to)) {
            default:
                usage(argv[0], true);
                break;
            case 1:
                to = from;
                // Fall through.
            case 2:
                tmpfds = realloc(fds, ++nfds * sizeof(fds[0]));
                if (tmpfds == NULL) {
                    perror("realloc");
                    return 2;
                }
                fds = tmpfds;
                fds[nfds - 1].from = from;
                fds[nfds - 1].to = to;
                break;
            }
            break;
        case 'e':
            tmpenv = realloc(env, (++envsize + 1) * sizeof(env[0]));
            if (tmpenv == NULL) {
                perror("realloc");
                return 2;
            }
            env = tmpenv;
            env[envsize - 1] = optarg;
            env[envsize] = NULL;
            break;
        case 'f':
            program = optarg;
            break;
        case 'F':
            if (sscanf(optarg, "%u", &program_fd) != 1)
                usage(argv[0], true);
            break;
        case 'h':
            usage(argv[0], false);
            break;
        case 'L':
            send_loader_message = true;
            break;
        case 'l':
            pass_loader_handle = true;
            break;
        case 'r':
            send_root = true;
            break;
        case 's':
            send_root = true;
            tmpfds = realloc(fds, (nfds + 3) * sizeof(fds[0]));
            if (tmpfds == NULL) {
                perror("realloc");
                return 2;
            }
            fds = tmpfds;
            for (int i = 0; i < 3; ++i)
                fds[nfds + i].from = fds[nfds + i].to = i;
            nfds += 3;
            break;
        case 'v':
            exec_vmo_file = optarg;
            break;
        case 'V':
            if (sscanf(optarg, "%u", &exec_vmo_fd) != 1)
                usage(argv[0], true);
            break;
        default:
            usage(argv[0], true);
        }
    }

    if (optind >= argc)
        usage(argv[0], true);

    mx_handle_t vmo;
    if (program_fd != -1) {
        vmo = launchpad_vmo_from_fd(program_fd);
        if (vmo == ERR_IO) {
            perror("launchpad_vmo_from_fd");
            return 2;
        }
        check("launchpad_vmo_from_fd", vmo);
    } else {
        if (program == NULL)
            program = argv[optind];
        vmo = launchpad_vmo_from_file(program);
        if (vmo == ERR_IO) {
            perror(program);
            return 2;
        }
        check("launchpad_vmo_from_file", vmo);
    }

    launchpad_t* lp;
    mx_status_t status = launchpad_create(program, &lp);
    check("launchpad_create", status);

    status = launchpad_arguments(lp, argc - optind,
                                 (const char *const*) &argv[optind]);
    check("launchpad_arguments", status);

    status = launchpad_environ(lp, env);
    check("launchpad_environ", status);

    if (send_root) {
        status = launchpad_clone_mxio_root(lp);
        check("launchpad_clone_mxio_root", status);
    }

    for (size_t i = 0; i < nfds; ++i) {
        status = launchpad_clone_fd(lp, fds[i].from, fds[i].to);
        check("launchpad_clone_fd", status);
    }

    if (basic) {
        status = launchpad_elf_load_basic(lp, vmo);
        check("launchpad_elf_load_basic", status);
    } else {
        status = launchpad_elf_load(lp, vmo);
        check("launchpad_elf_load", status);
    }

    if (send_loader_message) {
        bool already_sending = launchpad_send_loader_message(lp, true);
        if (!already_sending) {
            mx_handle_t loader_svc = mxio_loader_service(NULL, NULL);
            check("mxio_loader_service", loader_svc);
            mx_handle_t old = launchpad_use_loader_service(lp, loader_svc);
            check("launchpad_use_loader_service", old);
            if (old != MX_HANDLE_INVALID) {
                fprintf(stderr, "launchpad_use_loader_service returned %#x\n",
                        old);
                return 2;
            }
        }
    }

    if (pass_loader_handle) {
        mx_handle_t loader_svc = mxio_loader_service(NULL, NULL);
        check("mxio_loader_service", loader_svc);
        status = launchpad_add_handle(lp, loader_svc, MX_HND_TYPE_LOADER_SVC);
        check("launchpad_add_handle", status);
    }

    // Note that if both -v and -V were passed, we'll add two separate
    // MX_HND_TYPE_EXEC_VMO handles to the startup message, which is
    // unlikely to be useful.  But this program is mainly to test the
    // library, so it makes all the library calls the user asks for.
    if (exec_vmo_file != NULL) {
        mx_handle_t exec_vmo = launchpad_vmo_from_file(exec_vmo_file);
        if (exec_vmo == ERR_IO) {
            perror(exec_vmo_file);
            return 2;
        }
        check("launchpad_vmo_from_file", exec_vmo);
        status = launchpad_add_handle(lp, exec_vmo, MX_HND_TYPE_EXEC_VMO);
    }

    if (exec_vmo_fd != -1) {
        mx_handle_t exec_vmo = launchpad_vmo_from_fd(exec_vmo_fd);
        if (exec_vmo == ERR_IO) {
            perror("launchpad_vmo_from_fd");
            return 2;
        }
        check("launchpad_vmo_from_fd", exec_vmo);
        status = launchpad_add_handle(lp, exec_vmo, MX_HND_TYPE_EXEC_VMO);
    }

    // This doesn't get ownership of the process handle.
    // We're just testing the invariant that it returns a valid handle.
    mx_handle_t proc = launchpad_get_process_handle(lp);
    check("launchpad_get_process_handle", proc);

    // This gives us ownership of the process handle.
    proc = launchpad_start(lp);
    check("launchpad_start", proc);

    // The launchpad is done.  Clean it up.
    launchpad_destroy(lp);

    mx_signals_state_t state;
    status = mx_handle_wait_one(proc, MX_SIGNAL_SIGNALED,
                                MX_TIME_INFINITE, &state);
    check("mx_handle_wait_one", status);

    mx_process_info_t info;
    mx_ssize_t n = mx_handle_get_info(proc, MX_INFO_PROCESS,
                                      &info, sizeof(info));
    check("mx_handle_get_info", n);
    if (n != (mx_ssize_t)sizeof(info)) {
        fprintf(stderr, "mx_handle_get_info short read: %zu != %zu\n",
                (size_t)n, sizeof(info));
        exit(2);
    }

    printf("Process finished with return code %d\n", info.return_code);
    return info.return_code;
}
