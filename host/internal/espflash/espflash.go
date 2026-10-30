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
	"sort"
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
	cmdWriteReg     = 0x09
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

// RTC watchdogs of the ESP32-S3 (as esptool's ESP32-S3 target). After a reset through the
// USB-Serial-JTAG the RTC watchdog keeps running in the bootloader and resets the chip in
// the middle of a write, so it is turned off and the super watchdog fed by itself.
const (
	rtcWDTConfig0  = 0x60008098
	rtcWDTProtect  = 0x600080B0
	rtcWDTKey      = 0x50D83AA1
	rtcSWDConf     = 0x600080B4
	rtcSWDProtect  = 0x600080B8
	rtcSWDKey      = 0x8F1D312A
	rtcSWDAutoFeed = 1 << 31
)

// Range is an area of the flash: [Start, End).
type Range struct{ Start, End uint32 }

// Options for Flash.
type Options struct {
	Offset    uint32                // where the image goes (0 for a merged image)
	Skip      []Range               // areas to leave as they are (the settings, see SettingsRanges)
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
	if o.USBJTAG {
		if err := l.disableWatchdogs(); err != nil {
			return fmt.Errorf("watchdog: %w", err)
		}
	}
	if _, err := l.command(cmdSPIAttach, make([]byte, 8), 0, defaultTimeout); err != nil {
		return fmt.Errorf("attach flash: %w", err)
	}
	params := le32(0, uint32(o.FlashSize), 64<<10, 4<<10, 256, 0xFFFF)
	if _, err := l.command(cmdSPISetParams, params, 0, defaultTimeout); err != nil {
		return fmt.Errorf("flash parameters: %w", err)
	}

	parts := split(o.Offset, len(image), o.Skip)
	total := 0
	for _, r := range parts {
		total += int(r.End - r.Start)
	}
	if total < len(image) {
		logf("leaving %d KiB as they are (the settings)", (len(image)-total)>>10)
	}
	written := 0
	for _, r := range parts {
		data := image[r.Start-o.Offset : r.End-o.Offset]
		err := l.write(data, r.Start, logf, func(done int) {
			if o.Progress != nil {
				o.Progress(written+done, total)
			}
		})
		if err != nil {
			return err
		}
		written += len(data)
	}
	// Leave the loader's flash mode without rebooting from it, then reset by the RTS line.
	if _, err := l.command(cmdDeflEnd, le32(1), 0, defaultTimeout); err != nil {
		logf("flash end: %v (the image is written)", err)
	}
	logf("starting the new firmware")
	hardReset(p, o.USBJTAG)
	return nil
}

// write writes data compressed at offset and checks the flash's MD5. progress gets the
// bytes of data written so far.
func (l *loader) write(data []byte, offset uint32, logf func(string, ...any), progress func(int)) error {
	var z bytes.Buffer
	zw, _ := zlib.NewWriterLevel(&z, zlib.BestCompression) // the level is valid
	if _, err := zw.Write(data); err != nil {
		return fmt.Errorf("compress: %w", err)
	}
	if err := zw.Close(); err != nil {
		return fmt.Errorf("compress: %w", err)
	}
	comp := z.Bytes()
	blocks := (len(comp) + blockSize - 1) / blockSize
	eraseSize := (len(data) + blockSize - 1) / blockSize * blockSize
	logf("erasing %d KiB at %#x", eraseSize>>10, offset)
	begin := le32(uint32(eraseSize), uint32(blocks), blockSize, offset, 0) // last word: not encrypted
	if _, err := l.command(cmdDeflBegin, begin, 0, perMB(eraseTimePerMB, eraseSize)); err != nil {
		return fmt.Errorf("begin: %w", err)
	}
	logf("writing %d KiB (%d KiB compressed)", len(data)>>10, len(comp)>>10)
	for seq := range blocks {
		block := comp[seq*blockSize : min((seq+1)*blockSize, len(comp))]
		pkt := append(le32(uint32(len(block)), uint32(seq), 0, 0), block...)
		// Each compressed block stands for up to ~4 times as much flash (a guess, as esptool's).
		if _, err := l.commandChecked(cmdDeflData, pkt, checksum(block), perMB(writeTimePerMB, 4*blockSize)); err != nil {
			return fmt.Errorf("block %d of %d: %w", seq+1, blocks, err)
		}
		progress(min(len(data), (seq+1)*len(data)/blocks))
	}
	logf("checking")
	res, err := l.command(cmdFlashMD5, le32(offset, uint32(len(data)), 0, 0), 32, perMB(md5TimePerMB, len(data)))
	if err != nil {
		return fmt.Errorf("md5: %w", err)
	}
	want := md5.Sum(data)
	if got := strings.ToLower(string(res)); got != hex.EncodeToString(want[:]) {
		return fmt.Errorf("the flash doesn't match the image at %#x (md5 %s, want %x)", offset, got, want)
	}
	return nil
}

