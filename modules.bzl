# SPDX-License-Identifier: GPL-2.0 OR Apache-2.0
# Copyright (C) 2025 The Android Open Source Project

"""Re-exports of symbols for external usage regarding to lists of modules.

DO NOT ADD MORE SYMBOLS. Additional symbols for external usage should be added
to bazel/modules.bzl, NOT modules.bzl or bazel/modules_private.bzl.
"""

load(
    ":bazel/modules.bzl",
    _get_gki_modules_list = "get_gki_modules_list",
    _get_kunit_modules_list = "get_kunit_modules_list",
)

visibility("public")

get_gki_modules_list = _get_gki_modules_list
get_kunit_modules_list = _get_kunit_modules_list
