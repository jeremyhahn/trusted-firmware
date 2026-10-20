package avr

import (
	"bytes"
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/sha256"
	"crypto/x509"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"io"
	"log/slog"
	"math"
	"math/big"
	"os"
	"path/filepath"
	"testing"
	"time"

	tfw "github.com/jeremyhahn/trusted-firmware/sdk/go"

	"github.com/jeremyhahn/go-xkms/pkg/attestation/truststore"
	"github.com/jeremyhahn/go-xkms/pkg/types"

	"github.com/stretchr/testify/require"
)

// Mutation tests for what this package itself parses: its Config, the nonce
// and key attributes a caller hands it, the point PROV_PUBKEY answers, the
// alias certificate ATTEST_CERT answers (as far as handing it to dice.Verify
// -- what DICE evidence means is go-xkms's), and the signature ATTEST_CSR
// answers. Every mutation must come back as an error and never as a key, an
// attestation or a signature. The mutations are enumerated, not drawn, so a
// failing case names itself.

// packageErrors is every sentinel this package returns. A refusal of input
// this package parses must be one of them.
var packageErrors = []error{
	ErrNilConfig, ErrNilDevice, ErrInvalidFactoryConfig, ErrMissingKeyCN, ErrInvalidSlot,
	ErrBackendClosed, ErrInvalidKeyAttributes, ErrUnknownKey, ErrInvalidNonce, ErrTrustStoreInit,
	ErrSigningUnsupported, ErrInvalidAttestation, ErrPossessionMismatch, ErrDecryptionUnsupported,
	ErrGenerationUnsupported, ErrDeletionUnsupported, ErrRotationUnsupported, ErrMalformedCertificate,
	ErrProtocolVersion, ErrUnknownCryptoTier, ErrNoDeviceKey, ErrInvalidDevicePoint,
	ErrDeviceUnreachable, ErrDeviceProtocol, ErrDeviceRefused, ErrCommandUnavailable,
	ErrNotProvisioned, ErrDeviceCrypto, ErrAuthRequired, ErrDeviceBusy, ErrProtectedRegion,
	ErrDeviceInternal, ErrUnsupportedByDevice, ErrElementUnreachable, ErrInvalidArgument,
}

func requirePackageError(t *testing.T, label string, err error) {
	t.Helper()
	require.Error(t, err, label)
	for _, s := range packageErrors {
		if errors.Is(err, s) {
			return
		}
	}
	t.Fatalf("%s: %v is not one of this package's errors", label, err)
}

// The fixed key every case below is built on: d = 0x42 repeated, its point,
// and one signature it made over sha256(fixedCRI). Literals, so a case is the
// same bytes on every run; crypto/ecdsa no longer takes a caller's entropy.
const (
	fixedPointHex = "3ad3861a95621392516bb593ef05583ed2e5866f5cb6260a3017237fd89b90af" +
		"d0961c7e37075a6791a39c61f56295b02b6d26567b615e60aa41ee1c8e83388d"
	fixedSigHex = "86779a35f0bb0fdffafaf8e10ef7438dff2979409ea897f19782dac2244a7e70" +
		"89da3da80296fb265722344165665dfe44ed914b4d866df4362f125bb7e7ef87"
)

var fixedCRI = []byte("certification request info")

func fixedKey(tb testing.TB) (*ecdsa.PrivateKey, []byte) {
	tb.Helper()
	key, err := ecdsa.ParseRawPrivateKey(elliptic.P256(), bytes.Repeat([]byte{0x42}, 32))
	require.NoError(tb, err)
	point, err := hex.DecodeString(fixedPointHex)
	require.NoError(tb, err)
	return key, point
}

// fixedPossession is a backend whose device answers ATTEST_CSR with
// fixedSigHex, and a signer over fixedKey's attestation.
func fixedPossession(tb testing.TB) (*fakeLink, crypto.Signer, *ecdsa.PublicKey, [32]byte, []byte) {
	tb.Helper()
	key, _ := fixedKey(tb)
	sig, err := hex.DecodeString(fixedSigHex)
	require.NoError(tb, err)
	link := newFakeLink().
		answer(tfw.CmdCaps, capsPayload(tfw.CryptoSecureElement,
			tfw.FeatureAttest|tfw.FeatureAliasCert|tfw.FeatureAttestCSR, tfw.AlgECDSAP256SHA256)).
		answer(tfw.CmdAttestCSR, sig)
	b, err := NewBackend(context.Background(), quietConfig(), newTestDevice(link))
	require.NoError(tb, err)
	tb.Cleanup(func() { _ = b.Close() })
	signer, err := b.PossessionSigner(&Attestation{
		Format: FormatDICE, Certificate: &x509.Certificate{PublicKey: &key.PublicKey}, Nonce: possessionNonce,
	})
	require.NoError(tb, err)
	return link, signer, &key.PublicKey, sha256.Sum256(fixedCRI), sig
}

