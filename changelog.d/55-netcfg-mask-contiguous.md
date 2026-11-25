### Fixed

- **`NetConfig.MarshalBinary` refuses a static block whose netmask is not contiguous** (#55), as `tfwprov --bake-netblock` already did, so `tfwctl netcfg set` and `WriteNetConfig` no longer write a block such as mask 255.0.255.0; 0.0.0.0 and a DHCP block with no mask are still accepted.
