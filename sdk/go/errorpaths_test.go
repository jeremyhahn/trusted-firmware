package tfw

import (
	"bytes"
	"context"
	"crypto"
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"encoding/asn1"
	"errors"
	"io"
	"math/big"
	"net"
	"strings"
	"testing"
	"time"

	"github.com/stretchr/testify/require"
)

// scriptedLink answers each request with the next reply in turn, so a test
// can make a later page of a paged command disagree with the first.
type scriptedLink struct {
	replies []cannedLink
	rx      bytes.Buffer
}

func (l *scriptedLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	r := l.replies[0]
	if len(l.replies) > 1 {
		l.replies = l.replies[1:]
	}
	resp, err := EncodeResponse(f.Seq, f.Cmd(), r.status, r.payload)
	if err != nil {
		return 0, err
	}
	l.rx.Write(resp)
	return len(p), nil
}

func (l *scriptedLink) Read(p []byte) (int, error) { return l.rx.Read(p) }

// noisyLink puts a frame with a broken CRC on the line ahead of every real
// reply, the way a byte lost on a serial line leaves one.
type noisyLink struct{ cannedLink }

func (l *noisyLink) Write(p []byte) (int, error) {
	f, _, err := Decode(p)
	if err != nil {
		return 0, err
	}
	bad, err := EncodeResponse(f.Seq, f.Cmd(), StatusOK, []byte{1, 2})
	if err != nil {
		return 0, err
	}
	bad[len(bad)-1] ^= 0xFF
	l.rx.Write(bad)
	return l.cannedLink.Write(p)
}

func asStatus(t *testing.T, err error) Status {
	t.Helper()
	var st Status
	require.True(t, errors.As(err, &st), "error %v carries no Status", err)
	return st
}

func requireArgRange(t *testing.T, err error, cmd Cmd, arg string) {
	t.Helper()
	var ae *ArgRangeError
	require.True(t, errors.As(err, &ae), "error %v is not an ArgRangeError", err)
	require.Equal(t, cmd, ae.Cmd)
	require.Equal(t, arg, ae.Arg)
}

func requireShortReply(t *testing.T, err error, cmd Cmd) {
	t.Helper()
	var se *ShortReplyError
	require.True(t, errors.As(err, &se), "error %v is not a ShortReplyError", err)
	require.Equal(t, cmd, se.Cmd)
}

// An unknown status must read as unrecognised, never as a known one: that is
// what an older host sees when a newer firmware adds a code.
func TestUnknownStatusReadsAsUnrecognised(t *testing.T) {
	for _, s := range []Status{0x15, 0x7F, 0xEE, 0xFF} {
		if _, known := statusNames[s]; known {
			t.Fatalf("status %d is known; pick an unassigned value", s)
		}
		require.Equal(t, "unrecognised status "+itoa(uint8(s)), s.String())
		require.Equal(t, s.String(), s.Error())
		for k, name := range statusNames {
			require.NotEqual(t, name, s.String(), "status %d reads as %v", s, k)
		}

		_, err := New(&cannedLink{status: s}).Do(context.Background(), CmdOTAStatus, nil)
		require.Equal(t, s, asStatus(t, err))
		for k := range statusNames {
			require.False(t, errors.Is(err, k) && k != s, "status %d matched known %v", s, k)
		}
	}
}

func TestUnknownEnumsReadAsUnrecognised(t *testing.T) {
	require.Equal(t, "OTAState(9)", OTAState(9).String())
	require.Equal(t, "unrecognised tier 200", CryptoTier(200).String())
	for _, tc := range []struct {
		got, want string
	}{
		{OTAIdle.String(), "idle"},
		{OTAReceiving.String(), "receiving"},
		{OTAComplete.String(), "complete"},
		{OTAFailed.String(), "failed"},
		{CryptoNone.String(), "none"},
		{CryptoSoftware.String(), "software"},
		{CryptoSecureElement.String(), "secure-element"},
		{CryptoTPM2.String(), "tpm2-discrete"},
	} {
		require.Equal(t, tc.want, tc.got)
	}
}

func itoa(v uint8) string { return big.NewInt(int64(v)).String() }

func TestDeviceResynchronisesPastACorruptFrame(t *testing.T) {
	link := &noisyLink{cannedLink{payload: []byte{byte(OTAComplete), 0x10, 0, 0, 0}}}
	st, n, err := New(link).OTAStatus(context.Background())
	require.NoError(t, err)
	require.Equal(t, OTAComplete, st)
	require.Equal(t, uint32(16), n)
}

