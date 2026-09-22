# Vendored dependencies

Every third-party source in this tree is pinned by content hash and verified in
CI by `make verify-vendor`. This is the substitute for `go mod verify` and
`govulncheck`: a C project has no package manager, so the integrity guarantee
has to be written down and checked rather than inherited from a toolchain.

Adding an entry means adding its hash here in the same commit. An unlisted file
under `test/third_party/` or `src/third_party/` fails the check, because a
dependency nobody recorded is one nobody is watching.

## unity 2.6.0

ThrowTheSwitch Unity, MIT. Upstream tarball
`https://github.com/ThrowTheSwitch/Unity/archive/refs/tags/v2.6.0.tar.gz`

    tarball sha256  aa4c9fb1ae5fc5242f914c65f3557e817e40cb37f04a31e5ff76d1ab89dbf674

Vendored files, sha256 of each as committed:

    ee69607f977cf2653762cb3a84da8a122a0a5bbe6c3308ca1f59e443b1890cf5  unity.c
    e20bed56a8172986ff9f65933351b8b9df62996b77045ed8787d631223fe6eb3  unity.h
    c4220e36bc7c66f81a0f0b026e694865f3f33e6c8b48113dedd8db1d50ce0716  unity_internals.h
    865b12a0747d1e8973612a04ea978767aa8fa8aab1466c662807b54bfe517d2c  LICENSE.txt
