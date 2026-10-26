// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-License-Identifier: commercial

package tfwsim_test

import (
	"bytes"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"fmt"
	"io"
	"net"
	"net/netip"
	"os"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"testing"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"
	"github.com/jeremyhahn/trusted-firmware/sdk/go/tfwsim"
)

// recordEnv, set in a child's environment, makes this test binary stand in for
// a simulator. It writes the name it was started under and its arguments to
// the file recordEnv names, dials the endpoint in its network block when
// started with --eth, sends the BOOT event New waits for, and runs until its
// stdin closes. So the launchers are tested -- which binary, which arguments,
// which EEPROM image -- without the firmware; what the firmware answers is the
// tfwsim-tagged suite's job.
const recordEnv = "TFWSIM_FAKE_RECORD"

// noDialEnv makes the fake an Ethernet device that never dials.
const noDialEnv = "TFWSIM_FAKE_NO_DIAL"

// netcfgAddrEnv is what the fake prints for --netcfg-addr, the address its
// network block is read at. Unset, it reports 0x0140, tfw.NetConfigAddr, as
// the firmware does (#231).
const netcfgAddrEnv = "TFWSIM_FAKE_NETCFG_ADDR"

// fakeNetcfgAddr is the fake's answer to --netcfg-addr.
func fakeNetcfgAddr() string {
	if s := os.Getenv(netcfgAddrEnv); s != "" {
		return s
	}
	return "0x0140"
}

func TestMain(m *testing.M) {
	if record := os.Getenv(recordEnv); record != "" {
		if err := fakeSimulator(record, os.Args); err != nil {
			fmt.Fprintln(os.Stderr, "fake simulator:", err)
			os.Exit(1)
		}
		os.Exit(0)
	}
	os.Exit(m.Run())
}

func fakeSimulator(record string, argv []string) error {
	if slices.Equal(argv[1:], []string{"--netcfg-addr"}) {
		_, err := fmt.Println(fakeNetcfgAddr())
		return err
	}
	line := append([]string{filepath.Base(argv[0])}, argv[1:]...)
	if err := os.WriteFile(record, []byte(strings.Join(line, "\n")), 0o600); err != nil {
		return err
	}
	if slices.Contains(argv, "--eth") && os.Getenv(noDialEnv) == "" {
		conn, err := dialEndpoint(argv)
		if err != nil {
			return err
		}
		defer func() { _ = conn.Close() }()
	}
	boot, err := tfw.EncodeResponse(tfw.SeqNone, tfw.Cmd(0xFF), tfw.StatusOK, []byte{tfw.EventBoot})
	if err != nil {
		return err
	}
	if _, err := os.Stdout.Write(boot); err != nil {
		return err
	}
	_, err = io.Copy(io.Discard, os.Stdin)
	return err
}

// dialEndpoint reads the network block that follows the platform block in the
// --eeprom-bin image and dials its endpoint, as the device does at boot: the
// peer address at offset 24 of the block and the big-endian port at 28.
func dialEndpoint(argv []string) (net.Conn, error) {
	i := slices.Index(argv, "--eeprom-bin")
	if i < 0 || i+1 >= len(argv) {
		return nil, fmt.Errorf("--eth without --eeprom-bin")
	}
	image, err := os.ReadFile(argv[i+1])
	if err != nil {
		return nil, err
	}
	addr, err := strconv.ParseUint(fakeNetcfgAddr(), 0, 16)
	if err != nil {
		return nil, err
	}
	if uint64(len(image)) < addr+tfw.NetConfigLen {
		return nil, fmt.Errorf("EEPROM image of %d bytes has no network block at %#x", len(image), addr)
	}
	block := image[addr:]
	peer := netip.AddrFrom4([4]byte(block[24:28]))
	port := binary.BigEndian.Uint16(block[28:30])
	return net.Dial("tcp", netip.AddrPortFrom(peer, port).String())
}

// fake points the simulator directory at this test binary, under every
// simulator's name, and returns the file the child records its command line
// in.
func fake(t *testing.T) string {
	t.Helper()
	self, err := os.Executable()
	if err != nil {
		t.Fatal(err)
	}
	dir := t.TempDir()
	for _, name := range []string{"tfwsim", "tfwsim-atecc608a", "tfwsim-prov", "tfwsim-atecc608a-app-heartbeat"} {
		if err := os.Symlink(self, filepath.Join(dir, name)); err != nil {
			t.Fatal(err)
		}
	}
	record := filepath.Join(t.TempDir(), "argv")
	t.Setenv(tfwsim.DirEnv, dir)
	t.Setenv(recordEnv, record)
	return record
}

