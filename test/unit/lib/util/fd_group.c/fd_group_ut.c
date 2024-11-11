/*   SPDX-License-Identifier: BSD-3-Clause
 *   Copyright (C) 2024 Samsung Electronics Co., Ltd.
 *   All rights reserved.
 */

#include "spdk/stdinc.h"
#include "spdk_internal/cunit.h"
#include "util/fd_group.c"

#include <sys/timerfd.h>

static bool g_fd_group_cb_fn_called = false;
static int g_expected_cb_arg = 0;

static int
fd_group_cb_fn(void *ctx)
{
	int *cb_arg = ctx;

	SPDK_CU_ASSERT_FATAL(*cb_arg == g_expected_cb_arg);
	SPDK_CU_ASSERT_FATAL(!g_fd_group_cb_fn_called);
	g_fd_group_cb_fn_called = true;
	return 0;
}

static void
test_fd_group_basic(void)
{
	struct spdk_fd_group *fgrp;
	struct event_handler *ehdlr = NULL;
	int fd1, fd2;
	int rc;
	int cb_arg1 = 1;
	int cb_arg2 = 2;
	uint64_t val = 1;
	struct spdk_event_handler_opts eh_opts;
	struct itimerspec ts;

	rc = spdk_fd_group_create(&fgrp);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	fd1 = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	SPDK_CU_ASSERT_FATAL(fd1 >= 0);

	eh_opts.opts_size = sizeof(eh_opts);
	eh_opts.events = EPOLLIN;
	eh_opts.fd_type = SPDK_FD_TYPE_EVENTFD;
	rc = SPDK_FD_GROUP_ADD_EXT(fgrp, fd1, fd_group_cb_fn, &cb_arg1, &eh_opts);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(fgrp->num_fds == 1);

	/* Verify that event handler 1 is initialized correctly */
	ehdlr = TAILQ_FIRST(&fgrp->event_handlers);
	SPDK_CU_ASSERT_FATAL(ehdlr != NULL);
	CU_ASSERT(ehdlr->fd == fd1);
	CU_ASSERT(ehdlr->state == EVENT_HANDLER_STATE_WAITING);
	CU_ASSERT(ehdlr->events == EPOLLIN);

	fd2 = timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | TFD_CLOEXEC);
	SPDK_CU_ASSERT_FATAL(fd2 >= 0);

	rc = SPDK_FD_GROUP_ADD(fgrp, fd2, fd_group_cb_fn, &cb_arg2);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(fgrp->num_fds == 2);

	/* Verify that event handler 2 is initialized correctly */
	ehdlr = TAILQ_FIRST(&fgrp->event_handlers);
	ehdlr = TAILQ_NEXT(ehdlr, next);
	SPDK_CU_ASSERT_FATAL(ehdlr != NULL);
	CU_ASSERT(ehdlr->fd == fd2);
	CU_ASSERT(ehdlr->state == EVENT_HANDLER_STATE_WAITING);
	CU_ASSERT(ehdlr->events == EPOLLIN);

	/* Verify that the event handler 1 is called when its fd is set */
	g_fd_group_cb_fn_called = false;
	g_expected_cb_arg = cb_arg1;
	rc = write(fd1, &val, sizeof(val));
	SPDK_CU_ASSERT_FATAL(rc == sizeof(val));

	rc = spdk_fd_group_wait(fgrp, 0);
	SPDK_CU_ASSERT_FATAL(rc == 1);
	SPDK_CU_ASSERT_FATAL(g_fd_group_cb_fn_called);

	/* Modify event type and see if event handler is updated correctly */
	rc = spdk_fd_group_event_modify(fgrp, fd1, EPOLLIN | EPOLLERR);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	ehdlr = TAILQ_FIRST(&fgrp->event_handlers);
	SPDK_CU_ASSERT_FATAL(ehdlr != NULL);
	CU_ASSERT(ehdlr->events == (EPOLLIN | EPOLLERR));

	/* Verify that the event handler 2 is not called after it is removed */
	g_fd_group_cb_fn_called = false;
	g_expected_cb_arg = cb_arg2;
	ts.it_interval.tv_sec = 0;
	ts.it_interval.tv_nsec = 100000000;
	ts.it_value.tv_sec = 0;
	ts.it_value.tv_nsec = 0;
	rc = timerfd_settime(fd2, 0, &ts, NULL);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	spdk_fd_group_remove(fgrp, fd2);
	SPDK_CU_ASSERT_FATAL(fgrp->num_fds == 1);

	/* Simulate pointing to free memory */
	cb_arg2 = 0xDEADBEEF;

	rc = spdk_fd_group_wait(fgrp, 0);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(!g_fd_group_cb_fn_called);

	rc = close(fd2);
	CU_ASSERT(rc == 0);

	spdk_fd_group_remove(fgrp, fd1);
	SPDK_CU_ASSERT_FATAL(fgrp->num_fds == 0);

	rc = close(fd1);
	CU_ASSERT(rc == 0);

	spdk_fd_group_destroy(fgrp);
}

