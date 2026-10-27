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
	partStager   imageParts = 1 << iota // the OTA stager (OTA_STATUS)
	partOTACtl                          // OTA_INFO and OTA_CONFIRM: BOOT_AB, where tfw-boot runs the trial
	partApp                             // an application (APP_INFO)
	partIdentity                        // a provisioned identity, the DEVICEID the rows verify under
)

// rowTier is the crypto tier a row starts its peer on: startPeer checks it on
// a board as on the simulator.
type rowTier uint8

const (
	anyTier rowTier = iota
	softTier
	elementTier
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
}

// servedBy reports whether img serves a row with this need.
func (n rowNeed) servedBy(img boardImage) bool {
	return img.caps&n.caps == n.caps && img.caps&n.lacks == 0 &&
		(n.tier == anyTier || n.tier == img.tier) &&
		img.parts&n.has == n.has && img.parts&n.not == 0
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
	// stages; BOOT_AB=1 adds OTA_INFO and OTA_CONFIRM.
	"atmega2560": {
		caps:  avrPeripherals | tfw.FeatureAliasCert | tfw.FeatureAttestCSR | tfw.FeatureAuthSession,
		tier:  elementTier,
		parts: partIdentity | partStager | partOTACtl,
	},
}

// firmwareRowNeeds is every TestFirmware row and what it needs.
var firmwareRowNeeds = map[string]rowNeed{
	// e2e_firmware_test.go
	"TestFirmwarePing": {},
	"TestFirmwarePingDeliversTheBootEventWithNoBootWait": {},
	"TestFirmwarePingAfterAMalformedBurst":               {},
	"TestFirmwareCaps":                                   {},
	"TestFirmwareSys":                                    {},
	"TestFirmwarePin":                                    {caps: tfw.FeatureGPIO},
	"TestFirmwareWrite":                                  {caps: tfw.FeatureGPIO},
	"TestFirmwareTimer":                                  {caps: tfw.FeatureGPIO | tfw.FeatureSched},
	"TestFirmwareEEPROM":                                 {caps: tfw.FeatureEEPROM},
	"TestFirmwareAttestRefusedUnprovisioned":             {caps: tfw.FeatureAttest, not: partIdentity},
	"TestFirmwareAnalog":                                 {caps: tfw.FeatureAnalog},
	"TestFirmwarePWM":                                    {caps: tfw.FeaturePWM},
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
	"TestFirmwareElementWithoutTheCapsBitIsRefusedAsUnsupported": {},
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
		}, []string{
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
		}, []string{
			"TestFirmwareRefusesTheAuthSessionItsCapsLacks", "TestFirmwareRefusesAttestCSRItsCapsLacks",
			"TestFirmwareOTAAuthLetsTheCommandPastTheGate", "TestFirmwareAppReportsItsApplication",
			"TestFirmwareAttestCSRReportsADeviceThatCannotIssueOne",
			"TestFirmwareRefusesSPIItsCapsLacks", "TestEthernetStagesAnImage",
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

// A row whose name another row's begins with is not run by the prefix.
func TestBoardRunMatchesWholeNames(t *testing.T) {
	if boardRun(t, "atmega2560", false).MatchString("TestFirmwarePingSomethingElse") {
		t.Fatal("the run matched a row by its prefix")
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
