// Command wirecheck is one wire exchange with an ESP32 image over TCP, run by
// test/integration/esp32/qemu_test.py against QEMU's forwarded port, or with a
// Cortex-M image over QEMU's serial port on a Unix socket, run by
// test/integration/cortex-m/qemu_test.sh (-network unix).
//
// It is the Go SDK, unmodified, on a net.Conn: the framing on TCP is the
// framing on a UART (docs/multi-arch/adr-esp-idf-integration.md), so nothing
// here knows it is not a serial port beyond WithBootWait(0) -- a TCP accept
// does not reset the device, so there is no BOOT event to wait for.
//
// Exit status 0 means every check passed; each failure prints one line
// naming the check and exits 1. An OTA failure prints the replies before it
// first: the count sent, the first three, the last three and the failing one.
//
// -activate and -confirm sign their OTA_AUTH with -auth-key, ES256 over the
// Sig_structure docs/ota/api.md names. The key is the test's, a file the
// build generated or the test made; in a deployment go-xkms holds it and
// signs.
package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"flag"
	"fmt"
	"io"
	"math"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// markAddr is past the platform-reserved block, [0x000, 0x0C0) on every board
// (include/tfw/boot.h), which the wire refuses to write.
const markAddr = 0x100

// The evidence fields ATTEST signs (src/caps/attest.c): the signature covers
// SHA-256 of every byte before the algorithm id.
const (
	offNonce = 1
	offAlg   = 73
	offSig   = 74
	algES256 = 0x01 // TFW_ALG_ECDSA_P256_SHA256, include/tfw/protocol.h
)