// commandLine is what the child recorded. It wrote it before the BOOT event,
// and the Start functions return only once New has seen that event, so it is
// there by the time a Start function returns.
func commandLine(t *testing.T, record string) []string {
	t.Helper()
	raw, err := os.ReadFile(record)
	if err != nil {
		t.Fatalf("the simulator recorded no command line: %v", err)
	}
	return strings.Split(string(raw), "\n")
}

// eeprom returns the image the command line hands the simulator.
func eeprom(t *testing.T, argv []string) []byte {
	t.Helper()
	i := slices.Index(argv, "--eeprom-bin")
	if i < 0 || i+1 >= len(argv) {
		t.Fatalf("command line %q has no --eeprom-bin", argv)
	}
	image, err := os.ReadFile(argv[i+1])
	if err != nil {
		t.Fatal(err)
	}
	return image
}

// identityArgs is the command line of the element tier provisioned with the
// test identity, with the EEPROM path left out.
func identityArgs(t *testing.T, argv []string) {
	t.Helper()
	want := []string{"tfwsim-atecc608a", "--eeprom-bin", "", "--provision-element",
		"--image-hash", hex.EncodeToString(tfwsim.TestFWHash[:])}
	if len(argv) != len(want) {
		t.Fatalf("command line %q, want %q", argv, want)
	}
	got := slices.Clone(argv)
	got[2] = ""
	if !slices.Equal(got, want) {
		t.Fatalf("command line %q, want %q", argv, want)
	}
}

func TestStartRunsTheSoftwareTierWithNoArguments(t *testing.T) {
	record := fake(t)
	if tfwsim.Start(t) == nil {
		t.Fatal("Start returned no Device")
	}
	if got := commandLine(t, record); !slices.Equal(got, []string{"tfwsim"}) {
		t.Fatalf("command line %q, want the software tier with no arguments", got)
	}
}

func TestStartElementProvisionsTheTestIdentity(t *testing.T) {
	record := fake(t)
	tfwsim.StartElement(t)

	argv := commandLine(t, record)
	identityArgs(t, argv)
	want := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if got := eeprom(t, argv); !bytes.Equal(got, want[:]) {
		t.Fatalf("EEPROM image\n%x\nwant the test identity's platform block\n%x", got, want)
	}
}

// The element+app image is the element simulator with the example
// application linked in, provisioned with the same test identity.
func TestStartElementAppRunsTheApplicationImageAsTheTestIdentity(t *testing.T) {
	record := fake(t)
	tfwsim.StartElementApp(t)

	argv := commandLine(t, record)
	if argv[0] != "tfwsim-atecc608a-app-heartbeat" {
		t.Fatalf("ran %q, want the element+app simulator", argv[0])
	}
	// Past the binary, the command line is the element simulator's.
	identityArgs(t, append([]string{"tfwsim-atecc608a"}, argv[1:]...))
	want := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if got := eeprom(t, argv); !bytes.Equal(got, want[:]) {
		t.Fatalf("EEPROM image\n%x\nwant the test identity's platform block\n%x", got, want)
	}
}

func TestStartElementWithOperatorBakesTheOperatorKey(t *testing.T) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		t.Fatal(err)
	}
	record := fake(t)
	tfwsim.StartElementWithOperator(t, &key.PublicKey)

	argv := commandLine(t, record)
	identityArgs(t, argv)
	sec1, err := key.PublicKey.Bytes()
	if err != nil {
		t.Fatal(err)
	}
	var point [tfw.OperatorPubkeyLen]byte
	copy(point[:], sec1[1:])
	want := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, point)
	if got := eeprom(t, argv); !bytes.Equal(got, want[:]) {
		t.Fatalf("EEPROM image\n%x\nwant the platform block carrying the operator key\n%x", got, want)
	}
}

// The Device reads and writes through what wrap returns, so a test standing
// in the middle sees the device's BOOT event go by.
func TestStartElementViaPutsTheWrapperBetweenDeviceAndSimulator(t *testing.T) {
	record := fake(t)
	var seen bytes.Buffer
	tfwsim.StartElementVia(t, func(conn io.ReadWriter) io.ReadWriter {
		return struct {
			io.Reader
			io.Writer
		}{io.TeeReader(conn, &seen), conn}
	})

	identityArgs(t, commandLine(t, record))
	boot, err := tfw.EncodeResponse(tfw.SeqNone, tfw.Cmd(0xFF), tfw.StatusOK, []byte{tfw.EventBoot})
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(seen.Bytes(), boot) {
		t.Fatalf("the wrapper saw %x, want the BOOT event %x", seen.Bytes(), boot)
	}
}

