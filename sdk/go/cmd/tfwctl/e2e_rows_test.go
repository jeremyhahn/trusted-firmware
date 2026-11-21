// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package main

import (
	"flag"
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/internal/peer"
)

// The rows `make hw-test` runs on an AVR board (#7). Every TestFirmware row
// names what its peer's image must have (firmwareRowNeeds) and every AVR board
// the image its default build flashes (boardImages); the run is the rows that
// image serves, by name, as test/integration/cortex-m/qemu_wire_test.sh names
// each QEMU boot's. So a board runs exactly the rows its image has, its refusal
// rows among them, and a row its image cannot serve is never started rather
// than failed or passed. A row in no list fails the run: a row nobody runs is
// a skip by another name. The QEMU boards are not here: their script names the
// rows of each image it boots, and passes them as HW_TEST_RUN.

// boardRowsEnv asks for the selection: mk/go.mk sets it when HW_TEST_RUN is
// not given.
const boardRowsEnv = "TFW_HW_ROWS"

// imageParts is what an image has that CAPS has no bit for.
type imageParts uint8

const (
	partStager       imageParts = 1 << iota // the OTA stager (OTA_STATUS)
	partOTACtl                              // OTA_INFO and OTA_CONFIRM: BOOT_AB, where tfw-boot runs the trial
	partApp                                 // an application (APP_INFO)
	partIdentity                            // a provisioned identity, the DEVICEID the rows verify under
	partTCP                                 // served over TCP: the connect resets nothing, so no BOOT event comes
	partTonePinHeld                         // ETH=1 holds TFW_TONE_PIN: the Mega's pin 10 is the shield's chip select
	partEthAllowList                        // over TCP it admits the Ethernet allow-list only (src/proto/link_eth.h): any other command is UNKNOWN_CMD
)

// rowTier is the crypto tier a row starts its peer on: startPeer checks it on
// a board as on the simulator.
type rowTier uint8

// noneTier is an image's only: CRYPTO=none, no provider at all. cryptoTier
// is a row's only: either tier the row holds facts for, software or element,
// so an image on none does not run it (Codex P1 on #558).
const (
	anyTier rowTier = iota
	softTier
	elementTier
	noneTier
	cryptoTier
)

// rowNeed is what a row needs of its peer's image.
type rowNeed struct {
	caps  uint32     // CAPS feature bits the image reports
	lacks uint32     // CAPS feature bits it does not: a refusal row's family
	tier  rowTier    // the tier the row starts its peer on
	has   imageParts // what it has that CAPS cannot say
	not   imageParts // and what it must not have
}

// boardImage is what an AVR board's default image has: its profile.mk and
// board_profile.h, through mk/firmware.mk's selection.
type boardImage struct {
	caps  uint32
	tier  rowTier
	parts imageParts
	// drop names rows the image serves that this board's run leaves out
	// until the PR named beside each fixes them there.
	drop map[string]string
	// attestTimeout is how long a row that may sign waits for ATTEST's
	// reply; zero keeps suiteTimeout. Set from a measured signing time,
	// with margin, on a board whose software signer is slow.
	attestTimeout time.Duration
	// tcpParts are the parts a run adds when its PORT is tcp: or tcp-listen:, a board
	// whose image serves its wire on the UART or on TCP.
	tcpParts imageParts
}

// suiteTimeout is the hw suite's command timeout (startFirmware).
const suiteTimeout = 5 * time.Second

// attestTimeoutFor is the ATTEST timeout of a row on board: the board's
// attestTimeout, or suiteTimeout on a board without one (tfwsim among them).
func attestTimeoutFor(board string) time.Duration {
	if d := boardImages[board].attestTimeout; d > 0 {
		return d
	}
	return suiteTimeout
}

// servedBy reports whether img serves a row with this need.
func (n rowNeed) servedBy(img boardImage) bool {
	return img.caps&n.caps == n.caps && img.caps&n.lacks == 0 &&
		(n.tier == anyTier || n.tier == img.tier ||
			n.tier == cryptoTier && (img.tier == softTier || img.tier == elementTier)) &&
		img.parts&n.has == n.has && img.parts&n.not == 0
}

// boardImageHolds reports whether the board's bench image has every part in
// p. A board with no row list has none: the rows that need a part skip there.
func boardImageHolds(board string, p imageParts) bool {
	img, ok := boardImages[board]
	return ok && img.parts&p == p
}

// tonePinHeld reports whether the firmware under test is known to hold the
// tone pin: hardware running a board image with partTonePinHeld. The
// simulator and every other image leave it free, so a RESERVED_PIN there is
// a regression, not a skip.
func tonePinHeld(hardware bool, board string) bool {
	return hardware && boardImageHolds(board, partTonePinHeld)
}

// attestRefusal is what ATTEST answers on a device with no identity: a
// CRYPTO=none image has no provider to attest with, so UNSUPPORTED; a tier
// that could hold one and does not, NOT_PROVISIONED (Codex P1 on #567). It
// lives in this untagged file so the row-selection tests compile without
// tfwsim (Codex P1 on #567 at c75c203c).
func attestRefusal(tier tfw.CryptoTier) tfw.Status {
	if tier == tfw.CryptoNone {
		return tfw.StatusErrUnsupported
	}
	return tfw.StatusErrNotProvisioned
}

