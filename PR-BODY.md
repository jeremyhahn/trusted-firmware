## ci: every job runs on ubuntu-latest (owner ruling 999)

Owner ruling 999: CI runs on GitHub-hosted standard runners (ubuntu-latest) for everything possible; self-hosted only for hardware-specific jobs or a measured hosted limit (disk, memory, 6 h); larger hosted runners stay out.

`.github/workflows/ci.yml` routed same-repo PR and main runs of ten jobs to self-hosted `bigdisk-docker` and used ubuntu-latest only for forks. Every one of them now says `runs-on: ubuntu-latest` for every event.

| Job | Before (same-repo PR / main) | Now | Last green main run 44623338077 |
|---|---|---|---|
| image | self-hosted bigdisk-docker | ubuntu-latest | 20 min |
| unit (shards + race) | self-hosted bigdisk-docker | ubuntu-latest | 19-26 min / 6 min |
| security | self-hosted bigdisk-docker | ubuntu-latest | 1 min |
| esp32-build | self-hosted bigdisk-docker | ubuntu-latest | 1 min |
| esp32-qemu-test | self-hosted bigdisk-docker | ubuntu-latest | 40 min |
| optee-stack | self-hosted bigdisk-docker | ubuntu-latest | 0 min warm (cold timeout 180) |
| optee | self-hosted bigdisk-docker | ubuntu-latest | 21 min |
| lint-gates | self-hosted bigdisk-docker | ubuntu-latest | 0 min |
| integration | self-hosted bigdisk-docker | ubuntu-latest | 16 min |
| soak | self-hosted bigdisk-docker | ubuntu-latest | skipped |
| preflight, signatures, CI Gate and the rest | ubuntu-latest | ubuntu-latest (unchanged) | |

**Jobs kept self-hosted: none.** No job touches hardware, and every leg is far under the 6-hour limit. Disk is the only hosted limit in reach (about 14 GB free on `/`), so each of the ten jobs gets a first step, `Free hosted runner disk`, that removes the Android SDK, .NET, GHC, CodeQL and the preinstalled Docker images, instead of keeping the job self-hosted. The OP-TEE host-cache steps already fall back to the Actions cache when `CI_HOST_CACHE` is unset. Timeouts are unchanged. `code-review.yml` and `scans.yml` are untouched.

Checks run in the lane: `actionlint .github/workflows/ci.yml`, `scripts/test_schedule_guard.py` and `scripts/test_unit_shards.py` pass.

Follow-up to watch: the first hosted run of `optee-stack` on a cold cache (180 min timeout) and the peak disk in `image`. If either runs out of disk or memory, record that measurement beside its runs-on line before moving it back.