func TestStartBlankElementProvisionsNothing(t *testing.T) {
	record := fake(t)
	tfwsim.StartBlankElement(t)
	if got := commandLine(t, record); !slices.Equal(got, []string{"tfwsim-atecc608a"}) {
		t.Fatalf("command line %q, want the element tier with no arguments", got)
	}
}

func TestStartElementWithPassesTheArgumentsThrough(t *testing.T) {
	record := fake(t)
	args := []string{"--eeprom-bin", "baked.bin", "--no-handoff"}
	tfwsim.StartElementWith(t, args)
	if got := commandLine(t, record); !slices.Equal(got, append([]string{"tfwsim-atecc608a"}, args...)) {
		t.Fatalf("command line %q, want the element tier with %q", got, args)
	}
}

func TestStartProvisioningWithRunsTheJigImage(t *testing.T) {
	record := fake(t)
	args := []string{"--eeprom", "baked.eep"}
	tfwsim.StartProvisioningWith(t, args)
	if got := commandLine(t, record); !slices.Equal(got, append([]string{"tfwsim-prov"}, args...)) {
		t.Fatalf("command line %q, want the provisioning jig image with %q", got, args)
	}
}

// The device is started as an Ethernet device whose network block names the
// listener, and the connection it opens is the one handed back.
func TestStartElementEthernetAcceptsTheConnectionTheDeviceOpens(t *testing.T) {
	record := fake(t)
	serial, eth := tfwsim.StartElementEthernet(t)
	if serial == nil || eth == nil {
		t.Fatalf("StartElementEthernet returned serial %v, eth %v", serial, eth)
	}

	argv := commandLine(t, record)
	if !slices.Contains(argv, "--eth") {
		t.Fatalf("command line %q has no --eth", argv)
	}
	image := eeprom(t, argv)
	platform := tfw.BuildPlatformBlock(tfwsim.TestSerial, [tfw.PubkeyHashLen]byte{}, [tfw.OperatorPubkeyLen]byte{})
	if !bytes.Equal(image[:tfw.PlatformBlockLen], platform[:]) {
		t.Fatalf("EEPROM platform block\n%x\nwant the test identity's\n%x", image[:tfw.PlatformBlockLen], platform)
	}
	if len(image) != tfw.NetConfigAddr+tfw.NetConfigLen {
		t.Fatalf("EEPROM image is %d bytes, want the platform and network blocks (%d)",
			len(image), tfw.NetConfigAddr+tfw.NetConfigLen)
	}
	if addr, ok := eth.RemoteAddr().(*net.TCPAddr); !ok || !addr.IP.IsLoopback() {
		t.Fatalf("the accepted connection came from %v, want loopback", eth.RemoteAddr())
	}
}

// A device that never dials fails the test once the boot bound has passed,
// instead of hanging it on Accept.
func TestStartElementEthernetFailsWhenTheDeviceNeverConnects(t *testing.T) {
	fake(t)
	t.Setenv(noDialEnv, "1")
	f := &fatalTB{TB: t}
	done := make(chan struct{})
	go func() {
		defer close(done)
		tfwsim.StartElementEthernet(f)
	}()
	<-done
	if !strings.Contains(f.msg, "never connected") {
		t.Fatalf("StartElementEthernet failed with %q, want it to say the device never connected", f.msg)
	}
}

// The firmware, not the SDK, owns where its network block lives (#231 moved
// it from 0x0C0 to 0x140), so the harness writes the block where the
// simulator says it reads it, erased bytes between, and the device dials. The
// simulator here answers 0x0C0, not tfw.NetConfigAddr, so a harness that wrote
// at the constant would fail.
func TestStartElementEthernetWritesTheBlockAtTheAddressTheSimulatorReports(t *testing.T) {
	const at = 0x0C0
	record := fake(t)
	t.Setenv(netcfgAddrEnv, "0x00C0")
	_, eth := tfwsim.StartElementEthernet(t)

	image := eeprom(t, commandLine(t, record))
	if len(image) != at+tfw.NetConfigLen {
		t.Fatalf("EEPROM image is %d bytes, want the network block to end it at %#x (%d)",
			len(image), at, at+tfw.NetConfigLen)
	}
	for i := tfw.PlatformBlockLen; i < at; i++ {
		if image[i] != 0xFF {
			t.Fatalf("EEPROM byte %#x is %#02x, want it erased (0xFF)", i, image[i])
		}
	}
	var cfg tfw.NetConfig
	if err := cfg.UnmarshalBinary(image[at:]); err != nil {
		t.Fatalf("the block at %#x: %v", at, err)
	}
	// The fake dialled the endpoint it read at 0x0C0, so the link is up.
	if eth == nil || !cfg.Endpoint.Addr().IsLoopback() {
		t.Fatalf("link %v, endpoint %v in the block at %#x, want a link to a loopback endpoint", eth, cfg.Endpoint, at)
	}
}

