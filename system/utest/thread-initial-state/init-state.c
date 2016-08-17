// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <magenta/syscalls.h>
#include <unittest/unittest.h>
#include <stdio.h>

extern int thread_entry(void* arg);

int print_fail(void) {
    EXPECT_TRUE(false, "Failed");
    mx_thread_exit();
    return 1; // Not reached
}

bool tis_test(void) {
    BEGIN_TEST;
    void* arg = (void*)0x1234567890abcdef;
    mx_handle_t handle = mx_thread_create(thread_entry, arg, "", 0);
    ASSERT_GE(handle, 0, "Error while thread creation");
    mx_status_t status = mx_handle_wait_one(handle, MX_SIGNAL_SIGNALED,
                                                  MX_TIME_INFINITE, NULL);
    ASSERT_GE(status, 0, "Error while thread wait");
    END_TEST;
}

BEGIN_TEST_CASE(tis_tests)
RUN_TEST(tis_test)
END_TEST_CASE(tis_tests)

int main(int argc, char** argv) {
    return unittest_run_all_tests(argc, argv) ? 0 : -1;
}
