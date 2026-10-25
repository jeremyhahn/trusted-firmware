package main

import (
	"bytes"
	"context"
	"crypto/ecdsa"
	"crypto/ed25519"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/x509"
	"encoding/binary"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"math/big"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
)

// The tests in this file kill the faults the vector tests in main_test.go
// leave alive. wirecheck's checks end in fail, which exits, so a check that
// must refuse runs in a child of this test binary (runChild) and the parent
// reads its exit code and stderr. The device is a fake that answers frames
// over net.Pipe; nothing here needs a device, QEMU or tfwsim.

// handler answers one request frame.
type handler func(cmd tfw.Cmd, p []byte) (tfw.Status, []byte)

// serveConn answers every request frame read from conn until it closes.
func serveConn(conn net.Conn, h handler) {
	var buf []byte
	chunk := make([]byte, 512)
	for {
		n, err := conn.Read(chunk)
		if err != nil {
			return
		}
		buf = append(buf, chunk[:n]...)
		for len(buf) > 0 {
			f, used, err := tfw.Decode(buf)
			if errors.Is(err, tfw.ErrShortFrame) {
				break
			}
			if err != nil {
				buf = buf[1:]
				continue
			}
			buf = buf[used:]
			st, p := h(f.Cmd(), f.Payload)
			out, err := tfw.EncodeResponse(f.Seq, f.Cmd(), st, p)
			if err != nil {
				return
			}
			if _, err := conn.Write(out); err != nil {
				return
			}
		}
	}
}

// fake returns a device whose replies come from h, and a stop that closes
// the pipe and waits for the server to return.
func fake(h handler) (*tfw.Device, func()) {
	host, dev := net.Pipe()
	done := make(chan struct{})
	go func() {
		defer close(done)
		serveConn(dev, h)
	}()
	stop := func() {
		_ = host.Close()
		_ = dev.Close()
		<-done
	}
	return tfw.New(host, tfw.WithBootWait(0), tfw.WithTimeout(5*time.Second)), stop
}

func fakeT(t *testing.T, h handler) *tfw.Device {
	t.Helper()
	d, stop := fake(h)
	t.Cleanup(stop)
	return d
}

// otaDevice is an OTA session that accepts an image in order and records
// what the host sent.
type otaDevice struct {
	mu       sync.Mutex
	total    uint32
	image    []byte
	chunks   []int
	slots    []byte
	endSt    tfw.Status
	state    tfw.OTAState
	statusSz int
	// slotIdle reports slot 1 idle; activateBad refuses OTA_ACTIVATE for a
	// reason other than the missing OTA_AUTH.
	slotIdle    bool
	activateBad bool
}

func (o *otaDevice) handle(cmd tfw.Cmd, p []byte) (tfw.Status, []byte) {
	o.mu.Lock()
	defer o.mu.Unlock()
	switch cmd {
	case tfw.CmdOTABegin:
		o.total = binary.LittleEndian.Uint32(p)
	case tfw.CmdOTAData:
		if int(binary.LittleEndian.Uint32(p)) != len(o.image) {
			return tfw.StatusErrBadState, nil
		}
		o.chunks = append(o.chunks, len(p)-4)
		o.image = append(o.image, p[4:]...)
	case tfw.CmdOTAEnd:
		return o.endSt, nil
	case tfw.CmdOTAStatus:
		r := binary.LittleEndian.AppendUint32([]byte{byte(o.state)}, uint32(len(o.image)))
		return tfw.StatusOK, r[:o.statusSz]
	case tfw.CmdOTAInfo:
		o.slots = append(o.slots, p[0])
		r := make([]byte, tfw.OTAInfoLen)
		if !o.slotIdle {
			r[7] = byte(tfw.OTAComplete)
		}
		return tfw.StatusOK, r
	case tfw.CmdOTAActivate:
		if o.activateBad {
			return tfw.StatusErrBadState, nil
		}
		return tfw.StatusErrAuthRequired, nil
	}
	return tfw.StatusOK, nil
}