func main() {
	addr := flag.String("addr", "127.0.0.1:21574", "the device's wire address")
	wait := flag.Duration("wait", 60*time.Second, "how long to keep dialling while the device boots")
	board := flag.Uint("board", 0x81, "the board id CAPS must report")
	tier := flag.Int("tier", -1, "the crypto tier CAPS must report, or -1 for any")
	mark := flag.String("mark", "", "write these bytes to EEPROM at markAddr")
	expect := flag.String("expect-mark", "", "require these bytes at EEPROM markAddr")
	reset := flag.Bool("reset", false, "end with a soft reset")
	deviceID := flag.String("deviceid", "", "the DeviceID public point x||y, hex, an ATTEST signature must verify under")
	genKey := flag.String("genkey", "", "a PROVISION=1 image: PROV_GENKEY once, write the point x||y as hex to this file; no ATTEST")
	ota := flag.String("ota", "", "stream this image file through OTA_BEGIN/DATA/END")
	otaFail := flag.Int("ota-fail", -1, "the status the -ota stream must fail with, or -1 for none")
	authKey := flag.String("auth-key", "", "the PEM P-256 key -activate and -confirm sign OTA_AUTH with")
	signed := flag.String("signed-image", "", "the sealed image whose trailer the authorizations name; -ota's if empty")
	activate := flag.Int("activate", -1, "OTA_ACTIVATE the staged image in this mode, OTA_AUTH first; -1 for none")
	confirm := flag.Bool("confirm", false, "OTA_CONFIRM the running trial, OTA_AUTH first")
	info := flag.Bool("info", false, "print OTA_INFO for both slots")
	timeout := flag.Duration("timeout", 10*time.Second, "how long one command may take")
	network := flag.String("network", "tcp", "tcp, or unix for a socket path in -addr")
	silent := flag.Duration("silent", 0, "pass only if nothing answers a PING for this long")
	flag.Parse()

	if *silent > 0 {
		if err := expectSilent(*network, *addr, *silent); err != nil {
			fail("silent", err)
		}
		fmt.Println("wirecheck: ok")
		return
	}

	var pub *ecdsa.PublicKey
	if *deviceID != "" {
		raw, err := hex.DecodeString(*deviceID)
		if err == nil {
			pub, err = ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, raw...))
		}
		if err != nil {
			fail("deviceid", fmt.Errorf("-deviceid is not a P-256 point x||y: %w", err))
		}
	}

	d, conn, err := dial(*network, *addr, *wait, *timeout)
	if err != nil {
		fail("dial", err)
	}
	defer func() { _ = conn.Close() }() // every check has printed or failed by here
	ctx := context.Background()

	caps, err := d.Caps(ctx)
	if err != nil {
		fail("caps", err)
	}
	if uint(caps.BoardID) != *board {
		fail("caps", fmt.Errorf("board id 0x%02x, want 0x%02x", uint8(caps.BoardID), *board))
	}
	if *tier >= 0 && int(caps.CryptoTier) != *tier {
		fail("caps", fmt.Errorf("crypto tier %d, want %d", uint8(caps.CryptoTier), *tier))
	}
	sys, err := d.SysInfo(ctx)
	if err != nil {
		fail("sysinfo", err)
	}
	fmt.Printf("wirecheck: caps proto %d.%d fw %d.%d.%d board 0x%02x pins %d tier %d features 0x%08x\n",
		caps.ProtoMajor, caps.ProtoMinor, caps.FirmwareMajor, caps.FirmwareMinor, caps.FirmwarePatch,
		uint8(caps.BoardID), caps.PinCount, uint8(caps.CryptoTier), caps.Features)
	fmt.Printf("wirecheck: sysinfo uptime %v free-ram %d reset-cause %d\n",
		sys.Uptime, sys.FreeRAM, uint8(sys.ResetCause))
	if *genKey != "" {
		provision(ctx, d, caps, *genKey)
	} else {
		checkAttest(ctx, d, caps, pub)
	}

	if *expect != "" {
		n := len(*expect)
		if n > math.MaxUint8 {
			fail("eeprom read", fmt.Errorf("-expect-mark is %d bytes; one read carries at most %d", n, math.MaxUint8))
			return // fail exits; the return is for a reader who cannot see that
		}
		got, err := d.EEPROMRead(ctx, markAddr, uint8(n))
		if err != nil {
			fail("eeprom read", err)
		}
		if !bytes.Equal(got, []byte(*expect)) {
			fail("eeprom read", fmt.Errorf("%q at 0x%03x, want %q", got, markAddr, *expect))
		}
		fmt.Printf("wirecheck: eeprom holds %q across the reboot\n", got)
	}
	if *mark != "" {
		if err := d.EEPROMWrite(ctx, markAddr, []byte(*mark)); err != nil {
			fail("eeprom write", err)
		}
	}
	if *ota != "" {
		otaCheck(ctx, d, *ota, *otaFail)
	}
	if *activate >= 0 || *confirm {
		file := *signed
		if file == "" {
			file = *ota
		}
		a := authorizer(*authKey, file)
		if *activate >= 0 {
			activateCheck(ctx, d, a, *activate)
		}
		if *confirm {
			confirmCheck(ctx, d, a)
		}
	}
	if *info {
		infoPrint(ctx, d)
	}
	if *reset {
		if err := d.Reset(ctx, tfw.ResetSoft); err != nil {
			fail("reset", err)
		}
		fmt.Println("wirecheck: soft reset accepted")
	}
	fmt.Println("wirecheck: ok")
}

// provision is the jig's round on a PROVISION=1 image with no UDS yet
// (docs/multi-arch/esp32-p4-design.md section 2.4): PROV_GENKEY returns a
// P-256 point, PROV_PUBKEY reads the same point back, and a second
// PROV_GENKEY is refused because the UDS is burned. ATTEST is not asked: the
// image's provider found no UDS at boot, and the next normal boot is where
// the point is held to it.
func provision(ctx context.Context, d *tfw.Device, caps *tfw.Caps, path string) {
	if !caps.Has(tfw.FeatureProvision) {
		fail("caps", fmt.Errorf("features 0x%08x: not a PROVISION=1 image", caps.Features))
	}
	point, err := d.ProvGenKey(ctx, 0)
	if err != nil {
		fail("prov genkey", err)
	}
	if _, err := ecdsa.ParseUncompressedPublicKey(elliptic.P256(), append([]byte{0x04}, point...)); err != nil {
		fail("prov genkey", fmt.Errorf("the reply is not a P-256 point x||y: %w", err))
	}
	back, err := d.ProvPubKey(ctx, 0)
	if err != nil {
		fail("prov pubkey", err)
	}
	if !bytes.Equal(back, point) {
		fail("prov pubkey", errors.New("PROV_PUBKEY does not read back the point PROV_GENKEY returned"))
	}
	if _, err := d.ProvGenKey(ctx, 0); !errors.Is(err, tfw.StatusErrProtectedRegion) {
		fail("prov genkey", fmt.Errorf("a second PROV_GENKEY answered %v, want %v", err, tfw.StatusErrProtectedRegion))
	}
	if err := os.WriteFile(path, []byte(hex.EncodeToString(point)), 0o600); err != nil {
		fail("prov genkey", err)
	}
	fp := sha256.Sum256(point)
	fmt.Printf("wirecheck: prov: PROV_GENKEY point sha256(x||y)=%x, PROV_PUBKEY agrees, a second PROV_GENKEY refused (%v)\n",
		fp[:16], tfw.StatusErrProtectedRegion)
}