func quietConfig() *Config {
	c := testConfig()
	c.Logger = slog.New(slog.NewTextHandler(io.Discard, nil))
	return c
}

func TestEveryMutatedConfigFieldIsRefusedBeforeTheWire(t *testing.T) {
	cases := 0
	refuse := func(label string, c *Config, want error) {
		t.Helper()
		link := secureElementLink()
		b, err := NewBackend(context.Background(), c, newTestDevice(link))
		require.Nil(t, b, label)
		require.ErrorIs(t, err, want, label)
		require.Empty(t, link.requests, "%s: refused after %d frames", label, len(link.requests))
		cases++
	}

	refuse("nil config", nil, ErrNilConfig)

	c := quietConfig()
	c.KeyCN = ""
	refuse("key_cn empty", c, ErrMissingKeyCN)

	for slot := 0; slot <= 0xFF; slot++ {
		if uint8(slot) == tfw.SlotDeviceID {
			continue
		}
		c := quietConfig()
		c.Slot = uint8(slot)
		refuse("slot "+hex.EncodeToString([]byte{byte(slot)}), c, ErrInvalidSlot)
	}

	b, err := NewBackend(context.Background(), quietConfig(), nil)
	require.Nil(t, b)
	require.ErrorIs(t, err, ErrNilDevice)
	cases++

	// RequestTimeout is the one field a bad value of is replaced rather than
	// refused, and Config says so: zero or below selects the default.
	for _, d := range []time.Duration{0, -1, -time.Hour, math.MinInt64} {
		c := quietConfig()
		c.RequestTimeout = d
		require.NoError(t, c.Validate())
		require.Equal(t, DefaultRequestTimeout, c.RequestTimeout, "request_timeout %d", d)
		cases++
	}
	t.Logf("%d config mutations refused", cases)
}

func TestEveryMutatedAttestationConfigIsRefused(t *testing.T) {
	device := newAliasDevice(t)
	dir := t.TempDir()
	cases := 0

	newWith := func(att *AttestationConfig) (*Backend, error) {
		c := quietConfig()
		c.Attestation = att
		return NewBackend(context.Background(), c, newTestDevice(secureElementLink()))
	}

	// An intermediate cut at every length, extended, or replaced by noise is
	// refused when the backend is made, never at the first attestation.
	good := device.deviceIDDER
	var bad [][]byte
	for n := 0; n < len(good); n++ {
		bad = append(bad, good[:n])
	}
	bad = append(bad,
		append(bytes.Clone(good), 0x00),
		append(bytes.Clone(good), good...),
		bytes.Repeat([]byte{0x30}, 64),
		pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: good}),
	)
	for i, der := range bad {
		b, err := newWith(&AttestationConfig{Intermediates: [][]byte{der}})
		require.Nil(t, b, "intermediate mutation %d", i)
		require.Error(t, err, "intermediate mutation %d (%d bytes)", i, len(der))
		cases++
	}
	// A good intermediate beside a bad one is still refused.
	b, err := newWith(&AttestationConfig{Intermediates: [][]byte{good, good[:len(good)-1]}})
	require.Nil(t, b)
	require.Error(t, err)
	cases++

	// The trust store's roots: absent, empty, not PEM, a PEM of a cut
	// certificate.
	write := func(name string, data []byte) string {
		p := filepath.Join(dir, name)
		require.NoError(t, os.WriteFile(p, data, 0o600))
		return p
	}
	rootPEM := pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: device.rootDER})
	for name, path := range map[string]string{
		"absent":    filepath.Join(dir, "absent.pem"),
		"empty":     write("empty.pem", nil),
		"not pem":   write("noise.pem", []byte("not a certificate\n")),
		"cut der":   write("cut.pem", pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: device.rootDER[:len(device.rootDER)/2]})),
		"cut pem":   write("cutpem.pem", rootPEM[:len(rootPEM)/2]),
		"directory": dir,
	} {
		b, err := newWith(&AttestationConfig{TrustStore: &truststore.Config{ExternalRootPaths: []string{path}}})
		require.Nil(t, b, "trust store %s", name)
		requirePackageError(t, "trust store "+name, err)
		cases++
	}
	t.Logf("%d attestation-config mutations refused", cases)
}