func writeFile(t *testing.T, b []byte) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "f")
	if err := os.WriteFile(path, b, 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestOTACheckStreamsTheImageInChunks(t *testing.T) {
	img := make([]byte, 2*tfw.OTAChunk+10)
	for i := range img {
		img[i] = byte(i)
	}
	o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5}
	otaCheck(context.Background(), fakeT(t, o.handle), writeFile(t, img), -1)
	o.mu.Lock()
	defer o.mu.Unlock()
	if o.total != uint32(len(img)) || !bytes.Equal(o.image, img) {
		t.Fatalf("BEGIN %d, image %x", o.total, o.image)
	}
	if want := []int{tfw.OTAChunk, tfw.OTAChunk, 10}; !equalInts(o.chunks, want) {
		t.Fatalf("chunks %v, want %v", o.chunks, want)
	}
	if !bytes.Equal(o.slots, []byte{0, 1}) {
		t.Fatalf("OTA_INFO slots %v, want [0 1]", o.slots)
	}
}

func TestOTACheckTakesTheExpectedFailure(t *testing.T) {
	o := &otaDevice{endSt: tfw.StatusErrBadState, state: tfw.OTAFailed, statusSz: 5}
	otaCheck(context.Background(), fakeT(t, o.handle), writeFile(t, []byte{1, 2, 3}), int(tfw.StatusErrBadState))
}

func equalInts(a, b []int) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

// authDevice refuses OTA_ACTIVATE and OTA_CONFIRM unless an OTA_AUTH whose
// signature verifies over msg came just before. While an activation is
// pending, the same OTA_ACTIVATE again with no OTA_AUTH since is a retry and
// is taken; any other answers BAD_STATE (docs/ota/api.md, m1117 (1)).
// oneShot is the device before that rule, which never takes a retry; lax
// takes one after a new OTA_AUTH too.
type authDevice struct {
	mu       sync.Mutex
	pub      *ecdsa.PublicKey
	msg      func(cmd tfw.Cmd, p []byte) []byte
	oneShot  bool
	lax      bool
	grant    bool
	arrived  bool
	pending  bool
	mode     byte
	sig      []byte
	activate []byte
	confirms int
}

func (a *authDevice) handle(cmd tfw.Cmd, p []byte) (tfw.Status, []byte) {
	a.mu.Lock()
	defer a.mu.Unlock()
	switch cmd {
	case tfw.CmdOTAAuth:
		a.sig, a.grant, a.arrived = bytes.Clone(p), true, true
		return tfw.StatusOK, nil
	case tfw.CmdOTAActivate, tfw.CmdOTAConfirm:
		if cmd == tfw.CmdOTAActivate {
			a.activate = append(a.activate, p[0])
		} else {
			a.confirms++
		}
		if cmd == tfw.CmdOTAActivate && a.pending && !a.oneShot {
			a.grant = false
			if (a.lax || !a.arrived) && p[0] == a.mode {
				return tfw.StatusOK, nil
			}
			return tfw.StatusErrBadState, nil
		}
		if !a.grant || !verifies(a.pub, a.msg(cmd, p), a.sig) {
			return tfw.StatusErrAuthRequired, nil
		}
		a.grant = false
		if cmd == tfw.CmdOTAActivate {
			a.pending, a.mode, a.arrived = true, p[0], false
		}
	}
	return tfw.StatusOK, nil
}

func verifies(pub *ecdsa.PublicKey, msg, sig []byte) bool {
	h := sha256.Sum256(msg)
	return len(sig) == 64 &&
		ecdsa.Verify(pub, h[:], new(big.Int).SetBytes(sig[:32]), new(big.Int).SetBytes(sig[32:]))
}

func testSigner(t *testing.T) *signer {
	t.Helper()
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	s := &signer{key: key, version: 300}
	s.measurement[0] = 0xAA
	return s
}

