// Package espflash writes a firmware image to an ESP32-S3 over its serial port, speaking
// the protocol of the chip's ROM bootloader as esptool does: reset into the bootloader,
// sync, check the chip, write the image compressed (zlib) in 1 KiB blocks, compare the
// flash's MD5, reset. It talks to the ROM directly (no flasher stub), which is slower than
// esptool but needs nothing besides the image.
//
// Protocol reference: https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/serial-protocol.html
package espflash

import (
	"bytes"
	"compress/zlib"
	"crypto/md5"
	"encoding/binary"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"strings"
	"time"
)

// Port is a serial port: data plus the two control lines that reset the chip.
type Port interface {
	io.ReadWriter
	SetDTR(bool) error
	SetRTS(bool) error
	SetReadTimeout(time.Duration) error
}

// Commands of the ROM loader.
const (
	cmdSync         = 0x08
	cmdReadReg      = 0x0A
	cmdSPISetParams = 0x0B
	cmdSPIAttach    = 0x0D
	cmdDeflBegin    = 0x10
	cmdDeflData     = 0x11
	cmdDeflEnd      = 0x12
	cmdFlashMD5     = 0x13
)

const (
	blockSize       = 0x400      // the ROM loader's write block
	chipMagicReg    = 0x40001000 // CHIP_DETECT_MAGIC_REG_ADDR
	esp32s3Magic    = 0x9
	checksumSeed    = 0xEF
	flashSize16MB   = 16 << 20
	defaultTimeout  = 3 * time.Second
	eraseTimePerMB  = 30 * time.Second
	writeTimePerMB  = 40 * time.Second
	md5TimePerMB    = 8 * time.Second
	syncTries       = 7
	resetTries      = 4
	readPollTimeout = 50 * time.Millisecond
)

// Options for Flash.
type Options struct {
	Offset    uint32                // where the image goes (0 for a merged image)
	FlashSize int                   // bytes; 0 = 16 MiB (both Blinky boards)
	USBJTAG   bool                  // the ESP32-S3's own USB port (VID 303a) instead of a UART bridge
	Progress  func(done, total int) // bytes of the image written so far
	Log       func(string)          // steps, for the user
}

// Flash writes image to the chip on p and resets it into the new firmware.
func Flash(p Port, image []byte, o Options) error {
	if len(image) == 0 {
		return errors.New("empty image")
	}
	if o.FlashSize == 0 {
		o.FlashSize = flashSize16MB
	}
	logf := func(format string, args ...any) {
		if o.Log != nil {
			o.Log(fmt.Sprintf(format, args...))
		}
	}
	l := &loader{p: p}
	logf("resetting into the bootloader")
	if err := l.connect(o.USBJTAG); err != nil {
		return err
	}
	magic, err := l.readReg(chipMagicReg)
	if err != nil {
		return fmt.Errorf("chip check: %w", err)
	}
	if magic != esp32s3Magic {
		return fmt.Errorf("this is not an ESP32-S3 (chip magic %#x)", magic)
	}
	logf("ESP32-S3 found")
	if _, err := l.command(cmdSPIAttach, make([]byte, 8), 0, defaultTimeout); err != nil {
		return fmt.Errorf("attach flash: %w", err)
	}
	params := le32(0, uint32(o.FlashSize), 64<<10, 4<<10, 256, 0xFFFF)
	if _, err := l.command(cmdSPISetParams, params, 0, defaultTimeout); err != nil {
		return fmt.Errorf("flash parameters: %w", err)
	}

	var z bytes.Buffer
	zw, _ := zlib.NewWriterLevel(&z, zlib.BestCompression)
	zw.Write(image)
	zw.Close()
	comp := z.Bytes()
	blocks := (len(comp) + blockSize - 1) / blockSize
	eraseSize := (len(image) + blockSize - 1) / blockSize * blockSize
	logf("erasing %d KiB at %#x", eraseSize>>10, o.Offset)
	begin := le32(uint32(eraseSize), uint32(blocks), blockSize, o.Offset, 0) // last word: not encrypted
	if _, err := l.command(cmdDeflBegin, begin, 0, perMB(eraseTimePerMB, eraseSize)); err != nil {
		return fmt.Errorf("begin: %w", err)
	}
	logf("writing %d KiB (%d KiB compressed)", len(image)>>10, len(comp)>>10)
	for seq := range blocks {
		block := comp[seq*blockSize : min((seq+1)*blockSize, len(comp))]
		data := append(le32(uint32(len(block)), uint32(seq), 0, 0), block...)
		// Each compressed block stands for up to ~4 times as much flash (a guess, as esptool's).
		if _, err := l.commandChecked(cmdDeflData, data, checksum(block), perMB(writeTimePerMB, 4*blockSize)); err != nil {
			return fmt.Errorf("block %d of %d: %w", seq+1, blocks, err)
		}
		if o.Progress != nil {
			o.Progress(min(len(image), (seq+1)*len(image)/blocks), len(image))
		}
	}
	logf("checking")
	res, err := l.command(cmdFlashMD5, le32(o.Offset, uint32(len(image)), 0, 0), 32, perMB(md5TimePerMB, len(image)))
	if err != nil {
		return fmt.Errorf("md5: %w", err)
	}
	want := md5.Sum(image)
	if got := strings.ToLower(string(res)); got != hex.EncodeToString(want[:]) {
		return fmt.Errorf("the flash doesn't match the image (md5 %s, want %x)", got, want)
	}
	// Leave the loader's flash mode without rebooting from it, then reset by the RTS line.
	if _, err := l.command(cmdDeflEnd, le32(1), 0, defaultTimeout); err != nil {
		logf("flash end: %v (the image is written)", err)
	}
	logf("starting the new firmware")
	hardReset(p, o.USBJTAG)
	return nil
}

