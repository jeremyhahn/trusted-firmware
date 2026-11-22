### Changed

- **A test pins scripts/reap-stale-ci-stacks.sh** (#640). `make scripts-test` runs it against a stub `docker`: only a prefixed compose project whose every container is stale is reaped, with its networks and volumes; a lone prefixed container goes only when stale; an integration image goes only under the image lock, when neither created nor tagged within REAP_IMAGE_MIN_AGE and with no container on its id; a dry run removes nothing and takes no lock; a bad argument exits 2.