// checkAttest asks for evidence and holds the answer to what the tier CAPS
// reports, so one binary checks both QEMU machines. The none tier has nothing
// to sign with and must say so: no P-256 in CAPS, ATTEST refused UNSUPPORTED.
// A tier that signs must report P-256 and ATTEST, and its evidence must carry
// the nonce and verify as ES256 under the DeviceID the caller derived on the
// host -- and not verify once the nonce is changed.
func checkAttest(ctx context.Context, d *tfw.Device, caps *tfw.Caps, pub *ecdsa.PublicKey) {
	nonce := make([]byte, tfw.AttestNonceLen)
	if _, err := rand.Read(nonce); err != nil {
		fail("attest", err)
	}
	if caps.CryptoTier == tfw.CryptoNone {
		if caps.Algorithms.Can(tfw.AlgECDSAP256SHA256) {
			fail("caps", fmt.Errorf("none tier reports %s", caps.Algorithms))
		}
		if _, err := d.Attest(ctx, nonce); !errors.Is(err, tfw.StatusErrUnsupported) {
			fail("attest", fmt.Errorf("none tier answered %v, want %v", err, tfw.StatusErrUnsupported))
		}
		fmt.Println("wirecheck: none tier: no P-256 in CAPS, ATTEST refused (unsupported)")
		return
	}
	if pub == nil {
		fail("attest", fmt.Errorf("tier %s signs ATTEST: -deviceid is required to verify it", caps.CryptoTier))
		return // fail exits; the return is for a reader who cannot see that
	}
	if !caps.Has(tfw.FeatureAttest) || !caps.Algorithms.Can(tfw.AlgECDSAP256SHA256) {
		fail("caps", fmt.Errorf("tier %s: features 0x%08x algorithms %s, want ATTEST and ecdsa-p256-sha256",
			caps.CryptoTier, caps.Features, caps.Algorithms))
	}
	ev, err := d.Attest(ctx, nonce)
	if err != nil {
		fail("attest", err)
	}
	if !bytes.Equal(ev[offNonce:offNonce+tfw.AttestNonceLen], nonce) {
		fail("attest", errors.New("the evidence does not carry the nonce sent"))
	}
	if ev[offAlg] != algES256 {
		fail("attest", fmt.Errorf("algorithm id 0x%02x, want 0x%02x (ES256)", ev[offAlg], algES256))
	}
	if !es256Verifies(pub, ev) {
		fail("attest", errors.New("the evidence signature does not verify as ES256 under the DeviceID"))
	}
	ev[offNonce] ^= 0x01
	if es256Verifies(pub, ev) {
		fail("attest", errors.New("the evidence signature still verifies with the nonce changed"))
	}
	fmt.Printf("wirecheck: attest tier %s: ES256 over the evidence verifies under the DeviceID\n", caps.CryptoTier)
}

// es256Verifies checks the evidence's raw r||s signature over SHA-256 of the
// signed span with crypto/ecdsa, the verifier a host would use.
func es256Verifies(pub *ecdsa.PublicKey, ev []byte) bool {
	digest := sha256.Sum256(ev[:offAlg])
	r := new(big.Int).SetBytes(ev[offSig : offSig+32])
	s := new(big.Int).SetBytes(ev[offSig+32 : offSig+64])
	return ecdsa.Verify(pub, digest[:], r, s)
}

