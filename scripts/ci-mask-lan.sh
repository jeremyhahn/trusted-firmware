#!/usr/bin/env bash
# scripts/ci-mask-lan.sh: mask the LAN addresses a pool runner's environment names, and every SECRETS_*_REGISTRY entry; CI_MASK_REQUIRE names the SECRETS_* variables that must be set.
# Host names are matched case-insensitively with one terminal dot ignored; the masks keep the entry's own spelling.
set -euo pipefail

private() {
	local h=${1,,}
	h=${h%.}
	case $h in
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