// avrPeripherals are the families config.h turns on and neither AVR board's
// profile turns off.
const avrPeripherals = tfw.FeatureGPIO | tfw.FeatureSched | tfw.FeatureAnalog | tfw.FeaturePWM |
	tfw.FeatureTone | tfw.FeaturePulse | tfw.FeatureEEPROM | tfw.FeatureI2C | tfw.FeatureSPI |
	tfw.FeatureOneWire | tfw.FeatureAttest

// boardImages is each AVR hw-test board's default image, provisioned (the
// rows verify under DEVICEID).
var boardImages = map[string]boardImage{
	// CRYPTO=atecc608a, the part's one tier. board_profile.h turns
	// AUTH_SESSION and ATTEST_CSR off; ETH=0, so no stager (OTA_STAGE follows
	// ETH).
	"atmega328p": {
		caps:  avrPeripherals | tfw.FeatureAliasCert,
		tier:  elementTier,
		parts: partIdentity,
	},
	// CRYPTO=auto on the bench Mega, which has the element, and ETH=1, which
	// stages; BOOT_AB=1 adds OTA_INFO and OTA_CONFIRM. The shield's chip
	// select is the tone pin, which board.c marks TFW_PIN_ETH, in use under
	// ETH=1: the tone row needs a shieldless image (ETH=0).
	"atmega2560": {
		caps:  avrPeripherals | tfw.FeatureAliasCert | tfw.FeatureAttestCSR | tfw.FeatureAuthSession,
		tier:  elementTier,
		parts: partIdentity | partStager | partOTACtl | partTonePinHeld,
	},
	// The ESP32-P4 at bench HIL-0 (boards/esp32p4-poe/profile.mk), CRYPTO=none:
	// CAPS features 0x7ff on the bench record
	// (docs/development/bench/esp32-p4.md), the AVR families exactly, tier
	// none, so no tier's rows, nor the rows that hold facts only for a tier
	// (cryptoTier); no identity provisioned and no stager or application
	// known to the rows. Its wire is TCP, which finds it running: the
	// BOOT-event row resets it through HW_RESET=cmd:, or is skipped (#583).
	// The bench HIL-0 image: CRYPTO=none with sdkconfig.hil0, so the none
	// tier and no identity (no UDS in eFuse); CAPS is the AVR peripheral set,
	// 0x7ff.
	"esp32p4-poe": {
		caps:  avrPeripherals,
		tier:  noneTier,
		parts: partTCP,
	},
	// The default image this checkout builds (boards/stm32h573i-dk/
	// board_profile.h, mk/firmware.mk): the #535 port's 13-pin table, CAPS
	// gpio sched analog pwm tone pulse eeprom attest, the soft tier, no
	// stager, no provisioned identity. I2C (off unless I2C=1), SPI and
	// 1-Wire are off, so their refusal rows run; the I2C=1 image's rows run
	// by name, HW_TEST_RUN='TestFirmwareI2C'.
	"stm32h573i-dk": {
		caps: tfw.FeatureGPIO | tfw.FeatureSched | tfw.FeatureAnalog | tfw.FeaturePWM |
			tfw.FeatureTone | tfw.FeaturePulse | tfw.FeatureEEPROM | tfw.FeatureAttest,
		tier: softTier,
		// The attest refusal needs tfw-boot with its UDS slot erased
		// (cm_image's boot-unprovisioned.bin): the bench boot, boot.bin,
		// carries a UDS, so attest signs, in about 26 s on the board, past
		// the suite's 5 s timeout. Verified by name on the unprovisioned
		// boot (#570, docs/boards/stm32h573i-dk.md); held from the bench
		// boot's list.
		drop: map[string]string{"TestFirmwareAttestRefusedUnprovisioned": "#570"},
		// Software P-256 at 64 MHz: attest answered with evidence in 27.7 s,
		// twice, on the bench boot (#570); 45 s leaves the margin.
		attestTimeout: 45 * time.Second,
		// The ETH_MAC=1 image's wire on port 21574 (hal_eth_wire.c): a
		// connect resets nothing and events stay on the UART, and a TCP
		// peer gets the Ethernet allow-list only.
		tcpParts: partTCP | partEthAllowList,
	},
}

// ethAllowListRows are the rows whose every command is on the Ethernet
// allow-list (src/proto/link_eth.h: CAPS, APP_INFO, ATTEST, ATTEST_CERT,
// ATTEST_CSR, OTA_*). An image with partEthAllowList runs these only: any
// other row would get UNKNOWN_CMD for its PING, GPIO, SYS or refusal
// command, a failure that is the allow-list's, not the row's.
var ethAllowListRows = map[string]bool{
	"TestFirmwareCaps": true,
	"TestFirmwareSoftwareTierStillCannotIssueACertificate":  true,
	"TestFirmwareAttestCSRReportsADeviceThatCannotIssueOne": true,
	"TestFirmwareAppIsAnUnknownCommandWithoutAnApplication": true,
	"TestFirmwareRefusesAttestCSRItsCapsLacks":              true,
	"TestFirmwareAttestRefusedUnprovisioned":                true,
}