// dial keeps trying until a PING round-trips. QEMU's user-mode network accepts
// on the host side before the guest listens and then drops the connection, so
// a connect alone proves nothing; an echoed PING proves the firmware. The
// harness runs this once the guest has logged its link up, so the guest's
// listen is moments away; each try is bounded by its dial and PING deadlines,
// with nothing slept between them.
//
// A refused connect is not retried: QEMU's forwarded port is bound for as long
// as QEMU runs, so a refusal means there is no QEMU to wait for. The same holds
// for the Cortex-M suite's Unix socket, which QEMU has bound before -daemonize
// returns.
func dial(network, addr string, wait, timeout time.Duration) (*tfw.Device, net.Conn, error) {
	deadline := time.Now().Add(wait)
	msg := []byte("tfw-esp32-qemu")
	var last error
	for time.Now().Before(deadline) {
		conn, err := net.DialTimeout(network, addr, 2*time.Second)
		if err != nil {
			return nil, nil, fmt.Errorf("dial %s %s: %w", network, addr, err)
		}
		d := tfw.New(conn, tfw.WithBootWait(0), tfw.WithTimeout(2*time.Second))
		echo, err := d.Ping(context.Background(), msg)
		if err == nil && bytes.Equal(echo, msg) {
			fmt.Printf("wirecheck: ping echoed over %s %s\n", network, addr)
			d = tfw.New(conn, tfw.WithBootWait(0), tfw.WithTimeout(timeout))
			return d, conn, nil
		}
		last = fmt.Errorf("ping: %v (echo %q)", err, echo)
		_ = conn.Close() // abandoned for a fresh dial; last already says why
	}
	return nil, nil, fmt.Errorf("no answer from %s within %v: %v", addr, wait, last)
}

// expectSilent is the refusal's observation on a row with no OTA: tfw-boot
// that will not boot an image stops (boot/hal/cortex-m/recovery_halt.c) and
// never answers. It connects -- so there is a QEMU listening -- and passes only
// if no PING is echoed for the whole window, each try bounded by its PING
// deadline. That QEMU was still running at the end is the caller's to check.
func expectSilent(network, addr string, window time.Duration) error {
	conn, err := net.DialTimeout(network, addr, 2*time.Second)
	if err != nil {
		return fmt.Errorf("dial %s %s: %w", network, addr, err)
	}
	defer func() { _ = conn.Close() }() // the verdict is already decided
	msg := []byte("tfw-silent")
	d := tfw.New(conn, tfw.WithBootWait(0), tfw.WithTimeout(2*time.Second))
	deadline := time.Now().Add(window)
	for time.Now().Before(deadline) {
		echo, err := d.Ping(context.Background(), msg)
		if err == nil && bytes.Equal(echo, msg) {
			return fmt.Errorf("a PING was echoed over %s %s: the image ran", network, addr)
		}
		if errors.Is(err, io.EOF) || errors.Is(err, net.ErrClosed) {
			return fmt.Errorf("%s %s closed: QEMU is gone: %w", network, addr, err)
		}
	}
	fmt.Printf("wirecheck: no answer over %s %s for %v\n", network, addr, window)
	return nil
}

