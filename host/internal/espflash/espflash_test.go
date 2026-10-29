package espflash

import (
	"bytes"
	"compress/zlib"
	"crypto/md5"
	"encoding/binary"
	"encoding/hex"
	"io"
	"math/rand"
	"sync"
	"testing"
	"time"
)

// fakeROM answers like the ESP32-S3's ROM loader, after the reset into the bootloader.
type fakeROM struct {
	t         *testing.T
	statusLen int // 2 or 4 status bytes
	mu        sync.Mutex
	out       []byte // bytes for the host to read
	in        []byte
	inBoot    bool
	rts, dtr  bool
	magic     uint32
	flash     []byte
	comp      []byte
	nextSeq   uint32
	offset    uint32
	resets    int
	failBlock int // answer this data block with an error (-1 = none)
	ended     bool
	regs      map[uint32]uint32 // written with WRITE_REG
}

func newROM(t *testing.T) *fakeROM {
	return &fakeROM{t: t, statusLen: 4, magic: esp32s3Magic, flash: make([]byte, 4<<20), failBlock: -1}
}

func (f *fakeROM) SetDTR(on bool) error { f.mu.Lock(); f.dtr = on; f.mu.Unlock(); return nil }
func (f *fakeROM) SetRTS(on bool) error {
	f.mu.Lock()
	defer f.mu.Unlock()
	if f.rts && !on { // the chip leaves reset; GPIO0 low (DTR) means the bootloader
		f.resets++
		f.inBoot = true
		f.out = append(f.out, []byte("ESP-ROM:esp32s3-20210327\r\nwaiting for download\r\n")...)
	}
	f.rts = on
	return nil
}
func (f *fakeROM) SetReadTimeout(time.Duration) error { return nil }

func (f *fakeROM) Read(p []byte) (int, error) {
	f.mu.Lock()
	n := copy(p, f.out)
	f.out = f.out[n:]
	f.mu.Unlock()
	if n == 0 {
		time.Sleep(time.Millisecond) // like a serial read timeout: no data, no error
	}
	return n, nil
}

func (f *fakeROM) Write(p []byte) (int, error) {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.in = append(f.in, p...)
	for {
		frame, rest, ok := slipNext(f.in)
		if !ok {
			break
		}
		f.in = rest
		if f.inBoot {
			f.handle(frame)
		}
	}
	return len(p), nil
}

func (f *fakeROM) reply(op byte, val uint32, data []byte, status byte) {
	body := append(append([]byte{}, data...), status, 0x05)
	if f.statusLen == 4 {
		body = append(body, 0, 0)
	}
	pkt := make([]byte, 8, 8+len(body))
	pkt[0], pkt[1] = 0x01, op
	binary.LittleEndian.PutUint16(pkt[2:], uint16(len(body)))
	binary.LittleEndian.PutUint32(pkt[4:], val)
	f.out = append(f.out, slipEncode(append(pkt, body...))...)
}