func TestOTARefusals(t *testing.T) {
	ctx := context.Background()
	for _, n := range []int{0, OTAChunk + 1} {
		err := New(&cannedLink{}).OTAData(ctx, 0, make([]byte, n))
		requireArgRange(t, err, CmdOTAData, "length")
	}

	_, _, err := New(&cannedLink{payload: make([]byte, 4)}).OTAStatus(ctx)
	requireShortReply(t, err, CmdOTAStatus)

	_, _, err = New(&cannedLink{status: StatusErrBadState}).OTAStatus(ctx)
	require.Equal(t, StatusErrBadState, asStatus(t, err))

	for _, tc := range []struct {
		name    string
		replies []cannedLink
	}{
		{"begin refused", []cannedLink{{status: StatusErrBusy}}},
		{"data refused", []cannedLink{{}, {status: StatusErrBusy}}},
		{"end refused", []cannedLink{{}, {}, {}, {status: StatusErrBusy}}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := New(&scriptedLink{replies: tc.replies}).StageImage(ctx, make([]byte, 2*OTAChunk))
			require.Equal(t, StatusErrBusy, asStatus(t, err))
		})
	}
	require.NoError(t, New(&cannedLink{}).StageImage(ctx, make([]byte, OTAChunk+1)))
}

func TestProvisionRefusals(t *testing.T) {
	ctx := context.Background()
	require.NoError(t, New(&cannedLink{}).ProvWriteConfig(ctx, 1, make([]byte, 32)))
	require.NoError(t, New(&cannedLink{}).ProvLock(ctx, ZoneConfig, 0x1234))
	err := New(&cannedLink{status: StatusErrProtectedRegion}).ProvLock(ctx, ZoneData, 1)
	require.Equal(t, StatusErrProtectedRegion, asStatus(t, err))

	for _, tc := range []struct {
		name string
		cmd  Cmd
		call func(*Device) ([]byte, error)
	}{
		{"genkey", CmdProvGenKey, func(d *Device) ([]byte, error) { return d.ProvGenKey(ctx, SlotDeviceID) }},
		{"pubkey", CmdProvPubKey, func(d *Device) ([]byte, error) { return d.ProvPubKey(ctx, SlotDeviceID) }},
	} {
		t.Run(tc.name, func(t *testing.T) {
			_, err := tc.call(New(&cannedLink{payload: make([]byte, 63)}))
			requireShortReply(t, err, tc.cmd)
			_, err = tc.call(New(&cannedLink{status: StatusErrNotProvisioned}))
			require.Equal(t, StatusErrNotProvisioned, asStatus(t, err))
			key, err := tc.call(New(&cannedLink{payload: bytes.Repeat([]byte{7}, MaxPayload)}))
			require.NoError(t, err)
			require.Equal(t, bytes.Repeat([]byte{7}, 64), key)
		})
	}

	err = New(&cannedLink{status: StatusErrCrypto}).ProvSeedUDS(ctx, SlotUDS)
	require.Equal(t, StatusErrCrypto, asStatus(t, err))
}

func TestAttestRefusals(t *testing.T) {
	ctx := context.Background()
	_, err := New(&cannedLink{}).Attest(ctx, make([]byte, AttestNonceLen-1))
	requireArgRange(t, err, CmdAttest, "nonce")
	_, err = New(&cannedLink{}).AttestCert(ctx, make([]byte, AliasNonceLen+1))
	requireArgRange(t, err, CmdAttestCert, "nonce")

	_, err = New(&cannedLink{status: StatusErrNotProvisioned}).Attest(ctx, make([]byte, AttestNonceLen))
	require.Equal(t, StatusErrNotProvisioned, asStatus(t, err))
	_, err = New(&cannedLink{status: StatusErrNotProvisioned}).AttestCert(ctx, make([]byte, AliasNonceLen))
	require.Equal(t, StatusErrNotProvisioned, asStatus(t, err))

	// The first page is well formed; the second is refused, or empty.
	first := []byte{0, 10, 0, 1, 2, 3}
	_, err = New(&scriptedLink{replies: []cannedLink{{payload: first}, {status: StatusErrRange}}}).
		AttestCert(ctx, make([]byte, AliasNonceLen))
	require.Equal(t, StatusErrRange, asStatus(t, err))
	_, err = New(&scriptedLink{replies: []cannedLink{{payload: first}, {payload: []byte{1, 10, 0}}}}).
		AttestCert(ctx, make([]byte, AliasNonceLen))
	requireShortReply(t, err, CmdAttestCert)
	_, err = New(&cannedLink{payload: []byte{0, 0, 0}}).AttestCert(ctx, make([]byte, AliasNonceLen))
	requireShortReply(t, err, CmdAttestCert)
}

// failingSigner is an HSM that will not sign, or signs in a format the
// device cannot read.
type failingSigner struct {
	pub crypto.PublicKey
	der []byte
}