func TestEveryMutatedNonceAndKeyNameIsRefusedBeforeTheWire(t *testing.T) {
	link := secureElementLink()
	b := newTestBackend(t, link)
	ctx := context.Background()
	before := len(link.requests)
	cases := 0

	for n := 0; n <= 2*tfw.AliasNonceLen+1; n++ {
		if n == tfw.AliasNonceLen {
			continue
		}
		got, err := b.AttestKey(ctx, &types.KeyAttributes{CN: DefaultKeyCN}, make([]byte, n))
		require.Nil(t, got, "nonce of %d bytes", n)
		require.ErrorIs(t, err, ErrInvalidNonce, "nonce of %d bytes", n)
		cases++
	}

	nonce := make([]byte, tfw.AliasNonceLen)
	got, err := b.AttestKey(ctx, nil, nonce)
	require.Nil(t, got)
	require.ErrorIs(t, err, ErrInvalidKeyAttributes)
	key, err := b.GetKey(ctx, nil)
	require.Nil(t, key)
	require.ErrorIs(t, err, ErrInvalidKeyAttributes)
	cases += 2

	cn := DefaultKeyCN
	names := []string{"", " " + cn, cn + " ", cn + "\x00", cn[:len(cn)-1], cn + "x", "x" + cn[1:]}
	if up := bytes.ToUpper([]byte(cn)); string(up) != cn {
		names = append(names, string(up))
	}
	for _, name := range names {
		got, err := b.AttestKey(ctx, &types.KeyAttributes{CN: name}, nonce)
		require.Nil(t, got, "cn %q", name)
		require.ErrorIs(t, err, ErrUnknownKey, "cn %q", name)
		key, err := b.GetKey(ctx, &types.KeyAttributes{CN: name})
		require.Nil(t, key, "cn %q", name)
		require.ErrorIs(t, err, ErrUnknownKey, "cn %q", name)
		cases += 2
	}
	require.Len(t, link.requests, before, "a refused call reached the wire")
	t.Logf("%d nonce and key-name mutations refused", cases)
}

func TestEveryMutatedKeyReplyIsRefused(t *testing.T) {
	_, point := fixedKey(t)
	link := secureElementLink().answer(tfw.CmdProvPubKey, point)
	b := newTestBackend(t, link)
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	key, err := b.GetKey(ctx, attrs)
	require.NoError(t, err)
	require.NotNil(t, key)

	cases := 0
	refuse := func(label string, reply []byte) {
		t.Helper()
		link.answer(tfw.CmdProvPubKey, reply)
		key, err := b.GetKey(ctx, attrs)
		require.Nil(t, key, "%s: a key came back", label)
		requirePackageError(t, label, err)
		cases++
	}
	for n := 0; n < devicePointLen; n++ {
		refuse("cut to "+itoa(n), point[:n])
	}
	for _, tail := range [][]byte{{0x00}, {0x04}, point[:32], point} {
		refuse("extended by "+itoa(len(tail)), append(bytes.Clone(point), tail...))
	}
	refuse("sec1 prefix", append([]byte{0x04}, point...)[:devicePointLen])
	for i := range point {
		for _, mask := range []byte{0x01, 0x80} {
			bad := bytes.Clone(point)
			bad[i] ^= mask
			refuse("byte "+itoa(i)+" ^ "+itoa(int(mask)), bad)
		}
	}
	refuse("all zero", make([]byte, devicePointLen))
	refuse("all ff", bytes.Repeat([]byte{0xFF}, devicePointLen))
	// X = 0: P-256 has no point there with any Y this test can name, and
	// Y = p is out of range for a field element.
	p := elliptic.P256().Params().P
	outOfRange := make([]byte, devicePointLen)
	copy(outOfRange, point[:32])
	p.FillBytes(outOfRange[32:])
	refuse("y = p", outOfRange)
	t.Logf("%d key-reply mutations refused", cases)
}

// serveCert points the link's ATTEST_CERT pages at der and rewinds them.
func serveCert(link *fakeLink, der []byte) {
	link.answerAttestCert(der)
	link.pageCursor[tfw.CmdAttestCert] = 0
}