func TestActivateCheckSignsTheModeItSends(t *testing.T) {
	for mode, want := range map[int][]byte{
		0: {tfw.OTAActivateAtReset, tfw.OTAActivateAtReset, tfw.OTAActivateAtReset, tfw.OTAActivateAtReset},
		1: {tfw.OTAActivateResetNow, tfw.OTAActivateResetNow},
	} {
		s := testSigner(t)
		a := &authDevice{pub: &s.key.PublicKey, msg: func(_ tfw.Cmd, p []byte) []byte { return s.activateMessage(p[0]) }}
		activateCheck(context.Background(), fakeT(t, a.handle), s, mode)
		a.mu.Lock()
		got := a.activate
		a.mu.Unlock()
		if !bytes.Equal(got, want) {
			t.Errorf("mode %d: OTA_ACTIVATE sent %v, want %v", mode, got, want)
		}
	}
}

func TestConfirmCheckSignsTheConfirmMessage(t *testing.T) {
	s := testSigner(t)
	a := &authDevice{pub: &s.key.PublicKey, msg: func(tfw.Cmd, []byte) []byte { return s.confirmMessage() }}
	confirmCheck(context.Background(), fakeT(t, a.handle), s)
	a.mu.Lock()
	defer a.mu.Unlock()
	if a.confirms != 2 {
		t.Fatalf("%d OTA_CONFIRM, want 2", a.confirms)
	}
}

func TestInfoPrintReadsBothSlots(t *testing.T) {
	o := &otaDevice{}
	infoPrint(context.Background(), fakeT(t, o.handle))
	o.mu.Lock()
	defer o.mu.Unlock()
	if !bytes.Equal(o.slots, []byte{0, 1}) {
		t.Fatalf("OTA_INFO slots %v, want [0 1]", o.slots)
	}
}

func TestCheckAttestNoneTierExpectsRefusal(t *testing.T) {
	var sent int
	d := fakeT(t, func(cmd tfw.Cmd, _ []byte) (tfw.Status, []byte) {
		sent++
		return tfw.StatusErrUnsupported, nil
	})
	checkAttest(context.Background(), d, &tfw.Caps{CryptoTier: tfw.CryptoNone}, nil)
	if sent != 1 {
		t.Fatalf("%d commands, want the one ATTEST", sent)
	}
}

func TestSignVerifiesUnderTheKey(t *testing.T) {
	s := testSigner(t)
	msg := []byte("message")
	sig := s.sign(msg)
	if !verifies(&s.key.PublicKey, msg, sig[:]) {
		t.Fatal("the signature does not verify as r||s over sha256(msg)")
	}
}

func TestES256VerifiesTheEvidenceHead(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	ev := make([]byte, offSig+64)
	for i := range offAlg {
		ev[i] = byte(i)
	}
	ev[offAlg] = algES256
	h := sha256.Sum256(ev[:offAlg])
	r, s, err := ecdsa.Sign(rand.Reader, key, h[:])
	if err != nil {
		t.Fatal(err)
	}
	r.FillBytes(ev[offSig : offSig+32])
	s.FillBytes(ev[offSig+32:])
	if !es256Verifies(&key.PublicKey, ev) {
		t.Fatal("a signature over ev[:offAlg] does not verify")
	}
	ev[offAlg-1] ^= 1
	if es256Verifies(&key.PublicKey, ev) {
		t.Fatal("verifies with the last signed byte changed")
	}
}

func TestSigStructureShortestVersion(t *testing.T) {
	for version, want := range map[uint32]string{
		0: "00", 23: "17", 24: "1818", 255: "18ff", 256: "190100",
		65535: "19ffff", 65536: "1a00010000", 0xFFFFFFFF: "1affffffff",
	} {
		s := &signer{version: version}
		got := s.confirmMessage()
		w, _ := hex.DecodeString(want)
		if !bytes.HasSuffix(got, w) || int(got[18]) != len(got)-19 {
			t.Errorf("version %d: %x, want the tail %s and bstr length %d", version, got, want, len(got)-19)
		}
	}
}