// split returns the parts of [offset, offset+size) outside the skip ranges, in order.
func split(offset uint32, size int, skip []Range) []Range {
	parts := []Range{{offset, offset + uint32(size)}}
	for _, s := range skip {
		var next []Range
		for _, p := range parts {
			if s.End <= p.Start || s.Start >= p.End {
				next = append(next, p)
				continue
			}
			if s.Start > p.Start {
				next = append(next, Range{p.Start, s.Start})
			}
			if s.End < p.End {
				next = append(next, Range{s.End, p.End})
			}
		}
		parts = next
	}
	sort.Slice(parts, func(i, j int) bool { return parts[i].Start < parts[j].Start })
	return parts
}

// Partition table of a merged image (ESP-IDF's binary format: 32-byte entries).
const (
	partTableOffset = 0x8000
	partEntryMagic  = 0x50AA
	partTypeData    = 0x01
	partSubNVS      = 0x02
	partSubNVSKeys  = 0x04
)

// SettingsRanges returns where the settings live (the nvs and nvs_keys partitions) as
// read from the partition table in a merged image that starts at offset 0. Flashing with
// these as Options.Skip keeps the settings; nil if the image has no partition table.
func SettingsRanges(image []byte) []Range {
	var out []Range
	for off := partTableOffset; off+32 <= len(image) && off < partTableOffset+0x1000; off += 32 {
		e := image[off : off+32]
		if binary.LittleEndian.Uint16(e) != partEntryMagic {
			break // the end of the table (0xFF) or its MD5 entry
		}
		typ, sub := e[2], e[3]
		start, size := binary.LittleEndian.Uint32(e[4:]), binary.LittleEndian.Uint32(e[8:])
		if typ == partTypeData && (sub == partSubNVS || sub == partSubNVSKeys) {
			out = append(out, Range{start, start + size})
		}
	}
	return out
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

func (l *loader) writeReg(addr, val uint32) error {
	_, err := l.command(cmdWriteReg, le32(addr, val, 0xFFFFFFFF, 0), 0, defaultTimeout)
	return err
}

// disableWatchdogs turns off the RTC watchdog and lets the super watchdog feed itself.
func (l *loader) disableWatchdogs() error {
	swd, err := l.readReg(rtcSWDConf)
	if err != nil {
		return err
	}
	for _, w := range [][2]uint32{
		{rtcWDTProtect, rtcWDTKey}, {rtcWDTConfig0, 0}, {rtcWDTProtect, 0},
		{rtcSWDProtect, rtcSWDKey}, {rtcSWDConf, swd | rtcSWDAutoFeed}, {rtcSWDProtect, 0},
	} {
		if err := l.writeReg(w[0], w[1]); err != nil {
			return err
		}
	}
	return nil
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
	if err := l.p.SetReadTimeout(readPollTimeout); err != nil {
		return nil, err
	}
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
	if l.p.SetReadTimeout(readPollTimeout) != nil {
		return // the next read reports the problem
	}
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