// otaCheck streams the image in file through OTA_BEGIN/DATA/END and reads the
// session back (#48). With want < 0 the stream must be accepted: STATUS is
// complete with every byte counted, and INFO reports the written slot with its
// measurement. Otherwise it must be refused with status want -- a corrupted
// image at OTA_END -- and STATUS reads failed. Either way the running slot is
// printed, so a harness can see it is unchanged, and OTA_ACTIVATE with no
// OTA_AUTH before it is refused. A failure first prints the replies around it
// (see otaLog).
func otaCheck(ctx context.Context, d *tfw.Device, file string, want int) {
	image, err := os.ReadFile(filepath.Clean(file)) // #nosec G304 -- file is the operator's own -ota argument; wirecheck runs as the operator and can read nothing they could not
	if err != nil {
		fail("ota", err)
	}
	n := int64(len(image))
	if n < 0 || n > math.MaxUint32 {
		fail("ota", fmt.Errorf("%s is %d bytes; OTA_BEGIN's total_len carries at most %d", file, n, int64(math.MaxUint32)))
		return // fail exits; the return is for a reader who cannot see that
	}
	size := uint32(n)
	l := &otaLog{d: d, start: time.Now()}

	// The session one command at a time, not StageImage, so a failure can
	// say which reply it was. BEGIN and every DATA must be taken: the image
	// check is OTA_END's, and a refusal anywhere else is a different failure.
	if _, err := l.do(ctx, tfw.CmdOTABegin, binary.LittleEndian.AppendUint32(nil, size)); err != nil {
		l.fail("ota begin", err)
	}
	for off := uint32(0); off < size; {
		k := min(size-off, tfw.OTAChunk)
		p := binary.LittleEndian.AppendUint32(make([]byte, 0, 4+tfw.OTAChunk), off)
		if _, err := l.do(ctx, tfw.CmdOTAData, append(p, image[off:off+k]...)); err != nil {
			l.fail("ota data", err)
		}
		off += k
	}
	var st tfw.Status
	_, err = l.do(ctx, tfw.CmdOTAEnd, nil)
	if want < 0 && err != nil {
		l.fail("ota end", err)
	}
	if want >= 0 && (!errors.As(err, &st) || int(st) != want) {
		l.fail("ota end", fmt.Errorf("got %v, want status %d", err, want))
	}

	f, err := l.do(ctx, tfw.CmdOTAStatus, nil)
	if err != nil {
		l.fail("ota status", err)
	}
	if len(f.Payload) < 5 {
		l.fail("ota status", &tfw.ShortReplyError{Cmd: tfw.CmdOTAStatus, Got: len(f.Payload), Need: 5})
	}
	state, received := tfw.OTAState(f.Payload[0]), binary.LittleEndian.Uint32(f.Payload[1:5])
	if want < 0 && (state != tfw.OTAComplete || received != size) {
		l.fail("ota status", fmt.Errorf("state %d received %d, want %d %d",
			state, received, tfw.OTAComplete, size))
	}
	if want >= 0 && state != tfw.OTAFailed {
		l.fail("ota status", fmt.Errorf("state %d, want %d", state, tfw.OTAFailed))
	}

	for slot := byte(0); slot < 2; slot++ {
		f, err := l.do(ctx, tfw.CmdOTAInfo, []byte{slot})
		if err != nil {
			l.fail("ota info", err)
		}
		p := f.Payload
		if len(p) < tfw.OTAInfoLen {
			l.fail("ota info", &tfw.ShortReplyError{Cmd: tfw.CmdOTAInfo, Got: len(p), Need: tfw.OTAInfoLen})
		}
		// tfw.SlotInfo's layout: [slots:1][active:1][flags:1][floor:4]
		// [state:1][image_version:4][measurement:32].
		flags, floor, slotState := p[2], binary.LittleEndian.Uint32(p[3:7]), p[7]
		version, measurement := binary.LittleEndian.Uint32(p[8:12]), p[12:44]
		if want < 0 && slot == 1 && slotState != byte(tfw.OTAComplete) {
			l.fail("ota info", fmt.Errorf("slot 1 state %d, want %d", slotState, tfw.OTAComplete))
		}
		fmt.Printf("ota: slot %d state %d version %d floor %d flags 0x%02x measurement %x\n",
			slot, slotState, version, floor, flags, measurement)
	}

	// With no OTA_AUTH just before it, OTA_ACTIVATE is refused whatever the
	// session's state, and nothing is written.
	_, err = l.do(ctx, tfw.CmdOTAActivate, []byte{tfw.OTAActivateAtReset})
	if !errors.As(err, &st) || st != tfw.StatusErrAuthRequired {
		l.fail("ota activate", fmt.Errorf("got %v, want %v", err, tfw.StatusErrAuthRequired))
	}
	fmt.Printf("wirecheck: ota: %d commands in %v\n", l.sent, time.Since(l.start).Round(time.Millisecond))
}

// The ESP-IDF image layout boot/hal/esp32/l0_seal.py's layout() walks, and the
// trailer v1 fields (include/tfw/boot.h) an authorization names.
const (
	espMagic        = 0xE9
	espHeaderLen    = 24
	espHashAppended = 23
	espSegHeaderLen = 8
	trailerMagic    = 0x4D494654 // "TFIM"
	trailerLen      = 128
	trImageVersion  = 8
	trMeasurement   = 16
)

// signer is the key and the image one run's authorizations are for.
type signer struct {
	key         *ecdsa.PrivateKey
	version     uint32
	measurement [32]byte
}