func TestSigStructureCarriesA255BytePayload(t *testing.T) {
	head := make([]byte, 255-2-32-1)
	got := sigStructure(head, [32]byte{}, 0)
	if len(got) != 19+255 || got[18] != 0xFF {
		t.Fatalf("length %d header 0x%02x, want %d and 0xff", len(got), got[18], 19+255)
	}
}

func TestTrailerOfSegments(t *testing.T) {
	for name, segs := range map[string][]int{
		"checksum on a 16-byte boundary": {32},
		"two segments":                   {5, 9},
	} {
		img := imageOf(segs, 9)
		version, _, err := trailerOf(img)
		if err != nil || version != 9 {
			t.Errorf("%s: version %d, %v", name, version, err)
		}
	}
	if _, _, err := trailerOf([]byte{espMagic}); err == nil {
		t.Error("one byte: no error")
	}
	trunc := imageOf([]int{4}, 1)
	trunc[1] = 200
	if _, _, err := trailerOf(trunc); err == nil {
		t.Error("a segment count past the image: no error")
	}
}

// imageOf is image() with the given segment lengths.
func imageOf(segs []int, version uint32) []byte {
	img := make([]byte, espHeaderLen)
	img[0], img[1], img[espHashAppended] = espMagic, byte(len(segs)), 1
	for _, n := range segs {
		img = binary.LittleEndian.AppendUint32(img, 0x3C000000)
		img = binary.LittleEndian.AppendUint32(img, uint32(n))
		img = append(img, make([]byte, n)...)
	}
	for len(img)%16 != 15 {
		img = append(img, 0)
	}
	img = append(img, 0xEF)
	digest := sha256.Sum256(img)
	img = append(img, digest[:]...)
	tr := make([]byte, trailerLen)
	binary.LittleEndian.PutUint32(tr, trailerMagic)
	binary.LittleEndian.PutUint32(tr[trImageVersion:], version)
	copy(tr[trMeasurement:], digest[:])
	return append(img, tr...)
}

func pemFile(t *testing.T, blocks ...*pem.Block) string {
	t.Helper()
	var b []byte
	for _, blk := range blocks {
		b = append(b, pem.EncodeToMemory(blk)...)
	}
	return writeFile(t, b)
}

func TestLoadP256(t *testing.T) {
	p256, _ := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	p384, _ := ecdsa.GenerateKey(elliptic.P384(), rand.Reader)
	_, ed, _ := ed25519.GenerateKey(rand.Reader)
	sec1, _ := x509.MarshalECPrivateKey(p256)
	sec1384, _ := x509.MarshalECPrivateKey(p384)
	pk8, _ := x509.MarshalPKCS8PrivateKey(p256)
	pk8ed, _ := x509.MarshalPKCS8PrivateKey(ed)
	params := &pem.Block{Type: "EC PARAMETERS", Bytes: []byte{6, 8, 0x2a, 0x86, 0x48, 0xce, 0x3d, 3, 1, 7}}
	for name, path := range map[string]string{
		"sec1":         pemFile(t, &pem.Block{Type: "EC PRIVATE KEY", Bytes: sec1}),
		"params first": pemFile(t, params, &pem.Block{Type: "EC PRIVATE KEY", Bytes: sec1}),
		"pkcs8":        pemFile(t, &pem.Block{Type: "PRIVATE KEY", Bytes: pk8}),
	} {
		key, err := loadP256(path)
		if err != nil || !key.Equal(p256) {
			t.Errorf("%s: %v", name, err)
		}
	}
	for name, path := range map[string]string{
		"missing":           filepath.Join(t.TempDir(), "none"),
		"no pem":            writeFile(t, []byte("not pem")),
		"params alone":      pemFile(t, params),
		"certificate":       pemFile(t, &pem.Block{Type: "CERTIFICATE", Bytes: sec1}),
		"certificate first": pemFile(t, &pem.Block{Type: "CERTIFICATE", Bytes: sec1}, &pem.Block{Type: "EC PRIVATE KEY", Bytes: sec1}),
		"bad sec1":          pemFile(t, &pem.Block{Type: "EC PRIVATE KEY", Bytes: []byte{1}}),
		"bad pkcs8":         pemFile(t, &pem.Block{Type: "PRIVATE KEY", Bytes: []byte{1}}),
		"ed25519 pkcs8":     pemFile(t, &pem.Block{Type: "PRIVATE KEY", Bytes: pk8ed}),
		"p384":              pemFile(t, &pem.Block{Type: "EC PRIVATE KEY", Bytes: sec1384}),
	} {
		if key, err := loadP256(path); err == nil || key != nil {
			t.Errorf("%s: key %v, err %v", name, key, err)
		}
	}
}