func (f *fakeROM) handle(pkt []byte) {
	op := pkt[1]
	chk := binary.LittleEndian.Uint32(pkt[4:])
	data := pkt[8:]
	u32 := func(i int) uint32 { return binary.LittleEndian.Uint32(data[4*i:]) }
	switch op {
	case cmdSync:
		for range 3 {
			f.reply(op, 0, nil, 0)
		}
	case cmdReadReg:
		if v, ok := f.regs[u32(0)]; ok {
			f.reply(op, v, nil, 0)
		} else {
			f.reply(op, f.magic, nil, 0)
		}
	case cmdWriteReg:
		if f.regs == nil {
			f.regs = map[uint32]uint32{}
		}
		f.regs[u32(0)] = u32(1)
		f.reply(op, 0, nil, 0)
	case cmdSPIAttach, cmdSPISetParams:
		f.reply(op, 0, nil, 0)
	case cmdDeflBegin:
		if len(data) != 20 {
			f.t.Errorf("DEFL_BEGIN needs 5 words on the ESP32-S3 ROM, got %d bytes", len(data))
		}
		f.offset, f.comp, f.nextSeq = u32(3), nil, 0
		f.reply(op, 0, nil, 0)
	case cmdDeflData:
		size, seq := u32(0), u32(1)
		block := data[16:]
		if int(size) != len(block) || seq != f.nextSeq || checksum(block) != chk {
			f.reply(op, 0, nil, 1)
			return
		}
		if int(seq) == f.failBlock {
			f.reply(op, 0, nil, 1)
			return
		}
		f.nextSeq++
		f.comp = append(f.comp, block...)
		f.reply(op, 0, nil, 0)
	case cmdFlashMD5:
		zr, err := zlib.NewReader(bytes.NewReader(f.comp))
		if err != nil {
			f.reply(op, 0, nil, 1)
			return
		}
		img, _ := io.ReadAll(zr)
		copy(f.flash[f.offset:], img)
		sum := md5.Sum(f.flash[u32(0) : u32(0)+u32(1)])
		f.reply(op, 0, []byte(hex.EncodeToString(sum[:])), 0)
	case cmdDeflEnd:
		f.ended = true
		f.reply(op, 0, nil, 0)
	default:
		f.reply(op, 0, nil, 1)
	}
}

func testImage(n int) []byte {
	img := make([]byte, n)
	r := rand.New(rand.NewSource(1))
	for i := range img {
		if i%7 == 0 {
			img[i] = byte(r.Intn(256)) // partly random: compresses a little, like firmware
		}
	}
	img[0] = 0xC0 // SLIP bytes in the data must be escaped
	img[1] = 0xDB
	return img
}

func TestFlash(t *testing.T) {
	for _, statusLen := range []int{2, 4} {
		rom := newROM(t)
		rom.statusLen = statusLen
		img := testImage(300 << 10)
		var last int
		err := Flash(rom, img, Options{Offset: 0x1000, USBJTAG: true, Progress: func(done, total int) {
			if done < last || total != len(img) {
				t.Errorf("progress %d/%d after %d", done, total, last)
			}
			last = done
		}})
		if err != nil {
			t.Fatalf("status %d bytes: %v", statusLen, err)
		}
		if !bytes.Equal(rom.flash[0x1000:0x1000+len(img)], img) {
			t.Fatal("flash content differs from the image")
		}
		if last != len(img) || !rom.ended || rom.rts {
			t.Errorf("progress %d, ended %v, rts %v", last, rom.ended, rom.rts)
		}
		// Over USB-Serial-JTAG the RTC watchdog is off and the super watchdog feeds itself.
		if v, ok := rom.regs[rtcWDTConfig0]; !ok || v != 0 || rom.regs[rtcSWDConf]&rtcSWDAutoFeed == 0 || rom.regs[rtcWDTProtect] != 0 {
			t.Errorf("watchdog registers %x", rom.regs)
		}
	}
}

func TestFlashErrors(t *testing.T) {
	rom := newROM(t)
	rom.magic = 0x00F01D83 // an ESP32
	if err := Flash(rom, testImage(4096), Options{USBJTAG: true}); err == nil {
		t.Error("a different chip must be an error")
	}
	rom = newROM(t)
	rom.failBlock = 2
	if err := Flash(rom, testImage(64<<10), Options{}); err == nil {
		t.Error("a failed block must be an error")
	}
}

func TestSLIP(t *testing.T) {
	pkt := []byte{0x00, 0xC0, 0xDB, 0x01}
	buf := append([]byte("boot noise\r\n"), slipEncode(pkt)...)
	buf = append(buf, slipEncode([]byte{0x02})...)
	frame, rest, ok := slipNext(buf)
	if !ok || !bytes.Equal(frame, pkt) {
		t.Fatalf("frame %x %v", frame, ok)
	}
	if frame, _, ok = slipNext(rest); !ok || !bytes.Equal(frame, []byte{0x02}) {
		t.Fatalf("second frame %x %v", frame, ok)
	}
}