func (s failingSigner) Public() crypto.PublicKey { return s.pub }
func (s failingSigner) Sign(io.Reader, []byte, crypto.SignerOpts) ([]byte, error) {
	if s.der == nil {
		return nil, errors.New("token removed")
	}
	return s.der, nil
}

func TestAuthRefusals(t *testing.T) {
	ctx := context.Background()
	_, err := New(&cannedLink{payload: make([]byte, AuthNonceLen-1)}).AuthChallenge(ctx)
	requireShortReply(t, err, CmdAuthChallenge)
	_, err = New(&cannedLink{status: StatusErrBusy}).AuthChallenge(ctx)
	require.Equal(t, StatusErrBusy, asStatus(t, err))

	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	require.NoError(t, err)
	nonce, serial := make([]byte, AuthNonceLen), make([]byte, SerialLen)

	err = New(&cannedLink{}).AuthResponse(ctx, key, nonce[1:], serial)
	requireArgRange(t, err, CmdAuthResponse, "nonce")
	err = New(&cannedLink{}).AuthResponse(ctx, key, nonce, serial[1:])
	requireArgRange(t, err, CmdAuthResponse, "serial")

	for _, tc := range []struct {
		name, op string
		der      []byte
	}{
		{"signer fails", "sign", nil},
		{"signature not DER", "decode signature", []byte{0x30, 0x01}},
	} {
		t.Run(tc.name, func(t *testing.T) {
			err := New(&cannedLink{}).AuthResponse(ctx, failingSigner{key.Public(), tc.der}, nonce, serial)
			var se *AuthSignError
			require.True(t, errors.As(err, &se), "error %v is not an AuthSignError", err)
			require.Equal(t, tc.op, se.Op)
			require.NotNil(t, errors.Unwrap(err))
			require.Contains(t, err.Error(), tc.op)
		})
	}

	require.NoError(t, New(&cannedLink{}).AuthResponse(ctx, key, nonce, serial))
	err = New(&cannedLink{status: StatusErrAuthRequired}).AuthResponse(ctx, key, nonce, serial)
	require.Equal(t, StatusErrAuthRequired, asStatus(t, err))
}

func TestECDSADERToRawRefusals(t *testing.T) {
	der := func(r, s *big.Int) []byte {
		b, err := asn1.Marshal(ecdsaASN1Sig{R: r, S: s})
		require.NoError(t, err)
		return b
	}
	one, big257 := big.NewInt(1), new(big.Int).Lsh(big.NewInt(1), 256)
	for _, tc := range []struct {
		name string
		der  []byte
	}{
		{"not DER", []byte{0x02}},
		{"trailing data", append(der(one, one), 0)},
		{"negative r", der(big.NewInt(-1), one)},
		{"negative s", der(one, big.NewInt(-1))},
		{"r too long", der(big257, one)},
		{"s too long", der(one, big257)},
	} {
		t.Run(tc.name, func(t *testing.T) {
			_, err := ecdsaDERToRaw(tc.der)
			require.Error(t, err)
		})
	}
	raw, err := ecdsaDERToRaw(der(one, big.NewInt(2)))
	require.NoError(t, err)
	require.Len(t, raw, 64)
	require.Equal(t, byte(1), raw[31])
	require.Equal(t, byte(2), raw[63])
}

func TestWriteNetConfigRefusals(t *testing.T) {
	ctx := context.Background()
	err := New(&cannedLink{}).WriteNetConfig(ctx, NetConfig{})
	var ne *NetConfigError
	require.True(t, errors.As(err, &ne), "error %v is not a NetConfigError", err)
	require.True(t, strings.HasPrefix(err.Error(), "tfw: network block: "), err.Error())

	err = New(&cannedLink{status: StatusErrProtectedRegion}).WriteNetConfig(ctx, testNetConfig())
	require.Equal(t, StatusErrProtectedRegion, asStatus(t, err))
	require.NoError(t, New(&cannedLink{}).WriteNetConfig(ctx, testNetConfig()))
}

func TestTCPDialAndReadErrors(t *testing.T) {
	_, err := Listen("256.0.0.1:0")
	require.Error(t, err)

	l, err := Listen("127.0.0.1:0", WithTimeout(time.Second))
	require.NoError(t, err)
	defer func() { _ = l.Close() }() // the test closes it itself and checks that Close; this is only the backstop

	c, err := net.Dial("tcp", l.Addr().String())
	require.NoError(t, err)
	d, err := l.Accept()
	require.NoError(t, err)
	defer func() { _ = d.Close() }() // the assertions are on the closed peer; this side only goes away
	require.NoError(t, c.Close())

	_, err = d.Do(context.Background(), CmdOTAStatus, nil)
	var le *LinkError
	require.True(t, errors.As(err, &le), "a closed peer gave %v, not a LinkError", err)

	require.NoError(t, l.Close())
	_, err = l.Accept()
	require.Error(t, err)
}
