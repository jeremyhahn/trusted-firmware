#!/usr/bin/env bash
# SPDX-License-Identifier: AGPL-3.0-or-later
# SPDX-License-Identifier: commercial
#
# Point a CI job's image builds at the forge's container registry: a CI step, after the forge credentials are masked.
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
