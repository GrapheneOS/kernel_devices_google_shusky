#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

function exit_if_error {
  if [ $1 -ne 0 ]; then
    echo "ERROR: $2: retval=$1" >&2
    exit $1
  fi
}

if [ "${BUILD_AOSP_KERNEL}" = "1" -a "${BUILD_STAGING_KERNEL}" = "1" ]; then
  echo "BUILD_AOSP_KERNEL=1 is incompatible with BUILD_STAGING_KERNEL."
  exit_if_error 1 "Flags incompatible with BUILD_AOSP_KERNEL detected"
fi

parameters=
if [ "${BUILD_AOSP_KERNEL}" = "1" ]; then
  echo -e "\nBuilding with core-kernel generated from source tree aosp/\n"
  parameters="--config=no_download_gki"
elif [ "${BUILD_STAGING_KERNEL}" = "1" ]; then
  echo -e "\nBuilding with core-kernel generated from source tree aosp-staging/\n"
  parameters="--config=aosp_staging"
else
  TARGET=`echo ${0} | sed 's/build_\(.*\)\.sh/\1/'`
  GKI_BUILD_ID=`sed -n 's/.*gki_prebuilts=\([0-9]\+\).*/\1/p' private/devices/google/${TARGET}/device.bazelrc`
  echo -e "\nBuilding with GKI prebuilts from ab/$GKI_BUILD_ID - kernel_aarch64\n"
fi

exec tools/bazel run ${parameters} --config=stamp --config=shusky --config=fast //private/devices/google/shusky:zuma_shusky_dist "$@"