// runImage is the board's image as a run on port sees it: a tcp: port adds
// the image's tcpParts.
func runImage(img boardImage, port string) boardImage {
	if peer.IsTCP(port) {
		img.parts |= img.tcpParts
	}
	return img
}

// skipReason names why the image's run leaves a row it otherwise serves
// out, or is "" when the row runs.
func skipReason(img boardImage, row string) string {
	if img.parts&partEthAllowList != 0 && !ethAllowListRows[row] {
		return "a command outside the Ethernet allow-list (src/proto/link_eth.h)"
	}
	return ""
}

// firmwareRowNeeds is every TestFirmware row and what it needs.
var firmwareRowNeeds = map[string]rowNeed{
	// e2e_firmware_test.go
	"TestFirmwarePing": {},
	"TestFirmwarePingDeliversTheBootEventWithNoBootWait": {}, // on TCP: the reset hook (#583) or reset port (#550), else skipped
	"TestFirmwarePingAfterAMalformedBurst":               {},
	"TestFirmwareCaps":                                   {},
	"TestFirmwareSys":                                    {}, // #550: counter deltas, so no boot needed
	"TestFirmwarePin":                                    {caps: tfw.FeatureGPIO},
	"TestFirmwareWrite":                                  {caps: tfw.FeatureGPIO},
	"TestFirmwareTimer":                                  {caps: tfw.FeatureGPIO | tfw.FeatureSched},
	"TestFirmwareEEPROM":                                 {caps: tfw.FeatureEEPROM},
	"TestFirmwareAttestRefusedUnprovisioned":             {caps: tfw.FeatureAttest, not: partIdentity},
	"TestFirmwareAnalog":                                 {caps: tfw.FeatureAnalog},
	"TestFirmwarePWM":                                    {caps: tfw.FeaturePWM},
	"TestFirmwareTone":                                   {caps: tfw.FeatureTone, not: partTonePinHeld},
	"TestFirmwareToneRefusesTheShieldsPin":               {caps: tfw.FeatureTone, has: partTonePinHeld},
	"TestFirmwareI2C":                                    {caps: tfw.FeatureI2C},
	"TestFirmwareI2CConfig":                              {caps: tfw.FeatureI2C},
	"TestFirmwareSPIConfig":                              {caps: tfw.FeatureSPI},
	"TestFirmwareOneWire":                                {caps: tfw.FeatureOneWire},
	"TestFirmwarePulseIn":                                {caps: tfw.FeaturePulse},

	// e2e_dice_test.go
	"TestFirmwareAttestCertIsDICEEvidenceBoundToTheCallersNonce":              {caps: tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCertWithoutAnApplicationCarriesTheFirmwareMeasurement": {caps: tfw.FeatureAliasCert, has: partIdentity, not: partApp},
	"TestFirmwareAttestCertSignatureVerifiesUnderTheDeviceIDKey":              {caps: tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCertSignatureRejectsTamperingAndOtherKeys":             {caps: tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCertDiffersWithTheNonce":                               {caps: tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestEvidenceFrame":                                         {caps: tfw.FeatureAttest, has: partIdentity},
	"TestFirmwareEvidenceFrameRejectsTamperingAndOtherKeys":                   {caps: tfw.FeatureAttest, has: partIdentity},
	"TestFirmwareEvidenceAndCertificateShareOneKey":                           {caps: tfw.FeatureAttest | tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareElementReportsItsRootOfTrust":                                {caps: tfw.FeatureAliasCert, tier: elementTier},
	"TestFirmwareBlankElementRefusesToAttest":                                 {caps: tfw.FeatureAliasCert, tier: elementTier, not: partIdentity},
	"TestFirmwareSoftwareTierStillCannotIssueACertificate":                    {lacks: tfw.FeatureAliasCert, tier: softTier},

	// e2e_attestcsr_test.go
	"TestFirmwareAttestCSRPrintsTheSignatureAsHex":               {caps: tfw.FeatureAttestCSR, has: partIdentity},
	"TestFirmwareAttestCSRSignatureVerifiesOverTheKnownDigest":   {caps: tfw.FeatureAttestCSR | tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCSRSignatureIsOverTheNonce":               {caps: tfw.FeatureAttestCSR | tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCSRRefusesBWithNoCertificateMintedForIt":  {caps: tfw.FeatureAttestCSR | tfw.FeatureAliasCert, has: partIdentity},
	"TestFirmwareAttestCSRReportsADeviceThatCannotIssueOne":      {lacks: tfw.FeatureAttestCSR, tier: softTier},
	"TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported": {}, // any board: meddler keeps the read deadline, openPeer waits for no BOOT on TCP (#48)
	"TestFirmwareCapsNamesAttestCSROnADeviceThatOffersIt":        {caps: tfw.FeatureAttestCSR, has: partIdentity},

	// e2e_auth_test.go: the operator session, and the key TFW_HW_OPERATOR_KEY names
	"TestFirmwareAuthSessionIsAdvertisedByTheElementTier":        {caps: tfw.FeatureAuthSession},
	"TestFirmwareAnOperatorSignatureOpensTheSession":             {caps: tfw.FeatureAuthSession},
	"TestFirmwareASignatureFromAnotherKeyDoesNotOpenTheSession":  {caps: tfw.FeatureAuthSession},
	"TestFirmwareASignatureBoundToAnotherDevicesSerialIsRefused": {caps: tfw.FeatureAuthSession},
	"TestFirmwareEachAuthTakesAFreshChallenge":                   {caps: tfw.FeatureAuthSession},

	// e2e_ota_test.go
	"TestFirmwareOTAAuthLetsTheCommandPastTheGate": {tier: softTier, has: partStager},
	"TestFirmwareOTAInfo":                          {tier: elementTier, has: partStager | partOTACtl},
	"TestFirmwareOTAActivate":                      {caps: tfw.FeatureAuthSession, has: partStager},
	"TestFirmwareOTAConfirm":                       {caps: tfw.FeatureAuthSession, has: partStager | partOTACtl},

	// e2e_app_test.go
	"TestFirmwareAppReportsItsApplication":                  {tier: softTier, has: partApp},
	"TestFirmwareAppIsAnUnknownCommandWithoutAnApplication": {tier: softTier, not: partApp},

	// e2e_refusal_test.go: the refusal rows, on an image without the family
	"TestFirmwareRefusesTheAuthSessionItsCapsLacks":               {lacks: tfw.FeatureAuthSession},
	"TestFirmwareRefusesAttestCSRItsCapsLacks":                    {lacks: tfw.FeatureAttestCSR},
	"TestFirmwareSoftTierOTAActivateWithoutOTAAuthIsAuthRequired": {lacks: tfw.FeatureAuthSession, tier: softTier, has: partStager},

	// e2e_refusal_board_test.go (hwtest): families every simulator has
	"TestFirmwareRefusesAnalogItsCapsLacks":  {lacks: tfw.FeatureAnalog},
	"TestFirmwareRefusesPWMItsCapsLacks":     {lacks: tfw.FeaturePWM},
	"TestFirmwareRefusesPulseItsCapsLacks":   {lacks: tfw.FeaturePulse},
	"TestFirmwareRefusesEEPROMItsCapsLacks":  {lacks: tfw.FeatureEEPROM},
	"TestFirmwareRefusesI2CItsCapsLacks":     {lacks: tfw.FeatureI2C},
	"TestFirmwareRefusesSPIItsCapsLacks":     {lacks: tfw.FeatureSPI},
	"TestFirmwareRefusesOneWireItsCapsLacks": {lacks: tfw.FeatureOneWire},
}

// firmwareRows is every TestFirmware row this package's source defines, read
// from the source so that a row another build tag compiles is named too.
func firmwareRows(dir string) ([]string, error) {
	files, err := filepath.Glob(filepath.Join(dir, "*_test.go"))
	if err != nil {
		return nil, err
	}
	fset := token.NewFileSet()
	var rows []string
	for _, f := range files {
		file, err := parser.ParseFile(fset, f, nil, parser.SkipObjectResolution)
		if err != nil {
			return nil, err
		}
		for _, d := range file.Decls {
			fn, ok := d.(*ast.FuncDecl)
			if ok && fn.Recv == nil && strings.HasPrefix(fn.Name.Name, "TestFirmware") {
				rows = append(rows, fn.Name.Name)
			}
		}
	}
	sort.Strings(rows)
	return rows, nil
}

// boardRunPattern is the -run pattern of board's rows among rows, with the
// Ethernet rows when eth. It fails on a board with no image here and on a
// row firmwareRowNeeds does not name.
func boardRunPattern(board string, eth bool, rows []string) (string, error) {
	return boardRunPatternOn(board, "", eth, rows)
}

// boardRunPatternOn is boardRunPattern for a run on port: a tcp: port runs
// the rows the board's image serves over TCP (runImage, skipReason).
func boardRunPatternOn(board, port string, eth bool, rows []string) (string, error) {
	img, ok := boardImages[board]
	if !ok {
		return "", fmt.Errorf("hw-test: board %q has no row list (boardImages, cmd/tfwctl/e2e_rows_test.go): name its rows with HW_TEST_RUN", board)
	}
	img = runImage(img, port)
	var run, unlisted []string
	for _, r := range rows {
		need, ok := firmwareRowNeeds[r]
		switch {
		case !ok:
			unlisted = append(unlisted, r)
		case img.drop[r] != "":
		case skipReason(img, r) != "":
		case need.servedBy(img):
			run = append(run, r)
		}
	}
	if len(unlisted) > 0 {
		return "", fmt.Errorf("hw-test: %s in no row list: name each in firmwareRowNeeds (cmd/tfwctl/e2e_rows_test.go)",
			strings.Join(unlisted, ", "))
	}
	pattern := "^(" + strings.Join(run, "|") + ")$"
	if eth {
		pattern += "|^TestEthernet"
	}
	return pattern, nil
}

// selectBoardRows sets -test.run to the board's rows when hw-test asks for
// the selection (boardRowsEnv).
func selectBoardRows() error {
	if os.Getenv(boardRowsEnv) == "" {
		return nil
	}
	flag.Parse()
	rows, err := firmwareRows(".")
	if err != nil {
		return err
	}
	pattern, err := boardRunPatternOn(os.Getenv(peer.BoardEnv), os.Getenv(peer.PortEnv), os.Getenv(peer.EthEnv) != "", rows)
	if err != nil {
		return err
	}
	return flag.Set("test.run", pattern)
}

// Every TestFirmware row is in the table, and the table names no row that is
// gone: a row added without a need would fail every board's run.
func TestEveryFirmwareRowIsInARowList(t *testing.T) {
	rows, err := firmwareRows(".")
	if err != nil {
		t.Fatal(err)
	}
	defined := map[string]bool{}
	for _, r := range rows {
		defined[r] = true
		if _, ok := firmwareRowNeeds[r]; !ok {
			t.Errorf("%s is in no row list (firmwareRowNeeds)", r)
		}
	}
	for r := range firmwareRowNeeds {
		if !defined[r] {
			t.Errorf("firmwareRowNeeds names %s, which no file defines", r)
		}
	}
}

// boardRun is the board's -run pattern over every row, compiled.
func boardRun(t *testing.T, board string, eth bool) *regexp.Regexp {
	t.Helper()
	rows, err := firmwareRows(".")
	if err != nil {
		t.Fatal(err)
	}
	pattern, err := boardRunPattern(board, eth, rows)
	if err != nil {
		t.Fatal(err)
	}
	return regexp.MustCompile(pattern)
}

// Each AVR board's run, row by row for the rows its image decides: its
// refusal rows are there, and the rows of what it lacks are not.
func TestEachAVRBoardRunsTheRowsItsImageServes(t *testing.T) {
	for _, tc := range []struct {
		board    string
		run, not []string
	}{
		{"atmega328p", []string{
			"TestFirmwarePing", "TestFirmwareEEPROM", "TestFirmwareOneWire",
			"TestFirmwarePulseIn", "TestFirmwareTone",
			"TestFirmwareAttestCertIsDICEEvidenceBoundToTheCallersNonce",
			"TestFirmwareElementReportsItsRootOfTrust",
			"TestFirmwareRefusesTheAuthSessionItsCapsLacks", "TestFirmwareRefusesAttestCSRItsCapsLacks",
			"TestFirmwareTone",
		}, []string{
			"TestFirmwareToneRefusesTheShieldsPin",
			"TestFirmwareAnOperatorSignatureOpensTheSession", "TestFirmwareAttestCSRPrintsTheSignatureAsHex",
			"TestFirmwareOTAInfo", "TestFirmwareOTAActivate", "TestFirmwareOTAAuthLetsTheCommandPastTheGate",
			"TestFirmwareAttestRefusedUnprovisioned", "TestFirmwareBlankElementRefusesToAttest",
			"TestFirmwareSoftwareTierStillCannotIssueACertificate",
			"TestFirmwareSoftTierOTAActivateWithoutOTAAuthIsAuthRequired",
			"TestFirmwareRefusesAnalogItsCapsLacks", "TestFirmwareRefusesOneWireItsCapsLacks",
		}},
		{"atmega2560", []string{
			"TestFirmwarePing", "TestFirmwareAnOperatorSignatureOpensTheSession",
			"TestFirmwareAttestCSRPrintsTheSignatureAsHex",
			"TestFirmwareOTAInfo", "TestFirmwareOTAActivate", "TestFirmwareOTAConfirm",
			"TestFirmwareToneRefusesTheShieldsPin",
		}, []string{
			"TestFirmwareTone",
			"TestFirmwareRefusesTheAuthSessionItsCapsLacks", "TestFirmwareRefusesAttestCSRItsCapsLacks",
			"TestFirmwareOTAAuthLetsTheCommandPastTheGate", "TestFirmwareAppReportsItsApplication",
			"TestFirmwareAttestCSRReportsADeviceThatCannotIssueOne",
			"TestFirmwareRefusesSPIItsCapsLacks", "TestEthernetStagesAnImage",
		}},
		{"stm32h573i-dk", []string{
			"TestFirmwarePing", "TestFirmwarePin", "TestFirmwareWrite", "TestFirmwareTimer",
			"TestFirmwareEEPROM", "TestFirmwareAnalog", "TestFirmwarePWM",
			"TestFirmwarePulseIn", "TestFirmwareTone",
			"TestFirmwareRefusesI2CItsCapsLacks", "TestFirmwareRefusesSPIItsCapsLacks",
			"TestFirmwareRefusesOneWireItsCapsLacks",
		}, []string{
			"TestFirmwareRefusesAnalogItsCapsLacks", "TestFirmwareRefusesPWMItsCapsLacks",
			"TestFirmwareRefusesPulseItsCapsLacks", "TestFirmwareRefusesEEPROMItsCapsLacks",
			"TestFirmwareI2C", "TestFirmwareSPIConfig", "TestFirmwareOneWire",
			"TestFirmwareOTAInfo", "TestFirmwareElementReportsItsRootOfTrust",
			"TestFirmwareAttestRefusedUnprovisioned",
		}},
		// The HIL-0 image has no crypto tier, and its wire is TCP: connecting
		// resets nothing, so the reset rows reset it through its hook or
		// reset port, or check counter deltas (#550, #583).
		{"esp32p4-poe", []string{
			"TestFirmwarePing", "TestFirmwarePin", "TestFirmwareTone", "TestFirmwareI2C",
			"TestFirmwareCaps", "TestFirmwareI2CConfig", "TestFirmwareAttestRefusedUnprovisioned",
			"TestFirmwareEEPROM", "TestFirmwareSys", "TestFirmwarePingAfterAMalformedBurst",
			"TestFirmwarePingDeliversTheBootEventWithNoBootWait",
			"TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported",
		}, []string{
			"TestFirmwareToneRefusesTheShieldsPin",
		}},
	} {
		run := boardRun(t, tc.board, false)
		for _, r := range tc.run {
			if !run.MatchString(r) {
				t.Errorf("%s: %s is not run", tc.board, r)
			}
		}
		for _, r := range tc.not {
			if run.MatchString(r) {
				t.Errorf("%s: %s is run", tc.board, r)
			}
		}
	}
}

// A held row is one its board's image serves, named in firmwareRowNeeds: a
// hold on a row the list would not run anyway, or on a renamed row, holds
// nothing.
func TestEachHeldRowIsOneItsImageServes(t *testing.T) {
	for board, img := range boardImages {
		for r, issue := range img.drop {
			need, ok := firmwareRowNeeds[r]
			switch {
			case !ok:
				t.Errorf("%s holds %s, in no row list", board, r)
			case !need.servedBy(img):
				t.Errorf("%s holds %s, which its image does not serve", board, r)
			}
			if !regexp.MustCompile(`^#\d+$`).MatchString(issue) {
				t.Errorf("%s holds %s for %q, not an issue", board, r, issue)
			}
		}
	}
}

// The ESP32-P4's bench image is CRYPTO=none (Codex P1 on #558, #559): the
// rows with no tier in them are in its run, Caps, I2CConfig and EEPROM among
// them now that they hold none-tier facts (#576), and the AVR boards, on a
// tier, still run all three.
func TestTheESP32P4RunLeavesOutTheRowsWithNoNoneTierFacts(t *testing.T) {
	// The unprovisioned attest row stays: it holds the none tier's refusal.
	if got := attestRefusal(tfw.CryptoNone); got != tfw.StatusErrUnsupported {
		t.Errorf("attest refusal on the none tier = %v, want UNSUPPORTED", got)
	}
	if got := attestRefusal(tfw.CryptoSoftware); got != tfw.StatusErrNotProvisioned {
		t.Errorf("attest refusal on the software tier = %v, want NOT_PROVISIONED", got)
	}
	p4 := boardRun(t, "esp32p4-poe", false)
	for _, r := range []string{"TestFirmwarePing", "TestFirmwareWrite", "TestFirmwarePin", "TestFirmwareI2C",
		"TestFirmwareCaps", "TestFirmwareI2CConfig", "TestFirmwareEEPROM",
		"TestFirmwareAttestRefusedUnprovisioned"} {
		if !p4.MatchString(r) {
			t.Errorf("esp32p4-poe: %s is not run", r)
		}
	}
	for _, board := range []string{"atmega328p", "atmega2560"} {
		run := boardRun(t, board, false)
		for _, r := range []string{"TestFirmwareCaps", "TestFirmwareEEPROM", "TestFirmwareI2CConfig"} {
			if !run.MatchString(r) {
				t.Errorf("%s: %s is not run", board, r)
			}
		}
	}
}

// sysCountersStartAtZero reports whether connecting to board's image resets
// it, so its framer counters start at zero; a board with no image record
// (the simulator) starts fresh with every row.
func sysCountersStartAtZero(board string) bool {
	img, ok := boardImages[board]
	return !ok || img.parts&partTCP == 0
}

// The P4's connect resets nothing (Codex P1 on #565): Sys there would compare
// its counters before and after clean traffic, not against zero, while a board
// whose connect resets it, and the simulator, still assert zero. The P4 run
// selects Sys and the malformed-burst row
// (TestEveryBoardRunsTheBootEventRow, #550).
func TestSysCountersAreZeroOnlyWhereTheConnectResets(t *testing.T) {
	if sysCountersStartAtZero("esp32p4-poe") {
		t.Error("esp32p4-poe: Sys asserts zero counters on an image the connect does not reset")
	}
	for _, board := range []string{"atmega328p", "atmega2560", ""} {
		if !sysCountersStartAtZero(board) {
			t.Errorf("%q: Sys does not assert zero counters after the connect's reset", board)
		}
	}
}

// A row whose name another row's begins with is not run by the prefix.
func TestBoardRunMatchesWholeNames(t *testing.T) {
	if boardRun(t, "atmega2560", false).MatchString("TestFirmwarePingSomethingElse") {
		t.Fatal("the run matched a row by its prefix")
	}
}

// The esp32p4-poe run leaves out the rows the stacked PRs fix there, and
// every dropped row is a listed row the image would otherwise serve.
func TestBoardRunDropsTheRowsAStackedPROwns(t *testing.T) {
	run := boardRun(t, "esp32p4-poe", false)
	for row, pr := range boardImages["esp32p4-poe"].drop {
		if _, ok := firmwareRowNeeds[row]; !ok {
			t.Errorf("dropped row %s (%s) is in no row list", row, pr)
		}
		if pr == "" || pr[0] != '#' {
			t.Errorf("dropped row %s names no PR: %q", row, pr)
		}
		if run.MatchString(row) {
			t.Errorf("esp32p4-poe run still runs %s, owned by %s", row, pr)
		}
	}
	for _, row := range []string{"TestFirmwarePin", "TestFirmwarePWM", "TestFirmwarePing"} {
		if !run.MatchString(row) {
			t.Errorf("esp32p4-poe run lost %s", row)
		}
	}
}

// The caps-bit refusal row runs on a TCP board as on a serial one: meddler
// passes SetReadDeadline through and openPeer waits for no BOOT where the
// connect resets nothing (TestMeddlerRefusesAttestCSRWithoutABoot, #48).
func TestBoardRunRunsTheMeddlerRowOnATCPBoard(t *testing.T) {
	const row = "TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported"
	if !boardRun(t, "esp32p4-poe", false).MatchString(row) {
		t.Fatalf("the esp32p4-poe run lost %s", row)
	}
	if !boardRun(t, "atmega2560", false).MatchString(row) {
		t.Fatalf("the atmega2560 run lost %s", row)
	}
}

// #583: the BOOT-event row decides for itself whether the board can be made
// to send BOOT (a serial board by DTR, a TCP board through its reset hook or
// reset port (#550), skipped with none), and the Sys row checks the framer
// counters' delta, not zero (#550, Codex P1 on #558), so every board's run
// lists both.
func TestEveryBoardRunsTheBootEventRow(t *testing.T) {
	for _, row := range []string{"TestFirmwarePingDeliversTheBootEventWithNoBootWait", "TestFirmwarePingAfterAMalformedBurst", "TestFirmwareSys"} {
		for _, board := range []string{"atmega328p", "atmega2560", "esp32p4-poe"} {
			if !boardRun(t, board, false).MatchString(row) {
				t.Errorf("%s: %s is not run", board, row)
			}
		}
	}
}

// make hw-test BOARD=stm32h573i-dk PORT=tcp-listen:<host-port>: the run's image
// gains partTCP and the allow-list, so it selects the allow-list rows the
// image serves and nothing else, not the BOOT-event or zero-counter rows a
// reconnect cannot satisfy, nor a row whose PING or GPIO command the TCP
// wire answers UNKNOWN_CMD. On the VCOM the same board keeps its full list.
func TestTheDKOverTCPSelectsOnlyItsAllowListRows(t *testing.T) {
	rows, err := firmwareRows(".")
	if err != nil {
		t.Fatal(err)
	}
	const board = "stm32h573i-dk"
	pattern, err := boardRunPatternOn(board, peer.TCPPrefix+"board:21574", false, rows)
	if err != nil {
		t.Fatal(err)
	}
	// The DK dials the host (#584): its hw-test listens, and that run
	// selects the same rows as a tcp: one.
	if lp, err := boardRunPatternOn(board, peer.TCPListenPrefix+"21574", false, rows); err != nil || lp != pattern {
		t.Errorf("a tcp-listen: run of %s selects %q (%v), want the tcp: run's %q", board, lp, err, pattern)
	}
	tcp := regexp.MustCompile(pattern)
	img := runImage(boardImages[board], peer.TCPPrefix+"board:21574")
	if img.parts&(partTCP|partEthAllowList) != partTCP|partEthAllowList {
		t.Fatalf("a tcp: run of %s has parts %b, want partTCP and partEthAllowList", board, img.parts)
	}
	var ran []string
	for _, r := range rows {
		if !tcp.MatchString(r) {
			continue
		}
		ran = append(ran, r)
		if !ethAllowListRows[r] {
			t.Errorf("%s over TCP selects %s, outside the allow-list", board, r)
		}
	}
	for _, want := range []string{"TestFirmwareCaps", "TestFirmwareAppIsAnUnknownCommandWithoutAnApplication"} {
		if !tcp.MatchString(want) {
			t.Errorf("%s over TCP lost %s", board, want)
		}
	}
	for _, not := range []string{"TestFirmwarePing", "TestFirmwarePingDeliversTheBootEventWithNoBootWait", "TestFirmwareSys", "TestFirmwarePin"} {
		if tcp.MatchString(not) {
			t.Errorf("%s over TCP selects %s", board, not)
		}
		if reason := skipReason(img, not); reason == "" && firmwareRowNeeds[not].servedBy(img) {
			t.Errorf("%s over TCP leaves %s out with no named reason", board, not)
		}
	}
	if len(ran) == 0 {
		t.Fatalf("%s over TCP selects no row", board)
	}
	if serial := boardRun(t, board, false); !serial.MatchString("TestFirmwarePing") || !serial.MatchString("TestFirmwarePin") {
		t.Errorf("%s on its VCOM lost Ping or Pin", board)
	}
	if got := runImage(boardImages["esp32p4-poe"], peer.TCPPrefix+"board:21574"); got.parts&partEthAllowList != 0 {
		t.Error("esp32p4-poe gained the DK's allow-list")
	}
}

func TestBoardRunAddsTheEthernetRowsWithAnAddress(t *testing.T) {
	if !boardRun(t, "atmega2560", true).MatchString("TestEthernetStagesAnImage") {
		t.Fatal("an Ethernet run left out the Ethernet rows")
	}
}

func TestBoardRunPatternFailsOnARowInNoList(t *testing.T) {
	_, err := boardRunPattern("atmega2560", false, []string{"TestFirmwarePing", "TestFirmwareNobodyRuns"})
	if err == nil || !strings.Contains(err.Error(), "TestFirmwareNobodyRuns") {
		t.Fatalf("err = %v, want the unlisted row named", err)
	}
}

func TestBoardRunPatternFailsOnABoardWithNoList(t *testing.T) {
	_, err := boardRunPattern("atmega32u4", false, []string{"TestFirmwarePing"})
	if err == nil || !strings.Contains(err.Error(), "HW_TEST_RUN") {
		t.Fatalf("err = %v, want the board refused and HW_TEST_RUN named", err)
	}
}

// The selection hw-test asks for refuses a board with no list before it sets
// anything.
func TestSelectBoardRowsRefusesABoardWithNoList(t *testing.T) {
	t.Setenv(boardRowsEnv, "1")
	t.Setenv(peer.BoardEnv, "atmega32u4")
	before := flag.Lookup("test.run").Value.String()
	if err := selectBoardRows(); err == nil {
		t.Fatal("no error for a board with no row list")
	}
	if got := flag.Lookup("test.run").Value.String(); got != before {
		t.Fatalf("-test.run = %q after a refusal, was %q", got, before)
	}
}

func TestSelectBoardRowsDoesNothingUnasked(t *testing.T) {
	t.Setenv(boardRowsEnv, "")
	t.Setenv(peer.BoardEnv, "atmega32u4")
	if err := selectBoardRows(); err != nil {
		t.Fatalf("selection ran unasked: %v", err)
	}
}

func TestAttestTimeoutIsPerBoard(t *testing.T) {
	for board, want := range map[string]time.Duration{
		"stm32h573i-dk": 45 * time.Second, // the measured 27.7 s, with margin (#570)
		"atmega2560":    suiteTimeout,
		"no-such-board": suiteTimeout,
		"":              suiteTimeout, // tfwsim
	} {
		if got := attestTimeoutFor(board); got != want {
			t.Errorf("attestTimeoutFor(%q) = %v, want %v", board, got, want)
		}
	}
	for board, img := range boardImages {
		if img.attestTimeout != 0 && img.attestTimeout < suiteTimeout {
			t.Errorf("%s: attestTimeout %v is below the suite's %v", board, img.attestTimeout, suiteTimeout)
		}
	}
}

// TestOnlyTheMegaImageHoldsTheTonePin: TestFirmwareToneRefusesTheShieldsPin
// runs on an image whose tone pin is the shield's chip select and on no other,
// so the 328P simulator, the P4 and a board with no list leave it free.
func TestOnlyTheMegaImageHoldsTheTonePin(t *testing.T) {
	for board, want := range map[string]bool{
		"atmega2560":  true,
		"atmega328p":  false,
		"esp32p4-poe": false,
		"no-such":     false,
	} {
		if got := boardImageHolds(board, partTonePinHeld); got != want {
			t.Errorf("%s: holds the tone pin = %v, want %v", board, got, want)
		}
	}
}

func TestTonePinHeldOnlyOnTheMegaHardware(t *testing.T) {
	for _, c := range []struct {
		hardware bool
		board    string
		want     bool
	}{
		{true, "atmega2560", true},
		{false, "atmega2560", false},
		{true, "esp32p4-poe", false},
		{true, "atmega328p", false},
		{false, "atmega328p", false},
	} {
		if got := tonePinHeld(c.hardware, c.board); got != c.want {
			t.Errorf("%s hardware=%v: tone pin held = %v, want %v", c.board, c.hardware, got, c.want)
		}
	}
}