// A simulator that cannot say where its block lives -- one built before it
// could, or one that answers an address inside the platform block -- fails
// the test before any simulator is started, and says to rebuild it.
func TestStartElementEthernetFailsWhenTheSimulatorReportsNoUsableAddress(t *testing.T) {
	for _, answer := range []string{"usage: tfwsim", "0x0010", "0xFFFF"} {
		record := fake(t)
		t.Setenv(netcfgAddrEnv, answer)
		f := &fatalTB{TB: t}
		done := make(chan struct{})
		go func() {
			defer close(done)
			tfwsim.StartElementEthernet(f)
		}()
		<-done
		if !strings.Contains(f.msg, "--netcfg-addr") || !strings.Contains(f.msg, "make tfwsim") {
			t.Errorf("answer %q: StartElementEthernet failed with %q, want it to name --netcfg-addr and make tfwsim",
				answer, f.msg)
		}
		if _, err := os.Stat(record); !os.IsNotExist(err) {
			t.Errorf("answer %q: a simulator was started (stat %v)", answer, err)
		}
	}
}

func TestStartElementEthernetFailsWhenTheSimulatorIsNotBuilt(t *testing.T) {
	t.Setenv(tfwsim.DirEnv, t.TempDir())
	f := &fatalTB{TB: t}
	done := make(chan struct{})
	go func() {
		defer close(done)
		tfwsim.StartElementEthernet(f)
	}()
	<-done
	if !strings.Contains(f.msg, "not built") || !strings.Contains(f.msg, tfwsim.DirEnv) {
		t.Errorf("StartElementEthernet failed with %q, want it to say the simulator is not built and name %s",
			f.msg, tfwsim.DirEnv)
	}
}

// goneTempDirTB hands out a temporary directory that does not exist, so the
// EEPROM image cannot be written.
type goneTempDirTB struct {
	*fatalTB
	dir string
}

func (g *goneTempDirTB) TempDir() string { return g.dir }

// An EEPROM image that cannot be written fails the test before any simulator
// starts with the wrong identity.
func TestStartFailsWhenTheEEPROMImageCannotBeWritten(t *testing.T) {
	record := fake(t)
	for name, start := range map[string]func(testing.TB){
		"StartElement":         func(tb testing.TB) { tfwsim.StartElement(tb) },
		"StartElementEthernet": func(tb testing.TB) { tfwsim.StartElementEthernet(tb) },
	} {
		g := &goneTempDirTB{fatalTB: &fatalTB{TB: t}, dir: filepath.Join(t.TempDir(), "gone")}
		done := make(chan struct{})
		go func() {
			defer close(done)
			start(g)
		}()
		<-done
		if !strings.Contains(g.msg, "gone") {
			t.Errorf("%s failed with %q, want the write to the missing directory", name, g.msg)
		}
	}
	if _, err := os.Stat(record); !os.IsNotExist(err) {
		t.Fatalf("a simulator was started (stat %v)", err)
	}
}

// The pinned public key is the P-256 point the constant spells.
func TestDeviceIDPublicKeyIsThePinnedPoint(t *testing.T) {
	pub := tfwsim.TestDeviceIDPublicKey()
	if pub.Curve != elliptic.P256() {
		t.Fatal("the test DeviceID key is not P-256")
	}
	raw, err := pub.Bytes()
	if err != nil {
		t.Fatal(err)
	}
	const want = "0441035035faf782ef1cab6a669f1c3809b89fbf1cd096771d12d87e1cb3c35aca" +
		"eda3e5021ff3ee9e48329bcc44c1061e1d5949f1fbba0742a5b6ac6799efe78a"
	if got := hex.EncodeToString(raw); got != want {
		t.Fatalf("TestDeviceIDPublicKey = %s, want %s", got, want)
	}
}