static void
test_fd_group_nest_unnest(void)
{
	struct spdk_fd_group *parent, *child, *not_parent;
	int fd_parent, fd_child, fd_child_2;
	int rc;
	int cb_arg;

	rc = spdk_fd_group_create(&parent);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	rc = spdk_fd_group_create(&child);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	rc = spdk_fd_group_create(&not_parent);
	SPDK_CU_ASSERT_FATAL(rc == 0);

	fd_parent = epoll_create1(0);
	SPDK_CU_ASSERT_FATAL(fd_parent >= 0);

	fd_child = epoll_create1(0);
	SPDK_CU_ASSERT_FATAL(fd_child >= 0);

	fd_child_2 = epoll_create1(0);
	SPDK_CU_ASSERT_FATAL(fd_child_2 >= 0);

	rc = SPDK_FD_GROUP_ADD(parent, fd_parent, fd_group_cb_fn, &cb_arg);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(parent->num_fds == 1);

	rc = SPDK_FD_GROUP_ADD(child, fd_child, fd_group_cb_fn, &cb_arg);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 1);

	/* Nest child fd group to a parent fd group and verify their relation */
	rc = spdk_fd_group_nest(parent, child);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(child->parent == parent);
	SPDK_CU_ASSERT_FATAL(parent->num_fds == 2);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 0);

	/* Register second child fd to the child fd group and verify that the parent fd group
	 * has the correct number of fds.
	 */
	rc = SPDK_FD_GROUP_ADD(child, fd_child_2, fd_group_cb_fn, &cb_arg);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 0);
	SPDK_CU_ASSERT_FATAL(parent->num_fds == 3);

	/* Unnest child fd group from wrong parent fd group and verify that it fails. */
	rc = spdk_fd_group_unnest(not_parent, child);
	SPDK_CU_ASSERT_FATAL(rc == -EINVAL);

	/* Unnest child fd group from its parent fd group and verify it. */
	rc = spdk_fd_group_unnest(parent, child);
	SPDK_CU_ASSERT_FATAL(rc == 0);
	SPDK_CU_ASSERT_FATAL(child->parent == NULL);
	SPDK_CU_ASSERT_FATAL(parent->num_fds == 1);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 2);

	spdk_fd_group_remove(child, fd_child);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 1);

	spdk_fd_group_remove(child, fd_child_2);
	SPDK_CU_ASSERT_FATAL(child->num_fds == 0);

	spdk_fd_group_remove(parent, fd_parent);
	SPDK_CU_ASSERT_FATAL(parent->num_fds == 0);

	rc = close(fd_child);
	CU_ASSERT(rc == 0);

	rc = close(fd_child_2);
	CU_ASSERT(rc == 0);

	rc = close(fd_parent);
	CU_ASSERT(rc == 0);

	spdk_fd_group_destroy(child);
	spdk_fd_group_destroy(parent);
	spdk_fd_group_destroy(not_parent);
}

int
main(int argc, char **argv)
{
	CU_pSuite	suite = NULL;
	unsigned int	num_failures;

	CU_initialize_registry();

	suite = CU_add_suite("fd_group", NULL, NULL);

	CU_ADD_TEST(suite, test_fd_group_basic);
	CU_ADD_TEST(suite, test_fd_group_nest_unnest);

	num_failures = spdk_ut_run_tests(argc, argv, NULL);

	CU_cleanup_registry();

	return num_failures;
}
