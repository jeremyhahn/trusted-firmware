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
	partStager      imageParts = 1 << iota // the OTA stager (OTA_STATUS)
	partOTACtl                             // OTA_INFO and OTA_CONFIRM: BOOT_AB, where tfw-boot runs the trial
	partApp                                // an application (APP_INFO)
	partIdentity                           // a provisioned identity, the DEVICEID the rows verify under
	partTCP                                // served over TCP: the connect resets nothing, so no BOOT event comes
	partTonePinHeld                        // ETH=1 holds TFW_TONE_PIN: the Mega's pin 10 is the shield's chip select
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
	// known to the rows.
	"esp32p4-poe": {
		caps:  avrPeripherals,
		tier:  noneTier,
		parts: partTCP,
	},
}

// firmwareRowNeeds is every TestFirmware row and what it needs.
var firmwareRowNeeds = map[string]rowNeed{
	// e2e_firmware_test.go
	"TestFirmwarePing": {},
	"TestFirmwarePingDeliversTheBootEventWithNoBootWait": {}, // #550: on TCP pulses TFW_HW_RESET_PORT, or skips by name
	"TestFirmwarePingAfterAMalformedBurst":               {},
	"TestFirmwareCaps":                                   {},
	"TestFirmwareSys":                                    {}, // #550: counter deltas, so no boot needed
	"TestFirmwarePin":                                    {caps: tfw.FeatureGPIO},
	"TestFirmwareWrite":                                  {caps: tfw.FeatureGPIO},
	"TestFirmwareTimer":                                  {caps: tfw.FeatureGPIO | tfw.FeatureSched},
	"TestFirmwareEEPROM":                                 {caps: tfw.FeatureEEPROM, tier: cryptoTier},
	"TestFirmwareAttestRefusedUnprovisioned":             {caps: tfw.FeatureAttest, not: partIdentity},
	"TestFirmwareAnalog":                                 {caps: tfw.FeatureAnalog},
	"TestFirmwarePWM":                                    {caps: tfw.FeaturePWM},
	"TestFirmwareTone":                                   {caps: tfw.FeatureTone, not: partTonePinHeld},
	"TestFirmwareToneRefusesTheShieldsPin":               {caps: tfw.FeatureTone, has: partTonePinHeld},
	"TestFirmwareI2C":                                    {caps: tfw.FeatureI2C},
	"TestFirmwareI2CConfig":                              {caps: tfw.FeatureI2C},
	"TestFirmwareSPIConfig":                              {caps: tfw.FeatureSPI},
	"TestFirmwareOneWireAndPulseIn":                      {caps: tfw.FeatureOneWire | tfw.FeaturePulse},

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
	"TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported": {not: partTCP}, // meddler waits for a BOOT
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
	img, ok := boardImages[board]
	if !ok {
		return "", fmt.Errorf("hw-test: board %q has no row list (boardImages, cmd/tfwctl/e2e_rows_test.go): name its rows with HW_TEST_RUN", board)
	}
	var run, unlisted []string
	for _, r := range rows {
		need, ok := firmwareRowNeeds[r]
		switch {
		case !ok:
			unlisted = append(unlisted, r)
		case img.drop[r] != "":
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
	pattern, err := boardRunPattern(os.Getenv(peer.BoardEnv), os.Getenv(peer.EthEnv) != "", rows)
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
			"TestFirmwarePing", "TestFirmwareEEPROM", "TestFirmwareOneWireAndPulseIn",
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
		// The HIL-0 image has no crypto tier, and its wire is TCP: connecting
		// resets nothing, so the reset rows pulse the reset port or check
		// counter deltas (#550).
		{"esp32p4-poe", []string{
			"TestFirmwarePing", "TestFirmwarePin", "TestFirmwareTone", "TestFirmwareI2C",
			"TestFirmwareCaps", "TestFirmwareI2CConfig", "TestFirmwareAttestRefusedUnprovisioned",
			"TestFirmwareSys", "TestFirmwarePingAfterAMalformedBurst",
			"TestFirmwarePingDeliversTheBootEventWithNoBootWait",
		}, []string{
			"TestFirmwareEEPROM",
			"TestFirmwareToneRefusesTheShieldsPin",
			"TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported",
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

// The ESP32-P4's bench image is CRYPTO=none (Codex P1 on #558): the rows that
// hold facts only for the software and element tiers (EEPROM) are not in its
// run, where they could only fail; the rows with no tier in them are, Caps and
// I2CConfig among them now that they hold none-tier facts (#576), and the AVR
// boards, on a tier, still run all three.
func TestTheESP32P4RunLeavesOutTheRowsWithNoNoneTierFacts(t *testing.T) {
	// The unprovisioned attest row stays: it holds the none tier's refusal.
	if got := attestRefusal(tfw.CryptoNone); got != tfw.StatusErrUnsupported {
		t.Errorf("attest refusal on the none tier = %v, want UNSUPPORTED", got)
	}
	if got := attestRefusal(tfw.CryptoSoftware); got != tfw.StatusErrNotProvisioned {
		t.Errorf("attest refusal on the software tier = %v, want NOT_PROVISIONED", got)
	}
	tiered := []string{"TestFirmwareEEPROM"}
	p4 := boardRun(t, "esp32p4-poe", false)
	for _, r := range []string{"TestFirmwarePing", "TestFirmwareWrite", "TestFirmwarePin", "TestFirmwareI2C",
		"TestFirmwareCaps", "TestFirmwareI2CConfig", "TestFirmwareAttestRefusedUnprovisioned"} {
		if !p4.MatchString(r) {
			t.Errorf("esp32p4-poe: %s is not run", r)
		}
	}
	for _, r := range tiered {
		if p4.MatchString(r) {
			t.Errorf("esp32p4-poe: %s is run on a CRYPTO=none image", r)
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
// (TestBoardRunSelectsTheRepairedResetRowsOnATCPBoard, #550).
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

// The caps-bit refusal row wraps its connection in meddler, which hides
// SetReadDeadline, and waits for a BOOT: a TCP connect resets nothing, so
// on the P4 the row would block until the test timeout (Codex, #567).
func TestBoardRunLeavesTheMeddlerRowOffATCPBoard(t *testing.T) {
	const row = "TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported"
	if boardRun(t, "esp32p4-poe", false).MatchString(row) {
		t.Fatalf("the esp32p4-poe run selects %s, which hangs on TCP", row)
	}
	if !boardRun(t, "atmega2560", false).MatchString(row) {
		t.Fatalf("the atmega2560 run lost %s", row)
	}
}

// A TCP connect resets nothing, so the boot-event row pulses
// TFW_HW_RESET_PORT (and skips by name with none) and the Sys row checks the
// framer counters' delta, not zero (#550, Codex P1 on #558): a TCP board
// runs both, as the serial boards do.
func TestBoardRunSelectsTheRepairedResetRowsOnATCPBoard(t *testing.T) {
	// #550: the boot-event row pulses the reset port (or skips by name) and
	// the Sys row checks counter deltas, so a TCP board runs both.
	for _, row := range []string{
		"TestFirmwarePingDeliversTheBootEventWithNoBootWait",
		"TestFirmwarePingAfterAMalformedBurst",
		"TestFirmwareSys",
	} {
		if !boardRun(t, "esp32p4-poe", false).MatchString(row) {
			t.Errorf("the esp32p4-poe run leaves out %s, which #550 repaired for a TCP link", row)
		}
		if !boardRun(t, "atmega2560", false).MatchString(row) {
			t.Errorf("the atmega2560 run lost %s", row)
		}
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
