package tfw

import (
	"context"
	"errors"
	"fmt"
)

// ErrUploadCount is OTA_STATUS answering, after a failed OTA_DATA, a count
// that is neither the start nor the end of the chunk in flight. Stop-and-wait
// leaves the device at one or the other; any other count is a session this
// upload did not drive, and resuming from it would stage a different image.
var ErrUploadCount = errors.New("tfw: OTA_STATUS count is not where the upload stands")

// UploadOption tunes Upload.
type UploadOption func(*uploadOptions)

type uploadOptions struct {
	resume   bool
	progress func(sent, total uint32)
}

// UploadProgress has Upload call fn after each chunk with the bytes the
// device holds and the stream's length.
func UploadProgress(fn func(sent, total uint32)) UploadOption {
	return func(o *uploadOptions) { o.progress = fn }
}

// Upload stages img.Stream (ADR section 13): OTA_BEGIN, stop-and-wait
// OTA_DATA in OTAChunk pieces, OTA_END. When an OTA_DATA fails, Upload asks
// OTA_STATUS where the session stands and, while the device is still
// receiving, goes on from its count, so no accepted byte is sent twice. It
// gives up with the OTA_DATA's error when the session is no longer
// receiving, when OTA_STATUS fails, or when the same chunk fails twice and
// OTA_STATUS after the second failure still shows no progress; a count the upload did not send is ErrUploadCount. The
// context is checked between chunks.
func (d *Device) Upload(ctx context.Context, img *Image, opts ...UploadOption) error {
	o := uploadOptions{resume: true}
	for _, opt := range opts {
		opt(&o)
	}
	return d.stage(ctx, img.Stream, o)
}

// stage is the one session loop StageImage and Upload share; the frames are
// OTABegin's, OTAData's and OTAEnd's.
func (d *Device) stage(ctx context.Context, image []byte, o uploadOptions) error {
	// The device refuses a zero total with StatusErrRange (OTA_BEGIN must
	// exceed the trailer, docs/ota/api.md); answer it here and send nothing.
	if len(image) == 0 {
		return StatusErrRange
	}
	total, err := otaTotal(int64(len(image)))
	if err != nil {
		return err
	}
	if err := d.OTABegin(ctx, total); err != nil {
		return err
	}
	retried := false
	for off := 0; off < len(image); {
		if err := ctx.Err(); err != nil {
			return err
		}
		end := min(off+OTAChunk, len(image))
		if err := d.OTAData(ctx, uint32(off), image[off:end]); err != nil {
			if !o.resume || ctx.Err() != nil {
				return err
			}
			cause := err
			next, err := d.resume(ctx, off, end, cause)
			if err != nil {
				return err
			}
			// A retry that failed may still have reached the device, so only
			// a second failure the count shows no progress after stops here.
			if next == off && retried {
				return cause
			}
			retried = next == off
			off = next
		} else {
			retried = false
			off = end
		}
		if o.progress != nil {
			o.progress(uint32(off), total)
		}
	}
	return d.OTAEnd(ctx)
}

// resume reads OTA_STATUS after the OTA_DATA for [off, end) failed with
// cause, and says where the session goes on.
func (d *Device) resume(ctx context.Context, off, end int, cause error) (int, error) {
	state, got, err := d.OTAStatus(ctx)
	if err != nil {
		return 0, errors.Join(cause, err)
	}
	if state != OTAReceiving {
		return 0, cause
	}
	switch int64(got) {
	case int64(off):
		return off, nil
	case int64(end):
		return end, nil
	}
	return 0, fmt.Errorf("%w: device holds %d bytes, the chunk in flight was [%d, %d)",
		ErrUploadCount, got, off, end)
}