func perMB(t time.Duration, size int) time.Duration {
	d := time.Duration(float64(t) * float64(size) / (1 << 20))
	return max(d, defaultTimeout)
}

func le32(vs ...uint32) []byte {
	b := make([]byte, 4*len(vs))
	for i, v := range vs {
		binary.LittleEndian.PutUint32(b[4*i:], v)
	}
	return b
}

func checksum(data []byte) uint32 {
	c := byte(checksumSeed)
	for _, b := range data {
		c ^= b
	}
	return uint32(c)
}

// resetIntoBootloader holds GPIO0 low (DTR) while the chip resets (RTS), as esptool does.
func resetIntoBootloader(p Port, usbJTAG bool) {
	if usbJTAG {
		// The ESP32-S3's USB-Serial-JTAG maps the lines itself; the sequence goes through
		// (1,1) rather than (0,0), and RTS is set again because Windows only passes DTR on then.
		p.SetRTS(false)
		p.SetDTR(false)
		time.Sleep(100 * time.Millisecond)
		p.SetDTR(true)
		p.SetRTS(false)
		time.Sleep(100 * time.Millisecond)
		p.SetRTS(true)
		p.SetDTR(false)
		p.SetRTS(true)
		time.Sleep(100 * time.Millisecond)
		p.SetDTR(false)
		p.SetRTS(false)
		return
	}
	// A USB-UART bridge with the usual two-transistor auto-reset circuit.
	p.SetDTR(false)
	p.SetRTS(true)
	time.Sleep(100 * time.Millisecond)
	p.SetDTR(true)
	p.SetRTS(false)
	time.Sleep(50 * time.Millisecond)
	p.SetDTR(false)
}

func hardReset(p Port, usbJTAG bool) {
	p.SetDTR(false)
	p.SetRTS(true)
	if usbJTAG {
		time.Sleep(200 * time.Millisecond)
		p.SetRTS(false)
		time.Sleep(200 * time.Millisecond)
		return
	}
	time.Sleep(100 * time.Millisecond)
	p.SetRTS(false)
}

// loader sends commands to the ROM loader and reads its responses.
type loader struct {
	p   Port
	buf []byte // bytes read but not used yet
}

// connect resets the chip into the bootloader and syncs; it tries a few times, since the
// chip may still be printing its boot messages.
func (l *loader) connect(usbJTAG bool) error {
	var last error
	for range resetTries {
		resetIntoBootloader(l.p, usbJTAG)
		l.drain(200 * time.Millisecond)
		for range syncTries {
			if last = l.sync(); last == nil {
				return nil
			}
		}
	}
	return fmt.Errorf("no answer from the bootloader (hold BOOT while plugging the board in, then try again): %w", last)
}

func (l *loader) sync() error {
	data := append([]byte{0x07, 0x07, 0x12, 0x20}, bytes.Repeat([]byte{0x55}, 32)...)
	if _, err := l.command(cmdSync, data, 0, 100*time.Millisecond); err != nil {
		return err
	}
	// The ROM answers a sync several times; drop the extra answers.
	l.drain(100 * time.Millisecond)
	return nil
}

func (l *loader) readReg(addr uint32) (uint32, error) {
	_, val, err := l.exchange(cmdReadReg, le32(addr), 0, 0, defaultTimeout)
	return val, err
}

func (l *loader) command(op byte, data []byte, dataLen int, timeout time.Duration) ([]byte, error) {
	res, _, err := l.exchange(op, data, 0, dataLen, timeout)
	return res, err
}