// authorizer loads the P-256 key and the sealed image's trailer values. The
// signer signs the image it built and staged, never what the device reports
// (docs/ota/api.md, "Where the signed fields come from").
func authorizer(keyFile, imageFile string) *signer {
	if keyFile == "" || imageFile == "" {
		fail("auth", errors.New("-activate and -confirm need -auth-key, and -ota or -signed-image"))
	}
	key, err := loadP256(keyFile)
	if err != nil {
		fail("auth key", err)
	}
	image, err := os.ReadFile(filepath.Clean(imageFile)) // #nosec G304 -- the operator's own argument, as -ota is
	if err != nil {
		fail("signed image", err)
	}
	version, measurement, err := trailerOf(image)
	if err != nil {
		fail("signed image", fmt.Errorf("%s: %w", imageFile, err))
	}
	return &signer{key: key, version: version, measurement: measurement}
}

// loadP256 reads a PEM P-256 private key, SEC1 as espsecure.py writes the
// image key or PKCS #8. Any other curve is refused: ES256 only.
func loadP256(file string) (*ecdsa.PrivateKey, error) {
	raw, err := os.ReadFile(filepath.Clean(file)) // #nosec G304 -- the operator's own -auth-key argument
	if err != nil {
		return nil, err
	}
	for {
		var b *pem.Block
		if b, raw = pem.Decode(raw); b == nil {
			return nil, fmt.Errorf("%s: no PEM private key", file)
		}
		var key *ecdsa.PrivateKey
		switch b.Type {
		case "EC PARAMETERS":
			continue // python-ecdsa may write the curve first
		case "EC PRIVATE KEY":
			key, err = x509.ParseECPrivateKey(b.Bytes)
		case "PRIVATE KEY":
			var k any
			if k, err = x509.ParsePKCS8PrivateKey(b.Bytes); err == nil {
				var ok bool
				if key, ok = k.(*ecdsa.PrivateKey); !ok {
					return nil, fmt.Errorf("%s: a %T, not an ECDSA key", file, k)
				}
			}
		default:
			return nil, fmt.Errorf("%s: a %q PEM block, not a private key", file, b.Type)
		}
		if err != nil {
			return nil, fmt.Errorf("%s: %w", file, err)
		}
		if key.Curve != elliptic.P256() {
			return nil, fmt.Errorf("%s: curve %s, not P-256", file, key.Curve.Params().Name)
		}
		return key, nil
	}
}

// trailerOf returns a sealed image's trailer image_version and measurement.
// The trailer follows the appended SHA-256 that ends the last segment, where
// l0_seal.py puts it, and its measurement must be that SHA-256.
func trailerOf(img []byte) (uint32, [32]byte, error) {
	var m [32]byte
	if len(img) < espHeaderLen || img[0] != espMagic {
		return 0, m, errors.New("not an ESP-IDF image")
	}
	if img[espHashAppended] != 1 {
		return 0, m, errors.New("no appended SHA-256, so no trailer")
	}
	pos := espHeaderLen
	for range img[1] {
		if pos+espSegHeaderLen > len(img) {
			return 0, m, errors.New("a segment header runs past the image")
		}
		pos += espSegHeaderLen + int(binary.LittleEndian.Uint32(img[pos+4:pos+8]))
	}
	digest := (pos + 16) &^ 15 // the checksum byte ends the image on 16 bytes
	at := digest + 32
	if at+trailerLen > len(img) {
		return 0, m, errors.New("the image ends before its trailer")
	}
	t := img[at : at+trailerLen]
	if binary.LittleEndian.Uint32(t) != trailerMagic {
		return 0, m, errors.New("no trailer v1 after the appended SHA-256")
	}
	copy(m[:], t[trMeasurement:trMeasurement+32])
	if !bytes.Equal(m[:], img[digest:at]) {
		return 0, m, errors.New("the trailer's measurement is not the appended SHA-256")
	}
	return binary.LittleEndian.Uint32(t[trImageVersion:]), m, nil
}

