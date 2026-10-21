// Copyright (c) 2026 Jeremy Hahn
// Copyright (c) 2026 Automate The Things, LLC
//
// This file is part of trusted-firmware.

package avr

// What the element simulator answered one device-attest-01 enrolment with,
// recorded once and frozen.
//
// The simulator is the real firmware compiled for the host
// (tfwsim-atecc608a, the element tier, the only one that mints DICE
// evidence). These frames were captured from its wire by
// testdata/record/record_test.go, run as
//
//	make tfwsim-atecc608a
//	cd xkms && TFWSIM_DIR=$PWD/../build/host go test -count=1 -v ./avr/testdata/record
//
// and pasted verbatim. They are LITERALS, not values computed by the code
// they pin: a vector the adapter produced would move whenever the adapter
// moved. The element signs with a fresh k, so a re-recording prints other
// signatures over the same TBS bytes; one replaces these whole, with the
// command above beside it, and never to make a failing test pass -- a
// failure here is the firmware's evidence and go-xkms's verifier disagreeing.
//
// The enrolment the frames answer:
//
//   - recKeyAuth is the key authorization the challenge stands for, and
//     DICENonce(recKeyAuth) the nonce the device was asked to attest over.
//     recAnotherKeyAuth is a second challenge's.
//   - recAttestCertPages are the ATTEST_CERT answers for that nonce, page by
//     page: page index, the certificate's total length (LE16), then up to
//     sixty bytes of the alias certificate's DER.
//   - recAttestCSRHex is the ATTEST_CSR answer for the same nonce: r||s, the
//     device's signature over the possession request it built.
//   - recAnother* are the same two answers for recAnotherKeyAuth's nonce.
//   - recCapsHex is the CAPS answer: protocol 2.0, firmware 0.1.0, the
//     atmega328p board, the secure-element tier.
//   - recUeidHex is the ueid the evidence carries, type byte 0x01 included:
//     the simulator's fixed test serial, not any device's.
//   - recDeviceIDSPKIHex is the DeviceID public key the provisioning record
//     holds: the simulator's fixed test key, the subject key of every alias
//     certificate above.
const (
	recKeyAuth        = "recorded-token.recorded-thumbprint"
	recAnotherKeyAuth = "another-token.another-thumbprint"

	recCapsHex = "02000001000116084002ffe700000500"

	recUeidHex = "0174667773696d2d7465737469642d3031"

	recDeviceIDSPKIHex = "" +
		"3059301306072a8648ce3d020106082a8648ce3d0301070342000441035035fa" +
		"f782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35acaeda3e5021f" +
		"f3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78a"

	recAttestCSRHex = "" +
		"6db3378a179ab978f346c98c730544c46e1a1c001d89adf675d819f55cefb481" +
		"ef349e7e9022304fa4132448246b88c2c6cb65cee44e876b0dd282e5163a8f7a"

	recAnotherAttestCSRHex = "" +
		"c8364b74c7f812e506d3b6af2790133fdd2806ae9b122b455e2f46e7582163c4" +
		"1e795b8685d6c9febfc240892e054abd82eae433c2f89d520eb27d86de50a341"
)

var recAttestCertPages = []string{
	"00da01308201d63082017ba00302010202087dbfb1d106fe27ba300a06082a8648ce3d0403023017311530130603550403130c746677204465766963654944",
	"01da013020170d3230303130313030303030305a180f39393939313233313233353935395a3014311230100603550403130974667720416c69617330593013",
	"02da0106072a8648ce3d020106082a8648ce3d0301070342000441035035faf782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35acaeda3e5021f",
	"03da01f3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78aa381b13081ae3081ab06066781050504060101ff04819d30819a048197a30a5820",
	"04da013dbfb1d106fe27baa5572aa9617cdc508c3fac6045cc35c3978e2d0fa005f313190100510174667773696d2d7465737469642d303119011181821901",
	"05da01025854a50050bcc710071a6653c1967ef0d3d5ede421016374667702a2181f6374667718210103a111a20782015820afd2dc68dd233f7db5dd373619",
	"06da017cdec7c45423f757c6303db0b5d6a5858989871818636170700c00300a06082a8648ce3d0403020349003046022100a2dd1f5b0bd0aef1ee49ff00ba",
	"07da019943d6c84e8a21c4bfd31c589fbddd0cd2a9e7022100e0cbeba75d24c40e8f4d98f28f758a40bb24ab382ac2d75af421a9282d0f376c",
}

var recAnotherAttestCertPages = []string{
	"00d901308201d53082017ba00302010202084fde152cdb9ec46d300a06082a8648ce3d0403023017311530130603550403130c746677204465766963654944",
	"01d9013020170d3230303130313030303030305a180f39393939313233313233353935395a3014311230100603550403130974667720416c69617330593013",
	"02d90106072a8648ce3d020106082a8648ce3d0301070342000441035035faf782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35acaeda3e5021f",
	"03d901f3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78aa381b13081ae3081ab06066781050504060101ff04819d30819a048197a30a5820",
	"04d901cfde152cdb9ec46d40e71637847cce9cb94e1010033e49572abf32a42b26bf7a190100510174667773696d2d7465737469642d303119011181821901",
	"05d901025854a50050bcc710071a6653c1967ef0d3d5ede421016374667702a2181f6374667718210103a111a20782015820afd2dc68dd233f7db5dd373619",
	"06d9017cdec7c45423f757c6303db0b5d6a5858989871818636170700c00300a06082a8648ce3d0403020348003045022100937193295da1331fc579f1357b",
	"07d901056f5fbe4fcbc5f42a88188005626823a5f62a02204c0ecac2a2c00654c1edd9781730790387af1f597fb32647758375da798ca440",
}