func TestReplyString(t *testing.T) {
	ok := reply{n: 2, cmd: tfw.CmdOTAEnd, status: tfw.StatusErrBadState, length: 4}.String()
	if !strings.HasPrefix(ok, "#2 OTA_END (0x") || !strings.Contains(ok, "status 19 ") || !strings.Contains(ok, "length 4") {
		t.Errorf("status reply: %q", ok)
	}
	lost := reply{n: 1, cmd: tfw.CmdOTABegin, err: errors.New("gone")}.String()
	if !strings.Contains(lost, "#1 OTA_BEGIN") || !strings.Contains(lost, "no reply") || !strings.HasSuffix(lost, "gone") {
		t.Errorf("lost reply: %q", lost)
	}
}

func TestOTALogKeepsTheFirstAndLast(t *testing.T) {
	d := fakeT(t, func(cmd tfw.Cmd, _ []byte) (tfw.Status, []byte) {
		if cmd == tfw.CmdOTAEnd {
			return tfw.StatusErrBadState, nil
		}
		return tfw.StatusOK, nil
	})
	l := &otaLog{d: d, start: time.Now()}
	for range 4 {
		if _, err := l.do(context.Background(), tfw.CmdOTAStatus, nil); err != nil {
			t.Fatal(err)
		}
	}
	if _, err := l.do(context.Background(), tfw.CmdOTAEnd, nil); !errors.Is(err, tfw.StatusErrBadState) {
		t.Fatalf("OTA_END: %v", err)
	}
	ns := func(rs []reply) []int {
		var n []int
		for _, r := range rs {
			n = append(n, r.n)
		}
		return n
	}
	if l.sent != 5 || !equalInts(ns(l.first), []int{1, 2, 3}) || !equalInts(ns(l.last), []int{3, 4, 5}) {
		t.Fatalf("sent %d first %v last %v", l.sent, ns(l.first), ns(l.last))
	}
	if r := l.last[2]; r.err != nil || r.status != tfw.StatusErrBadState || r.cmd != tfw.CmdOTAEnd {
		t.Fatalf("a status refusal is recorded as %+v", r)
	}
}

func TestDial(t *testing.T) {
	echo := func(wrong bool) string {
		ln, err := net.Listen("tcp", "127.0.0.1:0")
		if err != nil {
			t.Fatal(err)
		}
		var wg sync.WaitGroup
		t.Cleanup(func() { _ = ln.Close(); wg.Wait() })
		wg.Add(1)
		go func() {
			defer wg.Done()
			for {
				c, err := ln.Accept()
				if err != nil {
					return
				}
				wg.Add(1)
				go func() {
					defer wg.Done()
					defer func() { _ = c.Close() }()
					serveConn(c, func(_ tfw.Cmd, p []byte) (tfw.Status, []byte) {
						if wrong {
							return tfw.StatusOK, append([]byte{'x'}, p...)
						}
						return tfw.StatusOK, p
					})
				}()
			}
		}()
		return ln.Addr().String()
	}

	d, conn, err := dial("tcp", echo(false), 5*time.Second, time.Second)
	if err != nil {
		t.Fatal(err)
	}
	if got, err := d.Ping(context.Background(), []byte("again")); err != nil || string(got) != "again" {
		t.Fatalf("the returned device: %q %v", got, err)
	}
	_ = conn.Close()

	if _, _, err := dial("tcp", echo(true), 200*time.Millisecond, time.Second); err == nil ||
		!strings.Contains(err.Error(), "no answer") || !strings.Contains(err.Error(), "echo") {
		t.Errorf("a wrong echo: %v", err)
	}
	if _, _, err := dial("tcp", echo(false), 0, time.Second); err == nil || !strings.Contains(err.Error(), "no answer") {
		t.Errorf("no wait: %v", err)
	}

	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	closed := ln.Addr().String()
	_ = ln.Close()
	if _, _, err := dial("tcp", closed, 5*time.Second, time.Second); err == nil || !strings.HasPrefix(err.Error(), "dial ") {
		t.Errorf("a refused dial: %v", err)
	}
}

