/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2016-2017 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cli.h"

static int
__count_nodes(struct cli_node *node,
	      uint32_t flags, args_t *args)
{
	if (flags & node->type)
		args->arg1.u32[0]++;

	return 0;
}

uint32_t
cli_dir_item_count(struct cli_node *node, uint32_t types)
{
	args_t args;

	if (!node || !is_directory(node))
		return 0;

	memset(&args, '\0', sizeof(args));

	cli_scan_directory(node, __count_nodes, types, &args);

	return args.arg1.u32[0];
}

uint32_t
cli_path_item_count(uint32_t types)
{
	uint32_t cnt = 0, i;

	/* Look in the current directory first for a command */
	for (i = 0; i < CLI_MAX_BINS; i++)
		cnt += cli_dir_item_count(this_cli->bins[i], types);

	return cnt;
}

uint32_t
cli_path_cmd_count(void)
{
	return cli_path_item_count(CLI_EXE_TYPE);
}

static uint32_t
node_list_with_type(uint32_t flags, void **ret)
{
	struct cli_node *n, **nodes, *bin;
	uint32_t cnt, i, k = 0;

	cnt = cli_path_item_count(flags);

	if (cnt) {
		nodes = calloc(cnt + 1, sizeof(struct cli_node *));
		if (!nodes)
			return 0;

		for (i = 0; i < CLI_MAX_BINS; i++) {
			bin = this_cli->bins[i];
			if (!bin)
				continue;

			TAILQ_FOREACH(n, &bin->items, next) {
				if (n->type & flags)
					nodes[k++] = n;
			}
		}
		*ret = nodes;
	}
	return k;
}

static uint32_t
dir_list_with_type(struct cli_node *dir, uint32_t flags, void **ret)
{
	struct cli_node *n, **nodes;
	uint32_t cnt, k = 0;

	cnt = cli_dir_item_count(dir, flags);

	if (cnt) {
		nodes = calloc(cnt + 1, sizeof(struct cli_node *));
		if (!nodes)
			return 0;

		TAILQ_FOREACH(n, &dir->items, next) {
			if (n->type & flags)
				nodes[k++] = n;
		}
		*ret = nodes;
	}
	return k;
}

uint32_t
cli_node_list_with_type(struct cli_node *node, uint32_t flags, void **ret)
{
	if (node)
		return dir_list_with_type(node, flags, ret);
	else
		return node_list_with_type(flags, ret);
}

void
cli_node_list_free(void *nodes)
{
	free(nodes);
}

int
cli_scan_directory(struct cli_node *dir,
		   cli_scan_t func, uint32_t flags, args_t *args)
{
	struct cli_node *node;
	int ret = 0;

	if (!func)
		return ret;

	if (!dir)
		dir = cli_root_node();

	TAILQ_FOREACH(node, &dir->items, next) {
		if (node->type & flags) {
			ret = func(node, flags, args);
			if (ret)
				break;
		}
	}
	return ret;
}

int
cli_scan_path(const char *path, cli_scan_t func, uint32_t flags, args_t *args)
{
	struct cli_node *node;

	if (cli_find_node(path, &node))
		if (cli_scan_directory(node, func, flags, args))
			return 1;
	return 0;
}

struct cli_node *
cli_search_dir(struct cli_node *dir, const char *name, uint32_t type)
{
    struct cli_node *node;

	if (!name || (*name == '\0'))
		return NULL;

	if (!dir)
		dir = get_cwd();
	else if (!is_directory(dir))
		return NULL;

	/* Process the .. and . directories */
	if (!strcmp(name, ".."))
		return (dir->parent) ? dir->parent : NULL;
	else if (!strcmp(name, "."))
		return dir;

	TAILQ_FOREACH(node, &dir->items, next) {
		if (is_match(node->name, name) && (node->type & type))
			return node;
	}

	return NULL;
}

int
cli_find_node(const char *path, struct cli_node **ret)
{
	struct cli_node *node, *dir;
	char *my_path = NULL;
	char *argv[CLI_MAX_ARGVS + 1];
	int n, i;

	if (!path || (*path == '\0'))
		return 0;

	if (path[0] == '/' && path[1] == '\0') {
		node = cli_root_node();
		goto _leave;
	}

	/* Skip the leading '/' */
	my_path = strdup((path[0] == '/') ? &path[1] : path);
	if (!my_path)
		return 0;

	n = rte_strtok(my_path, "/", argv, CLI_MAX_ARGVS);

	/* handle the special case of leading '/' */
	if (path[0] == '/')
		dir = this_cli->root.tqh_first;
	else
		dir = get_cwd();

	/* Follow the directory path if present */
	for (i = 0, node = NULL; i < n; i++) {
		node = cli_search_dir(dir, argv[i], CLI_ALL_TYPE);

		if (!node)
			break;

		/* follow the next directory */
		if (is_directory(node) && (i < n))
			dir = node;
		else
			break;
	}

	free(my_path);

_leave:
	if (ret)
		*ret = node;

	return (node) ? 1 : 0;
}

struct cli_node *
cli_last_node_in_path(const char *path)
{
    struct cli_node *node, *dir;
    char *my_path = NULL;
    char *argv[CLI_MAX_ARGVS+1];
    int n, i;

	if (!path || (*path == '\0'))
		return get_cwd();

	if (path[0] == '/' && path[1] == '\0')
		return cli_root_node();

	/* Skip the leading '/' */
	my_path = strdup((path[0] == '/') ? &path[1] : path);
	if (!my_path)
		return NULL;

	n = rte_strtok(my_path, "/", argv, CLI_MAX_ARGVS);

	/* handle the special case of leading '/' */
	if (path[0] == '/')
		dir = this_cli->root.tqh_first;
	else
		dir = get_cwd();

	/* Follow the directory path if present */
	for (i = 0, node = NULL; i < n; i++) {
		node = cli_search_dir(dir, argv[i], CLI_ALL_TYPE);

		if (!node)
			break;

		/* follow the next directory */
		if (is_directory(node) && (i < n))
			dir = node;
		else
			break;
	}

	free(my_path);

	return dir;
}

struct cli_node *
cli_find_cmd(const char *path)
{
    struct cli_node *cmd, *dir;
    int i;

	if (cli_find_node(path, &cmd))
		return cmd;

	for (i = 0; i < CLI_MAX_BINS; i++) {
		if ((dir = this_cli->bins[i]) == NULL)
			continue;

		cmd = cli_search_dir(dir, path, CLI_EXE_TYPE);
		if (cmd)
			return cmd;
	}
	return NULL;
}
