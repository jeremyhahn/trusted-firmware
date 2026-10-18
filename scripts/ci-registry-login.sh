#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Point a CI job's image builds at the forge's container registry (Company
# Agent m644): a CI step, after the forge credentials are masked.
#
#   usage: scripts/ci-registry-login.sh
#
# The registry is the forge's own, from GITHUB_SERVER_URL, never a literal
# host: <host>/<owner>/trusted-firmware-<image>. This writes IMAGE_REGISTRY
# and IMAGE_PULL=1 to GITHUB_ENV, so mk/docker.mk pulls and caches from it.
# When the runner's forge credentials (GIT_CONFIG_KEY_0's URL,
# dependency-resolution.md §6) are for that host, it logs in with them into a
# job-private DOCKER_CONFIG under RUNNER_TEMP and writes IMAGE_PUSH=1. No
# credential, and no output of `docker login`, is printed (ci-standard.md §5);
# the job's tear-down removes the DOCKER_CONFIG.
#
# On the github.com mirror there is no forge registry, so the images stay on
# the daemon. A refused login leaves pull without push: it never fails the job.
set -euo pipefail

server=${GITHUB_SERVER_URL:?}
case $server in
  https://github.com) echo "registry: none on the github.com mirror, the daemon only"; exit 0 ;;
esac
host=${server#*://}
host=${host%%/*}
owner=${GITHUB_REPOSITORY_OWNER:?}
echo "IMAGE_REGISTRY=${host,,}/${owner,,}/" >> "${GITHUB_ENV:?}"
echo "IMAGE_PULL=1" >> "$GITHUB_ENV"

key=${GIT_CONFIG_KEY_0:-}
case $key in
  *://*:*@"$host"/*) ;;
  *) echo "registry $host: no forge credentials for it in the job, pull only, no push"; exit 0 ;;
esac
cred=${key#*://}
cred=${cred%%@*}

config="${RUNNER_TEMP:?}/docker-config"
( umask 077; mkdir -p "$config" )
if printf '%s' "${cred#*:}" | DOCKER_CONFIG="$config" docker login "$host" -u "${cred%%:*}" --password-stdin >/dev/null 2>&1; then
  echo "DOCKER_CONFIG=$config" >> "$GITHUB_ENV"
  echo "IMAGE_PUSH=1" >> "$GITHUB_ENV"
  echo "registry $host: logged in with the forge credentials, pull and push"
else
  rm -rf "$config"
  echo "warn  registry $host: the forge credentials' login was refused, pull only, no push"
fi