// The child side. Each entry must end in fail; returning is a test failure
// the parent sees as exit 0.
const childEnv = "WIRECHECK_MUTATE_CHILD"

var children = map[string]func(){
	"activate bad mode": func() { activateCheck(context.Background(), nil, &signer{}, 2) },
	"activate unauthed accepted": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusOK, nil })
		activateCheck(context.Background(), d, childSigner(), 1)
	},
	"activate retry refused": func() {
		s := childSigner()
		a := &authDevice{pub: &s.key.PublicKey, oneShot: true, msg: func(_ tfw.Cmd, p []byte) []byte { return s.activateMessage(p[0]) }}
		d, _ := fake(a.handle)
		activateCheck(context.Background(), d, s, 0)
	},
	"activate retry after auth taken": func() {
		s := childSigner()
		a := &authDevice{pub: &s.key.PublicKey, lax: true, msg: func(_ tfw.Cmd, p []byte) []byte { return s.activateMessage(p[0]) }}
		d, _ := fake(a.handle)
		activateCheck(context.Background(), d, s, 0)
	},
	"activate auth refused": func() {
		d, _ := fake(func(cmd tfw.Cmd, _ []byte) (tfw.Status, []byte) {
			if cmd == tfw.CmdOTAAuth {
				return tfw.StatusErrBadState, nil
			}
			return tfw.StatusErrAuthRequired, nil
		})
		activateCheck(context.Background(), d, childSigner(), 0)
	},
	"activate refused after auth": func() {
		d, _ := fake(func(cmd tfw.Cmd, _ []byte) (tfw.Status, []byte) {
			if cmd == tfw.CmdOTAAuth {
				return tfw.StatusOK, nil
			}
			return tfw.StatusErrAuthRequired, nil
		})
		activateCheck(context.Background(), d, childSigner(), 1)
	},
	"confirm unauthed accepted": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusOK, nil })
		confirmCheck(context.Background(), d, childSigner())
	},
	"confirm refused after auth": func() {
		d, _ := fake(func(cmd tfw.Cmd, _ []byte) (tfw.Status, []byte) {
			if cmd == tfw.CmdOTAAuth {
				return tfw.StatusOK, nil
			}
			return tfw.StatusErrAuthRequired, nil
		})
		confirmCheck(context.Background(), d, childSigner())
	},
	"sig structure too long": func() { sigStructure(make([]byte, 256-2-32-1), [32]byte{}, 0) },
	"authorizer no key":      func() { authorizer("", "image") },
	"authorizer no image":    func() { authorizer("key", "") },
	"ota end wrong status": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(5*tfw.OTAChunk), int(tfw.StatusErrBadState))
	},
	"ota end refused": func() {
		o := &otaDevice{endSt: tfw.StatusErrBadState, state: tfw.OTAComplete, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), -1)
	},
	"ota not complete": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAFailed, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), -1)
	},
	"ota not failed": func() {
		o := &otaDevice{endSt: tfw.StatusErrBadState, state: tfw.OTAComplete, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), int(tfw.StatusErrBadState))
	},
	"ota status short": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 4}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), -1)
	},
	"ota end other status": func() {
		o := &otaDevice{endSt: tfw.StatusErrBadState, state: tfw.OTAFailed, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), int(tfw.StatusErrAuthRequired))
	},
	"ota end wrong status, three sent": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(tfw.OTAChunk), int(tfw.StatusErrBadState))
	},
	"ota slot 1 idle": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5, slotIdle: true}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), -1)
	},
	"ota activate other refusal": func() {
		o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5, activateBad: true}
		d, _ := fake(o.handle)
		otaCheck(context.Background(), d, childImage(10), -1)
	},
	"activate other refusal": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusErrBadState, nil })
		activateCheck(context.Background(), d, childSigner(), 0)
	},
	"confirm other refusal": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusErrBadState, nil })
		confirmCheck(context.Background(), d, childSigner())
	},
	"attest no algorithm": func() {
		key := childSigner().key
		checkAttest(context.Background(), nil, &tfw.Caps{CryptoTier: tfw.CryptoSoftware, Features: tfw.FeatureAttest}, &key.PublicKey)
	},
	"ota missing file": func() { otaCheck(context.Background(), nil, filepath.Join(os.TempDir(), "wirecheck-none", "x"), -1) },
	"attest none tier signs": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusErrUnsupported, nil })
		checkAttest(context.Background(), d, &tfw.Caps{Algorithms: tfw.AlgECDSAP256SHA256}, nil)
	},
	"attest none tier accepted": func() {
		d, _ := fake(func(tfw.Cmd, []byte) (tfw.Status, []byte) { return tfw.StatusOK, make([]byte, 200) })
		checkAttest(context.Background(), d, &tfw.Caps{}, nil)
	},
	"attest no deviceid": func() {
		checkAttest(context.Background(), nil, &tfw.Caps{CryptoTier: tfw.CryptoSoftware}, nil)
	},
	"attest no feature": func() {
		key := childSigner().key
		checkAttest(context.Background(), nil, &tfw.Caps{CryptoTier: tfw.CryptoSoftware, Algorithms: tfw.AlgECDSAP256SHA256}, &key.PublicKey)
	},
	"main bad deviceid": func() {
		os.Args = []string{"wirecheck", "-deviceid", "zz"}
		main()
	},
}

