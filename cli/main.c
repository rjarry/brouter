// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2023 Robin Jarry

#include "exec.h"
#include "interact.h"
#include "log.h"

#include <br_api.h>
#include <br_cli.h>
#include <br_client.h>

#include <ecoli.h>

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/queue.h>
#include <unistd.h>

static void usage(const char *prog) {
	printf("Usage: %s [-h] [-e] [-x] [-s PATH] ...\n", prog);
	printf("       %s --complete CMD\n", prog);
}

static void help(void) {
	puts("");
	puts("  Boring router CLI.");
	puts("");
	puts("options:");
	puts("  -h, --help                 Show this help message and exit.");
	puts("  -s PATH, --socket PATH     Path the control plane API socket.");
	puts("  -e, --err-exit             Abort on first error.");
	puts("  -x, --trace-commands       Print executed commands.");
	puts("  -c CMD, --complete CMD     Complete command line.");
}

struct br_cli_opts {
	const char *sock_path;
	bool err_exit;
	bool trace_commands;
	const char *complete_cmdline;
};

struct br_cli_opts opts = {
	.sock_path = BR_DEFAULT_SOCK_PATH,
};

static int parse_args(int argc, char **argv) {
	int c;

#define FLAGS ":s:exhc:"
	static struct option long_options[] = {
		{"socket", required_argument, NULL, 's'},
		{"err-exit", no_argument, NULL, 'e'},
		{"trace-commands", no_argument, NULL, 'x'},
		{"complete-command", required_argument, NULL, 'c'},
		{"help", no_argument, NULL, 'h'},
		{0},
	};

	opterr = 0; // disable getopt error reporting

	while ((c = getopt_long(argc, argv, FLAGS, long_options, NULL)) != -1) {
		switch (c) {
		case 's':
			opts.sock_path = optarg;
			break;
		case 'e':
			opts.err_exit = true;
			break;
		case 'x':
			opts.trace_commands = true;
			break;
		case 'c':
			opts.complete_cmdline = optarg;
			break;
		case 'h':
			usage(argv[0]);
			help();
			return -1;
		case ':':
			usage(argv[0]);
			errorf("-%c requires a value", optopt);
			return -1;
		case '?':
			usage(argv[0]);
			errorf("-%c unknown option", optopt);
			return -1;
		default:
			usage(argv[0]);
			errorf("invalid arguments");
			return -1;
		}
	}

	if (opts.complete_cmdline != NULL &&
	    (optind != argc || opts.err_exit || opts.trace_commands))
	{
		usage(argv[0]);
		errorf("--complete cannot be combined with other arguments");
		return -1;
	}

	return optind;
}

int main(int argc, char **argv) {
	struct br_client *client = NULL;
	struct ec_node *cmdlist = NULL;
	exec_status_t status;
	int ret, c;

	ret = EXIT_FAILURE;
	tty_init();

	if ((c = parse_args(argc, argv)) < 0)
		goto end;

	argc -= c;
	argv += c;

	if (ec_init() < 0) {
		errorf("ec_init: %s", strerror(errno));
		goto end;
	}

	if ((cmdlist = init_commands()) == NULL)
		goto end;

	if ((client = br_connect(opts.sock_path)) == NULL) {
		errorf("br_connect: %s", strerror(errno));
		goto end;
	}

	if (argc > 0) {
		status = exec_args(client, cmdlist, argc, (const char *const *)argv);
		if (print_cmd_status(status) < 0)
			goto end;
	} else {
		if (opts.complete_cmdline != NULL) {
			errorf("shell completion is not implemented");
			goto end;
		} else if (is_tty(stdin) && is_tty(stdout) && is_tty(stderr)) {
			if (interact(client, cmdlist) < 0)
				goto end;
		} else {
			char buf[BUFSIZ];
			while (fgets(buf, sizeof(buf), stdin)) {
				if (opts.trace_commands)
					trace_cmd(buf);
				status = exec_line(client, cmdlist, buf);
				if (print_cmd_status(status) < 0 && opts.err_exit)
					goto end;
			}
		}
	}

	ret = EXIT_SUCCESS;

end:
	if (br_disconnect(client) < 0) {
		errorf("br_disconnect: %s", strerror(errno));
		ret = EXIT_FAILURE;
	}
	ec_node_free(cmdlist);
	return ret;
}