func TestEveryMutatedAnchoredCertificateIsRefused(t *testing.T) {
	device := newAliasDevice(t)
	nonce := bytes.Repeat([]byte{0x5C}, tfw.AliasNonceLen)
	good := device.mint(t, nonce)

	link := secureElementLink()
	serveCert(link, good)
	b := newAnchoredBackend(t, link, device)
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	got, err := b.AttestKey(ctx, attrs, nonce)
	require.NoError(t, err)
	require.True(t, got.(*Attestation).Anchored)

	cases := 0
	refuse := func(label string, der []byte) {
		t.Helper()
		serveCert(link, der)
		got, err := b.AttestKey(ctx, attrs, nonce)
		// The chain is minted per run, so a failure prints the bytes it ran.
		require.Error(t, err, "%s: attested %x", label, der)
		require.Nil(t, got, "%s: evidence came back beside %v from %x", label, err, der)
		cases++
	}
	for i := range good {
		bad := bytes.Clone(good)
		bad[i] ^= 0x01
		refuse("byte "+itoa(i)+" ^ 01", bad)
	}
	for n := 1; n < len(good); n++ {
		serveCert(link, good[:n])
		got, err := b.AttestKey(ctx, attrs, nonce)
		require.Nil(t, got, "cut to %d", n)
		requirePackageError(t, "cut to "+itoa(n), err)
		cases++
	}
	for _, tail := range [][]byte{{0x00}, {0x30, 0x00}, good[:16]} {
		serveCert(link, append(bytes.Clone(good), tail...))
		got, err := b.AttestKey(ctx, attrs, nonce)
		require.Nil(t, got, "extended by %d", len(tail))
		requirePackageError(t, "extended by "+itoa(len(tail)), err)
		cases++
	}
	// The right certificate for another challenge.
	other := bytes.Clone(nonce)
	other[len(other)-1] ^= 0x01
	refuse("another nonce", device.mint(t, other))
	t.Logf("%d anchored-certificate mutations refused", cases)
}

// The certificate a real device emitted, unanchored. Without a root nothing
// signs the alias leaf this host can check, so a byte flipped in its body is
// the verifier's to judge, not this package's; what this package parses is
// whether the bytes are one whole certificate, and every cut and every tail
// is refused as ErrMalformedCertificate.
func TestEveryCutOfTheDeviceCertificateIsMalformed(t *testing.T) {
	der, err := hex.DecodeString(vecAliasCertHex)
	require.NoError(t, err)
	nonce, err := hex.DecodeString(vecAliasCertNonceHex)
	require.NoError(t, err)

	link := secureElementLink()
	serveCert(link, der)
	b := newTestBackend(t, link)
	ctx := context.Background()
	attrs := &types.KeyAttributes{CN: DefaultKeyCN}

	_, err = b.AttestKey(ctx, attrs, nonce)
	require.NoError(t, err)

	cases := 0
	for n := 1; n < len(der); n++ {
		serveCert(link, der[:n])
		got, err := b.AttestKey(ctx, attrs, nonce)
		require.Nil(t, got, "cut to %d", n)
		require.ErrorIs(t, err, ErrMalformedCertificate, "cut to %d", n)
		cases++
	}
	for _, tail := range [][]byte{{0x00}, {0xFF}, der[:8]} {
		serveCert(link, append(bytes.Clone(der), tail...))
		got, err := b.AttestKey(ctx, attrs, nonce)
		require.Nil(t, got, "extended by %d", len(tail))
		require.ErrorIs(t, err, ErrMalformedCertificate, "extended by %d", len(tail))
		cases++
	}
	t.Logf("%d device-certificate cuts refused", cases)
}