func childSigner() *signer {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		panic(err)
	}
	return &signer{key: key, version: 1}
}

func childImage(n int) string {
	f, err := os.CreateTemp("", "wirecheck-mutate-*")
	if err != nil {
		panic(err)
	}
	if _, err := f.Write(make([]byte, n)); err != nil {
		panic(err)
	}
	_ = f.Close()
	return f.Name()
}

func TestMain(m *testing.M) {
	if name := os.Getenv(childEnv); name != "" {
		children[name]()
		os.Exit(0)
	}
	os.Exit(m.Run())
}

// runChild runs one children entry in a copy of this test binary and
// returns its stderr, failing t unless it exited 1.
func runChild(t *testing.T, name string) string {
	t.Helper()
	cmd := exec.Command(os.Args[0], "-test.run=^$")                          // #nosec G204 -- this test binary, re-run as a child
	cmd.Env = append(os.Environ(), childEnv+"="+name, "TMPDIR="+t.TempDir()) // childImage's files go with t
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	err := cmd.Run()
	var exit *exec.ExitError
	if !errors.As(err, &exit) || exit.ExitCode() != 1 {
		t.Errorf("%s: %v, want exit status 1; stderr %q", name, err, stderr.String())
	}
	return stderr.String()
}

func TestChecksRefuse(t *testing.T) {
	for name, want := range map[string]string{
		"activate bad mode":               "wirecheck: ota activate: -activate 2",
		"activate unauthed accepted":      "wirecheck: ota activate: with no OTA_AUTH",
		"activate retry refused":          "wirecheck: ota activate: the same exchange again: got command requires",
		"activate retry after auth taken": "wirecheck: ota activate: again after a new OTA_AUTH: got <nil>, want command not valid",
		"activate auth refused":           "wirecheck: ota auth: ",
		"activate refused after auth":     "wirecheck: ota activate: command requires",
		"confirm unauthed accepted":       "wirecheck: ota confirm: with no OTA_AUTH",
		"confirm refused after auth":      "wirecheck: ota confirm: command requires",
		"sig structure too long":          "wirecheck: auth: a 256-byte payload",
		"authorizer no key":               "wirecheck: auth: -activate and -confirm need",
		"authorizer no image":             "wirecheck: auth: -activate and -confirm need",
		"ota end refused":                 "wirecheck: ota end: ",
		"ota not complete":                "wirecheck: ota status: state 3 received 10, want 2 10",
		"ota not failed":                  "wirecheck: ota status: state 2, want 3",
		"ota status short":                "wirecheck: ota status: tfw: command 0x6b reply is 4 bytes, needs at least 5",
		"ota end other status":            "wirecheck: ota end: got command not valid in the current OTA state, want status 17",
		"ota slot 1 idle":                 "wirecheck: ota info: slot 1 state 0, want 2",
		"ota activate other refusal":      "wirecheck: ota activate: got command not valid",
		"activate other refusal":          "wirecheck: ota activate: with no OTA_AUTH: got command not valid",
		"confirm other refusal":           "wirecheck: ota confirm: with no OTA_AUTH: got command not valid",
		"attest no algorithm":             "wirecheck: caps: tier software: features 0x",
		"ota missing file":                "wirecheck: ota: ",
		"attest none tier signs":          "wirecheck: caps: none tier reports",
		"attest none tier accepted":       "wirecheck: attest: none tier answered",
		"attest no deviceid":              "wirecheck: attest: tier software signs ATTEST",
		"attest no feature":               "wirecheck: caps: tier software",
		"main bad deviceid":               "wirecheck: deviceid: -deviceid is not a P-256 point",
	} {
		if got := runChild(t, name); !strings.Contains(got, want) {
			t.Errorf("%s: stderr %q, want %q", name, got, want)
		}
	}
}