func (l *loader) commandChecked(op byte, data []byte, chk uint32, timeout time.Duration) ([]byte, error) {
	res, _, err := l.exchange(op, data, chk, 0, timeout)
	return res, err
}

// exchange sends one command and waits for its response. dataLen is how many bytes of
// data the response carries before its status bytes (the ROM sends 2 or 4 status bytes).
func (l *loader) exchange(op byte, data []byte, chk uint32, dataLen int, timeout time.Duration) ([]byte, uint32, error) {
	pkt := make([]byte, 8, 8+len(data))
	pkt[1] = op
	binary.LittleEndian.PutUint16(pkt[2:], uint16(len(data)))
	binary.LittleEndian.PutUint32(pkt[4:], chk)
	pkt = append(pkt, data...)
	if _, err := l.p.Write(slipEncode(pkt)); err != nil {
		return nil, 0, err
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		frame, err := l.readFrame(deadline)
		if err != nil {
			return nil, 0, err
		}
		if len(frame) < 8 || frame[0] != 0x01 || frame[1] != op {
			continue // boot messages, an answer to an earlier command
		}
		size := int(binary.LittleEndian.Uint16(frame[2:]))
		val := binary.LittleEndian.Uint32(frame[4:])
		body := frame[8:]
		if size < len(body) {
			body = body[:size]
		}
		if len(body) < dataLen+2 {
			return nil, 0, fmt.Errorf("short response to %#x", op)
		}
		if body[dataLen] != 0 {
			return nil, 0, fmt.Errorf("the bootloader reports error %#x for command %#x", body[dataLen+1], op)
		}
		return body[:dataLen], val, nil
	}
	return nil, 0, fmt.Errorf("no response to command %#x", op)
}

// readFrame returns the next SLIP frame (decoded), or an error at the deadline.
func (l *loader) readFrame(deadline time.Time) ([]byte, error) {
	l.p.SetReadTimeout(readPollTimeout)
	chunk := make([]byte, 512)
	for {
		if frame, rest, ok := slipNext(l.buf); ok {
			l.buf = rest
			return frame, nil
		}
		if time.Now().After(deadline) {
			return nil, errTimeout
		}
		n, err := l.p.Read(chunk)
		if err != nil && !isTimeout(err) {
			return nil, err
		}
		l.buf = append(l.buf, chunk[:n]...)
		if len(l.buf) > 1<<16 {
			l.buf = l.buf[len(l.buf)-1<<15:] // noise without frames
		}
	}
}

// drain reads and drops what arrives within d (boot messages, extra sync answers).
func (l *loader) drain(d time.Duration) {
	l.p.SetReadTimeout(readPollTimeout)
	chunk := make([]byte, 512)
	end := time.Now().Add(d)
	for time.Now().Before(end) {
		if _, err := l.p.Read(chunk); err != nil && !isTimeout(err) {
			break
		}
	}
	l.buf = nil
}

var errTimeout = errors.New("timeout")

// isTimeout reports a read that ended at its timeout (go.bug.st/serial returns 0 bytes
// and no error; deadline-based ports return a timeout error).
func isTimeout(err error) bool {
	var t interface{ Timeout() bool }
	return errors.As(err, &t) && t.Timeout()
}

// slipEncode frames a packet: 0xC0 at both ends, 0xC0 and 0xDB escaped.
func slipEncode(pkt []byte) []byte {
	out := make([]byte, 0, len(pkt)+8)
	out = append(out, 0xC0)
	for _, b := range pkt {
		switch b {
		case 0xC0:
			out = append(out, 0xDB, 0xDC)
		case 0xDB:
			out = append(out, 0xDB, 0xDD)
		default:
			out = append(out, b)
		}
	}
	return append(out, 0xC0)
}

// slipNext finds the first complete frame in buf and returns it decoded with the rest.
func slipNext(buf []byte) (frame, rest []byte, ok bool) {
	start := bytes.IndexByte(buf, 0xC0)
	if start < 0 {
		return nil, nil, false
	}
	for {
		end := bytes.IndexByte(buf[start+1:], 0xC0)
		if end < 0 {
			return nil, buf[start:], false
		}
		end += start + 1
		raw := buf[start+1 : end]
		if len(raw) == 0 { // two 0xC0 in a row: the second starts the frame
			start = end
			continue
		}
		out := make([]byte, 0, len(raw))
		for i := 0; i < len(raw); i++ {
			if raw[i] == 0xDB && i+1 < len(raw) {
				i++
				switch raw[i] {
				case 0xDC:
					out = append(out, 0xC0)
				case 0xDD:
					out = append(out, 0xDB)
				}
				continue
			}
			out = append(out, raw[i])
		}
		return out, buf[end+1:], true
	}
}
