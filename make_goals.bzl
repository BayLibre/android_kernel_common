# SPDX-License-Identifier: GPL-2.0
# Copyright (C) 2023 The Android Open Source Project

"""
GKI lists of make targets.
"""

GKI_AARCH64_MAKE_GOALS = [
    "Image",
    "Image.lz4",
    "Image.gz",
    "modules",
]

GKI_RISCV64_MAKE_GOALS = [
    "Image",
    "Image.lz4",
    "Image.gz",
    "modules",
]

GKI_X86_64_MAKE_GOALS = [
    "bzImage",
    "modules",
]