func TestEveryMutatedPossessionSignatureIsRefused(t *testing.T) {
	link, signer, pub, digest, good := fixedPossession(t)

	sig, err := signer.Sign(nil, digest[:], crypto.SHA256)
	require.NoError(t, err)
	require.True(t, ecdsa.VerifyASN1(pub, digest[:], sig))

	cases := 0
	refuse := func(label string, raw []byte) {
		t.Helper()
		link.answer(tfw.CmdAttestCSR, raw)
		sig, err := signer.Sign(nil, digest[:], crypto.SHA256)
		require.Nil(t, sig, "%s: a signature came back", label)
		requirePackageError(t, label, err)
		cases++
	}
	for i := range good {
		for _, mask := range []byte{0x01, 0x80} {
			bad := bytes.Clone(good)
			bad[i] ^= mask
			refuse("byte "+itoa(i)+" ^ "+itoa(int(mask)), bad)
		}
	}
	for n := 0; n < len(good); n++ {
		refuse("cut to "+itoa(n), good[:n])
	}
	for _, tail := range [][]byte{{0x00}, good[:1], good} {
		refuse("extended by "+itoa(len(tail)), append(bytes.Clone(good), tail...))
	}
	// r and s swapped, and each half zero.
	half := len(good) / 2
	refuse("r s swapped", append(bytes.Clone(good[half:]), good[:half]...))
	refuse("r zero", append(make([]byte, half), good[half:]...))
	refuse("s zero", append(bytes.Clone(good[:half]), make([]byte, half)...))
	// s replaced by n - s: the other valid-looking half of a malleable pair
	// is still a signature the element did not produce over this request
	// only if the verifier says so; Go's ecdsa.Verify accepts it, so this
	// case pins what the backend returns rather than refusing it.
	n := elliptic.P256().Params().N
	s := new(big.Int).SetBytes(good[half:])
	flipped := make([]byte, len(good))
	copy(flipped, good[:half])
	new(big.Int).Sub(n, s).FillBytes(flipped[half:])
	link.answer(tfw.CmdAttestCSR, flipped)
	if sig, err := signer.Sign(nil, digest[:], crypto.SHA256); err == nil {
		require.True(t, ecdsa.VerifyASN1(pub, digest[:], sig), "n - s came back unverifiable")
	}
	t.Logf("%d possession-signature mutations refused", cases)
}

func FuzzNewDeviceKey(f *testing.F) {
	_, point := fixedKey(f)
	f.Add(point)
	f.Add(make([]byte, devicePointLen))
	f.Add(point[:63])
	f.Add(append(bytes.Clone(point), 0))
	f.Fuzz(func(t *testing.T, point []byte) {
		k, err := newDeviceKey(point, tfw.SlotDeviceID, DefaultKeyCN)
		if err != nil {
			require.Nil(t, k)
			require.ErrorIs(t, err, ErrInvalidDevicePoint)
			return
		}
		require.Len(t, point, devicePointLen)
		pub, ok := k.Public().(*ecdsa.PublicKey)
		require.True(t, ok)
		// Bytes refuses a key that is not a point on its curve.
		sec1, err := pub.Bytes()
		require.NoError(t, err, "% x", point)
		require.Equal(t, point, sec1[1:])
	})
}

func FuzzCheckAliasCertificate(f *testing.F) {
	der, err := hex.DecodeString(vecAliasCertHex)
	if err != nil {
		f.Fatal(err)
	}
	nonce, err := hex.DecodeString(vecAliasCertNonceHex)
	if err != nil {
		f.Fatal(err)
	}
	b, err := NewBackend(context.Background(), quietConfig(), newTestDevice(secureElementLink()))
	if err != nil {
		f.Fatal(err)
	}
	f.Cleanup(func() { _ = b.Close() })
	f.Add(der)
	f.Add(der[:len(der)/2])
	f.Add(append(bytes.Clone(der), 0))
	f.Add([]byte{0x30, 0x00})
	f.Fuzz(func(t *testing.T, cert []byte) {
		att, err := b.checkAliasCertificate(cert, nonce)
		if err != nil {
			require.Nil(t, att)
			return
		}
		require.Equal(t, cert, att.CertificateDER)
		require.Equal(t, nonce, att.Nonce)
		require.NotNil(t, att.Certificate)
		pub, ok := att.Certificate.PublicKey.(*ecdsa.PublicKey)
		require.True(t, ok && pub.Curve == elliptic.P256(), "attested a key that is not P-256")
		require.False(t, att.Anchored)
	})
}

func FuzzPossessionSign(f *testing.F) {
	link, signer, pub, digest, good := fixedPossession(f)
	f.Add(good)
	f.Add(good[:63])
	f.Add(make([]byte, tfw.CSRSigLen))
	f.Fuzz(func(t *testing.T, raw []byte) {
		link.answer(tfw.CmdAttestCSR, raw)
		sig, err := signer.Sign(nil, digest[:], crypto.SHA256)
		if err != nil {
			require.Nil(t, sig)
			return
		}
		require.True(t, ecdsa.VerifyASN1(pub, digest[:], sig), "% x signed as % x", raw, sig)
	})
}

func itoa(n int) string { return big.NewInt(int64(n)).String() }