// otaLog.fail prints the first three replies, the last three not already
// printed, and the failing one.
func TestOTAFailPrintsTheWindow(t *testing.T) {
	got := runChild(t, "ota end wrong status")
	for _, want := range []string{
		"wirecheck: ota end: 7 OTA commands sent in ",
		"first   #1 OTA_BEGIN", "first   #3 OTA_DATA",
		"last    #5 OTA_DATA", "last    #7 OTA_END",
		"failing #7 OTA_END (0x", "wirecheck: ota end: got <nil>, want status 19",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("stderr lacks %q:\n%s", want, got)
		}
	}
	for _, not := range []string{"last    #3 ", "last    #4 ", "first   #4 "} {
		if strings.Contains(got, not) {
			t.Errorf("stderr has %q:\n%s", not, got)
		}
	}
	// Three sent: all three are the first, none printed again as the last.
	got = runChild(t, "ota end wrong status, three sent")
	if !strings.Contains(got, "first   #3 OTA_END") || strings.Contains(got, "last ") ||
		!strings.Contains(got, "failing #3 OTA_END") {
		t.Errorf("three sent:\n%s", got)
	}
}

func TestOTACheckSendsAOneByteLastChunk(t *testing.T) {
	img := make([]byte, 2*tfw.OTAChunk+1)
	img[len(img)-1] = 0x5A
	o := &otaDevice{endSt: tfw.StatusOK, state: tfw.OTAComplete, statusSz: 5}
	otaCheck(context.Background(), fakeT(t, o.handle), writeFile(t, img), -1)
	o.mu.Lock()
	defer o.mu.Unlock()
	if !bytes.Equal(o.image, img) {
		t.Fatalf("the device holds %d bytes, want %d", len(o.image), len(img))
	}
}
