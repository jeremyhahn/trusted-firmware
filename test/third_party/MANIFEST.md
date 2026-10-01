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

## ed25519-donna 8757bd4

floodyberry/ed25519-donna, **public domain** -- declared per file in each
header's opening comment ("Public domain by Andrew M. <liquidsun@gmail.com>",
and Adam Langley for the 64-bit field arithmetic). There is no LICENSE file
upstream to vendor, which is why none is listed below; the declarations are in
the files themselves and are covered by their hashes.

Upstream has no tags and no releases, so the pin is a COMMIT, not a version.
A branch name would pin nothing -- `master` is whatever it is today, and the
hash of a tarball fetched from a moving ref is a hash of a moving target.

    commit          8757bd4cd209cb032853ece0ce413f122eef212c  (2015-03-16)
    tarball         https://github.com/floodyberry/ed25519-donna/archive/8757bd4cd209cb032853ece0ce413f122eef212c.tar.gz
    tarball sha256  affbf8078b963f449fdafbc49a1e98389c6abf65fc6d49b051e7cbcf60764d1e

Seventeen of the upstream files are vendored: the ones `ed25519.c` includes on a
32-bit portable target. The SSE2 and 64-bit variants are not, because they
cannot be reached from any build here and an unreachable file is a file nobody
reviews. `ed25519-donna-64bit-x86-32bit.h` is the exception that proves the
rule: its name says x86 and its contents are guarded by `CPU_X86`, but
`ed25519-donna.h` includes it on EVERY non-64-bit target, so it is a
compile-time dependency on AVR too. `ed25519-donna-batchverify.h` IS vendored although nothing calls batch
verification: `ed25519.c` includes it unconditionally, so it is a compile-time
dependency whether or not `--gc-sections` keeps any of it.

VENDORED UNMODIFIED. The one thing this build needs that upstream does not
provide -- a declaration of `ed25519_randombytes_unsafe` for the custom-random
configuration -- is supplied by `boot/verify/donna_shim.h` through `-include`,
so that not one byte of the tree below differs from the pinned commit. Patching
a vendored file would make the hashes below pin our edit rather than upstream's
code, which is the opposite of what recording them is for.

WHAT IT COSTS ON THIS PART is measured in boot/boot_verify.h and reproduced by
`make bootloader-budget`: 72 930 bytes of flash and a 2 950-byte stack frame,
against a 4 096-byte boot section and 2 048 bytes of SRAM.

Vendored files, sha256 of each as committed:

    f92c32aa69d5a015a81b96bfd75063061447bcc60b79d7fa48f4056262addd39  curve25519-donna-32bit.h
    a3cd2e774f9f57cfe16ac7b010085590464c602b9eb64e704d48b25860a7e9a1  curve25519-donna-helpers.h
    491efa60521d6ef3cc8b42c722adbb07c5a8da3e355bd9ed8947a5ca02cec594  ed25519.c
    4487a16e12cd57252df91364ad820cc5eb93d91cbc602fc21d96c676134754bc  ed25519-donna-32bit-tables.h
    0bfcde5ac42fe07700b9c04b280d932c888d91a73597130201ff11dcfe629e3d  ed25519-donna-basepoint-table.h
    faa56400228b031de42b230e3d12192963bf863583f93f0ede5b93b843920e73  ed25519-donna-64bit-x86-32bit.h
    81c518f1591a3e90f952af752495bfb8e8b29082f63e56a441961957db04645d  ed25519-donna-batchverify.h
    b53f69909a80a6930879aa093a41831aa6f3f6e443187f159eacc96cb7e6294e  ed25519-donna.h
    243eee2c096612090bf724ca530590664605df1ca11744da09fe830a160c159a  ed25519-donna-impl-base.h
    5143e41565c60fb9bb60fbd18dd00c6bdb4304f567ee61cd0553b2dd1fd38371  ed25519-donna-portable.h
    bc3f37b7106dfac8594367b598747664d65e1a3ed5125d70c525649785070a6d  ed25519-donna-portable-identify.h
    98c711b3d04fcaaa9ff560efab6b0f2e969326208a7a854d56462162b1734bfb  ed25519.h
    4403f88d9a30aee0dc3b30eeba35fc6a513b7eba1efd1e4ed1811e8cf2794831  ed25519-hash-custom.h
    d1ee2991539feb3bd1bade01dfabb2a9d6a4c86e9f9deda7da1df45c831f732e  ed25519-hash.h
    db6ebec8ea68ec2d4aa1978cbea25b01379283a5a4f1f296cfd1e4a0eb3ea666  ed25519-randombytes-custom.h
    0716dd5e8a68e4241b432f14852cc69db2be07915163daffd01b564f2af553a7  ed25519-randombytes.h
    56716eb2f1eb1c5c5de494110b4ef912392ea7217a0e05608e3c70c236460ee1  modm-donna-32bit.h

