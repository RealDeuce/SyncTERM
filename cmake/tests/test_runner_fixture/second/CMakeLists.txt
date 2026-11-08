# Copyright (c) 2026 Stephen Hurd; SPDX-License-Identifier: BSD-2-Clause
syncterm_add_test_case(runner arguments ARGS arguments.c)
target_compile_definitions(arguments PRIVATE CASE_VALUE=2)
set_target_properties(arguments PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
syncterm_add_test_case(runner skipped skipped.c)
set_target_properties(skipped PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
add_test(NAME skipped COMMAND runner skipped)
set_tests_properties(skipped PROPERTIES SKIP_RETURN_CODE 77)
syncterm_add_test_case(runner failed failed.c)
set_target_properties(failed PROPERTIES
	C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
add_test(NAME failed COMMAND runner failed)
set_tests_properties(failed PROPERTIES WILL_FAIL TRUE)
