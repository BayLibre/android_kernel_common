#/bin/bash
export PATH=$(bazel-out/k8-fastbuild/bin/build/kernel/hermetic-tools/readlink -m bazel-out/k8-fastbuild/bin/build/kernel/hermetic-tools):$PATH
# utility functions
source build/kernel/build_utils.sh
# source the build environment
source bazel-out/k8-fastbuild/bin/common/kernel_x86_64_env.sh
# Increase parallelism # TODO(b/192655643): do not use -j anymore
export MAKEFLAGS="${MAKEFLAGS} -j$(nproc)"
# re-setup the PATH to also include the hermetic tools, because env completely overwrites
# PATH with HERMETIC_TOOLCHAIN=1
export PATH=$(bazel-out/k8-fastbuild/bin/build/kernel/hermetic-tools/readlink -m bazel-out/k8-fastbuild/bin/build/kernel/hermetic-tools):$PATH
# setup LD_LIBRARY_PATH for prebuilts
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${ROOT_DIR}/prebuilts/kernel-build-tools/linux-x86/lib64
# Set up KCONFIG_EXT
if [ -n "${KCONFIG_EXT}" ]; then
    export KCONFIG_EXT_PREFIX=$(realpath $(dirname ${KCONFIG_EXT}) --relative-to ${ROOT_DIR}/${KERNEL_DIR})/
fi
if [ -n "${DTSTREE_MAKEFILE}" ]; then
    export dtstree=$(realpath $(dirname ${DTSTREE_MAKEFILE}) --relative-to ${ROOT_DIR}/${KERNEL_DIR})
fi
# Set up KCPPFLAGS
# For Kleaf local (non-sandbox) builds, $ROOT_DIR is under execroot but
# $ROOT_DIR/$KERNEL_DIR is a symlink to the real source tree under
# workspace root, making $abs_srctree not under $ROOT_DIR.
if [[ "$(realpath ${ROOT_DIR}/${KERNEL_DIR})" != "${ROOT_DIR}/${KERNEL_DIR}" ]]; then
    export KCPPFLAGS="$KCPPFLAGS -ffile-prefix-map=$(realpath ${ROOT_DIR}/${KERNEL_DIR})/="
fi

# Pre-defconfig commands
eval ${PRE_DEFCONFIG_CMDS}
# Actual defconfig
make -C ${KERNEL_DIR} ${TOOL_ARGS} O=${OUT_DIR} ${DEFCONFIG}
# Post-defconfig commands
eval ${POST_DEFCONFIG_CMDS}