## micro-ecc v1.1

kmackay/micro-ecc, BSD-2-Clause (`LICENSE.micro-ecc.txt`). TEST AND HOST ONLY:
it lives under `test/third_party/` and is linked by nothing but the host
software model of the ATECC608A (`test/model/atecc608a.c`) and the unit suites
and simulator that use it. No firmware image, bootloader or AVR build contains
it, and `make firmware-matrix` sizes are unaffected. The model needs real P-256
so that the evidence the element-tier simulator produces verifies; hand-rolled
curve arithmetic was refused.

The pin is the `v1.1` release tag, a COMMIT, so the tag cannot be moved under
us. Upstream's later master commits (ARM asm constraints, an HCCA side-channel
mitigation) are not taken: neither touches the generic C path this build links.

    tag             v1.1
    commit          24c60e243580c7868f4334a1ba3123481fe1aa48
    tarball         https://github.com/kmackay/micro-ecc/archive/24c60e243580c7868f4334a1ba3123481fe1aa48.tar.gz
    tarball sha256  e3f5830c8a1c25d5db4d514b703616cc9384ebff7169b3503080ef97213b3d9c

VENDORED UNMODIFIED, with one rename: upstream's `LICENSE.txt` is committed as
`LICENSE.micro-ecc.txt`, because `make verify-vendor` finds files by name and
Unity already owns `LICENSE.txt`. The bytes are upstream's. The curve selection
(`-DuECC_SUPPORTS_secp160r1=0` and its siblings, leaving secp256r1) is made on
the compiler command line in `mk/host.mk`, not by editing `uECC.h`.

Eleven source files are vendored, the ones `uECC.c` includes. The `asm_*.inc`
files are compile-time dependencies even though only the generic C path is
selected on x86-64: `uECC.c` names them under `uECC_PLATFORM` guards.

    fe8f2c228203e927f7a8bc18ca49676090b2ead042cd1842ab9a07b303b30ede  uECC.c
    9d480aa6920155f68503064bebefb815a82aab318ab305efdcf04674d2a9cba7  uECC.h
    a8ff7957855e6520674cad4bcaf8f3e752d40f6c3f7501915c3504c777be1e7a  uECC_vli.h
    61bd255c0dca4c69dfc779cc5c1fe1cbff47039d4bb7f37578a88f63b7fca466  types.h
    9e9d8d6771731dce2e084a4469d0dd2e9ac6245c8773766198a83f82d24b6313  curve-specific.inc
    e0f036628b47fb7180bfaeb0903a41edbbcda8e1ef99098456eb3b1cdcb4531e  platform-specific.inc
    bd37042c9366ad2308c0b42bd997aa36fa04e5dd20a5f6997e807d021dbcd2f8  asm_arm.inc
    0e88a8691784dcde6ad283dd8c4e0ca99799d9826e3924f5d7a084c39c8b07f3  asm_arm_mult_square.inc
    da318a86761206601a7210eca9987803bf7f3bc24ac0549c2bb28f91a6e3b7b2  asm_arm_mult_square_umaal.inc
    bc29567dfe556f669c64fbb8ec1b352a0dfcb2ca00a1a5378ae2a8b7fb2b4749  asm_avr.inc
    5f6a0775622d011b88fa717d4b7d8e6f09798e749728460850afdbf681702d94  asm_avr_mult_square.inc
    ffd8b033d2df7568c25a98866bd92b1656f7da82d7f813c0b2eb85ec36611193  LICENSE.micro-ecc.txt