// sigStructure is RFC 9052 §4.4's Sig_structure for a COSE_Sign1 with ES256's
// protected header h'A10126', no external AAD and the detached payload
// docs/ota/api.md names: head is the payload's array and its text string,
// tail what follows the version. boot/boot_cose.c builds the same bytes on
// the device.
func sigStructure(head []byte, m [32]byte, version uint32, tail ...byte) []byte {
	p := append(bytes.Clone(head), 0x58, 0x20)
	p = append(p, m[:]...)
	be := binary.BigEndian.AppendUint32(nil, version)
	switch { // CBOR's shortest unsigned integer (RFC 8949 §4.2.1)
	case version < 24:
		p = append(p, be[3])
	case version <= 0xFF:
		p = append(p, 0x18, be[3])
	case version <= 0xFFFF:
		p = append(append(p, 0x19), be[2:]...)
	default:
		p = append(append(p, 0x1A), be...)
	}
	p = append(p, tail...)
	n := len(p)
	if n > math.MaxUint8 {
		fail("auth", fmt.Errorf("a %d-byte payload; the bstr header here carries at most %d", n, math.MaxUint8))
		return nil
	}
	b := append([]byte{0x84, 0x6A}, "Signature1"...)
	b = append(b, 0x43, 0xA1, 0x01, 0x26, 0x40, 0x58, uint8(n))
	return append(b, p...)
}

// activateMessage is ["tfw-ota-activate", measurement, version, mode].
func (s *signer) activateMessage(mode byte) []byte {
	return sigStructure(append([]byte{0x84, 0x70}, "tfw-ota-activate"...), s.measurement, s.version, mode)
}

// confirmMessage is ["tfw-ota-confirm", measurement, version].
func (s *signer) confirmMessage() []byte {
	return sigStructure(append([]byte{0x83, 0x6F}, "tfw-ota-confirm"...), s.measurement, s.version)
}

// sign is ES256: ECDSA P-256 over SHA-256, sent as r||s.
func (s *signer) sign(msg []byte) [tfw.OTAAuthSigLen]byte {
	h := sha256.Sum256(msg)
	r, ss, err := ecdsa.Sign(rand.Reader, s.key, h[:])
	if err != nil {
		fail("auth sign", err)
	}
	var sig [tfw.OTAAuthSigLen]byte
	r.FillBytes(sig[:32])
	ss.FillBytes(sig[32:])
	return sig
}

// activateCheck sends OTA_ACTIVATE as a signer does. With no OTA_AUTH before
// it the command is refused; after one it is taken, and in mode 0 the
// authorization is spent: a second OTA_ACTIVATE is refused again. The
// application does not verify the signature. Layer 0 does, at the reset that
// boots the trial, so a key the device was not paired with is taken here and
// refused there.
func activateCheck(ctx context.Context, d *tfw.Device, s *signer, mode int) {
	m := tfw.OTAActivateAtReset
	switch mode {
	case 0:
	case 1:
		m = tfw.OTAActivateResetNow
	default:
		fail("ota activate", fmt.Errorf("-activate %d: the modes are 0 and 1", mode))
	}
	var st tfw.Status
	if err := d.OTAActivate(ctx, m); !errors.As(err, &st) || st != tfw.StatusErrAuthRequired {
		fail("ota activate", fmt.Errorf("with no OTA_AUTH: got %v, want %v", err, tfw.StatusErrAuthRequired))
	}
	if err := d.OTAAuth(ctx, s.sign(s.activateMessage(m))); err != nil {
		fail("ota auth", err)
	}
	if err := d.OTAActivate(ctx, m); err != nil {
		fail("ota activate", err)
	}
	fmt.Printf("wirecheck: ota activate mode %d taken after OTA_AUTH, version %d measurement %x\n",
		m, s.version, s.measurement)
	if m == tfw.OTAActivateAtReset {
		if err := d.OTAActivate(ctx, m); !errors.As(err, &st) || st != tfw.StatusErrAuthRequired {
			fail("ota activate", fmt.Errorf("a second time on one OTA_AUTH: got %v, want %v", err, tfw.StatusErrAuthRequired))
		}
	}
}

// confirmCheck sends OTA_CONFIRM as a signer does: refused with no OTA_AUTH
// before it, taken after one signed over the running image's trailer values.
func confirmCheck(ctx context.Context, d *tfw.Device, s *signer) {
	var st tfw.Status
	if err := d.OTAConfirm(ctx); !errors.As(err, &st) || st != tfw.StatusErrAuthRequired {
		fail("ota confirm", fmt.Errorf("with no OTA_AUTH: got %v, want %v", err, tfw.StatusErrAuthRequired))
	}
	if err := d.OTAAuth(ctx, s.sign(s.confirmMessage())); err != nil {
		fail("ota auth", err)
	}
	if err := d.OTAConfirm(ctx); err != nil {
		fail("ota confirm", err)
	}
	fmt.Printf("wirecheck: ota confirm taken after OTA_AUTH, version %d measurement %x\n", s.version, s.measurement)
}

