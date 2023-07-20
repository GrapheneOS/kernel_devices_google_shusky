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
GKI_BUILD_ID=`sed -n 's/.*gki_prebuilts=\([0-9]\+\).*/\1/p' private/google-modules/soc/gs/device.bazelrc`
USE_GKI=`grep "build --config=download_gki" private/google-modules/soc/gs/device.bazelrc`
NO_USE_GKI=`grep "build --config=no_download_gki" private/google-modules/soc/gs/device.bazelrc`
USE_AOSP_OR_AOSP_STAGING=`grep "build --config=use_source_tree_aosp" private/google-modules/soc/gs/device.bazelrc`
if [ "${BUILD_AOSP_KERNEL}" = "1" ]; then
  echo -e "\nBuilding with core-kernel generated from source tree aosp/\n"
  parameters="--config=use_source_tree_aosp --config=no_download_gki"
elif [ "${BUILD_STAGING_KERNEL}" = "1" ]; then
  echo -e "\nBuilding with core-kernel generated from source tree aosp-staging/\n"
  parameters="--config=use_source_tree_aosp_staging --config=no_download_gki"
elif [ -n "${GKI_BUILD_ID}" ] && [ -n "${USE_GKI}" ] && [ -z "${NO_USE_GKI}" ] && [ -z "${USE_AOSP_OR_AOSP_STAGING}" ]; then
  echo -e "\nBuilding with GKI prebuilts from ab/$GKI_BUILD_ID - kernel_aarch64\n"
else
  echo -e "\nPlease check private/google-modules/soc/gs/device.bazelrc"
  echo -e "   1) IF \"build --config=use_source_tree_aosp\"           ----> core-kernel generated from source tree aosp/"
  echo -e "   2) IF \"build --config=use_source_tree_aosp_staging\"   ----> core-kernel generated from source tree aosp-staging/"
  echo -e "   3) IF \"build --config=download_gki\"                   ----> core-kernel based on GKI prebuilts\n"
fi

exec tools/bazel run ${parameters} --config=stamp --config=shusky --config=fast //private/devices/google/shusky:zuma_shusky_dist "$@"
