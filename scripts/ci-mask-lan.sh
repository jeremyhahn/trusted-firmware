#!/usr/bin/env bash
# scripts/ci-mask-lan.sh: mask the LAN addresses a pool runner's environment
# names (Tenant Agent ruling 887: internal addresses stay out of public
# repositories, and their CI logs count). The pool sets the DEVCACHE_*
# package caches, GOPROXY's LAN module proxy and the LAN CI registry; on a
# forge, the release registry (RELEASE_REGISTRY) and the forge's own host
# (GITHUB_SERVER_URL), which the image publishing and resolving name. A
# BuildKit build echoes its build arguments and `go env` prints GOPROXY.
# Run before any step that builds: for each entry of those variables whose
# host is internal, it prints ::add-mask:: for the entry, its host:port and
# its host, so the runner masks the address in every later line. A host is
# internal when it is a private IPv4 address, or (ci-template v1.53, Tenant
# Agent on trusted-firmware #821) a host name: a single label with no dot
# (a LAN host name) or a name under .internal, .lan, .local, .localdomain,
# .home.arpa or .corp. Public hosts and keywords (proxy.golang.org, direct,
# off) are left.
# v1.53 (Tenant Agent on go-quicbaas #2667): every variable whose name ends
# in _REGISTRY is scanned (CI_REGISTRY, CI_IMAGE_REGISTRY, RELEASE_REGISTRY,
# a copy's IT_REGISTRY). A _REGISTRY_OWNER or other non-host name is not
# scanned.
# #611 (handbook public-hygiene.md, internal values are configuration
# inputs): a registry a later step needs comes from an Actions secret, never
# a repository variable, which run logs print. The mask step sets
# SECRETS_<NAME> from secrets.<NAME>; every SECRETS_*_REGISTRY entry is masked
# whatever its host. CI_MASK_REQUIRE names (space-separated) the SECRETS_*
# variables the job needs: one that is empty or unset fails the step with an
# ::error naming the secret.
#
#   usage: scripts/ci-mask-lan.sh
set -euo pipefail

private() {
	case $1 in
	10.* | 192.168.* | 172.1[6-9].* | 172.2[0-9].* | 172.3[01].*) return 0 ;;
	'' | direct | off) return 1 ;;
	*.internal | *.lan | *.local | *.localdomain | *.home.arpa | *.corp) return 0 ;;
	*.*) return 1 ;;
	esac
	return 0
}

missing=0
for v in ${CI_MASK_REQUIRE:-}; do
	[ -n "${!v:-}" ] && continue
	echo "::error::secret ${v#SECRETS_} is not set; this job needs it (Settings > Secrets and variables > Actions > Secrets)"
	missing=1
done
[ "$missing" = 0 ] || exit 1

for v in $(compgen -e); do
	case $v in
	DEVCACHE_* | GOPROXY | *_REGISTRY | GITHUB_SERVER_URL) ;;
	*) continue ;;
	esac
	IFS='|, ' read -ra entries <<<"${!v}"
	for entry in "${entries[@]}"; do
		hostport=${entry#*://}
		hostport=${hostport%%/*}
		host=${hostport%%:*}
		case $v in
		SECRETS_*) [ -n "$host" ] || continue ;;
		*) private "$host" || continue ;;
		esac
		echo "::add-mask::$entry"
		[ "$hostport" = "$entry" ] || echo "::add-mask::$hostport"
		[ "$host" = "$hostport" ] || echo "::add-mask::$host"
	done
done