// infoPrint prints OTA_INFO for both slots, in otaCheck's line format.
func infoPrint(ctx context.Context, d *tfw.Device) {
	for slot := byte(0); slot < 2; slot++ {
		si, err := d.OTAInfo(ctx, slot)
		if err != nil {
			fail("ota info", err)
		}
		fmt.Printf("ota: slot %d state %d version %d floor %d flags 0x%02x measurement %x\n",
			slot, si.State, si.ImageVersion, si.Floor, si.Flags, si.Measurement)
	}
}

// otaKeep is how many replies a failure report shows from each end of the
// stream: an image is thousands of OTA_DATA, and the middle repeats itself.
const otaKeep = 3

// otaNames names the commands otaCheck sends, for its report.
var otaNames = map[tfw.Cmd]string{
	tfw.CmdOTABegin: "OTA_BEGIN", tfw.CmdOTAData: "OTA_DATA", tfw.CmdOTAEnd: "OTA_END",
	tfw.CmdOTAStatus: "OTA_STATUS", tfw.CmdOTAInfo: "OTA_INFO", tfw.CmdOTAActivate: "OTA_ACTIVATE",
	tfw.CmdOTAAuth: "OTA_AUTH", tfw.CmdOTAConfirm: "OTA_CONFIRM",
}

// reply is one command's outcome as otaCheck saw it. err is set only when no
// reply came at all -- a timeout or a broken link; a refusal is a reply, with
// its status.
type reply struct {
	n       int
	cmd     tfw.Cmd
	status  tfw.Status
	length  int
	elapsed time.Duration
	err     error
}

func (r reply) String() string {
	head := fmt.Sprintf("#%d %s (0x%02x)", r.n, otaNames[r.cmd], byte(r.cmd))
	if r.err != nil {
		return fmt.Sprintf("%s no reply after %v: %v", head, r.elapsed.Round(time.Microsecond), r.err)
	}
	return fmt.Sprintf("%s status %d (%v) length %d in %v",
		head, uint8(r.status), r.status, r.length, r.elapsed.Round(time.Microsecond))
}

// otaLog sends otaCheck's commands and keeps what a failure report needs: the
// count, the first otaKeep replies and the last otaKeep. Each command waits at
// most the Device's own timeout, -timeout.
type otaLog struct {
	d     *tfw.Device
	start time.Time
	sent  int
	first []reply
	last  []reply
}

func (l *otaLog) do(ctx context.Context, cmd tfw.Cmd, payload []byte) (tfw.Frame, error) {
	start := time.Now()
	f, err := l.d.Do(ctx, cmd, payload)
	l.sent++
	r := reply{n: l.sent, cmd: cmd, status: f.Status, length: len(f.Payload), elapsed: time.Since(start)}
	var st tfw.Status
	if err != nil && !errors.As(err, &st) {
		r.err = err
	}
	if len(l.first) < otaKeep {
		l.first = append(l.first, r)
	}
	if len(l.last) == otaKeep {
		copy(l.last, l.last[1:])
		l.last[otaKeep-1] = r
	} else {
		l.last = append(l.last, r)
	}
	return f, err
}

// fail reports the replies, then fails check. The failing reply is the latest
// one: every check reads the reply its command just recorded.
func (l *otaLog) fail(check string, err error) {
	fmt.Fprintf(os.Stderr, "wirecheck: %s: %d OTA commands sent in %v\n",
		check, l.sent, time.Since(l.start).Round(time.Millisecond))
	for _, r := range l.first {
		fmt.Fprintf(os.Stderr, "wirecheck: %s:   first   %v\n", check, r)
	}
	for _, r := range l.last {
		if r.n > len(l.first) { // not already printed as one of the first
			fmt.Fprintf(os.Stderr, "wirecheck: %s:   last    %v\n", check, r)
		}
	}
	if len(l.last) > 0 {
		fmt.Fprintf(os.Stderr, "wirecheck: %s:   failing %v\n", check, l.last[len(l.last)-1])
	}
	fail(check, err)
}

func fail(check string, err error) {
	fmt.Fprintf(os.Stderr, "wirecheck: %s: %v\n", check, err)
	os.Exit(1)
}
