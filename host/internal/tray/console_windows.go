package tray

import (
	"os"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	kernel32              = windows.NewLazySystemDLL("kernel32.dll")
	attachConsole         = kernel32.NewProc("AttachConsole")
	getConsoleProcessList = kernel32.NewProc("GetConsoleProcessList")
	getConsoleWindow      = kernel32.NewProc("GetConsoleWindow")
	showWindow            = windows.NewLazySystemDLL("user32.dll").NewProc("ShowWindow")
)

// AttachParentConsole lets a buddyd built as a GUI program (-H=windowsgui, so it never opens
// a console window) print to the terminal it was started from: version, config check, help
// and the log. Call it first thing in main. Output redirected to a file or pipe stays there.
func AttachParentConsole() {
	const attachParentProcess = ^uint32(0) // ATTACH_PARENT_PROCESS
	if r, _, _ := attachConsole.Call(uintptr(attachParentProcess)); r == 0 {
		return // no parent console (Explorer) or we already have one (console build)
	}
	out, err := os.OpenFile("CONOUT$", os.O_WRONLY, 0)
	if err != nil {
		return
	}
	if h, _ := windows.GetStdHandle(windows.STD_OUTPUT_HANDLE); h == 0 {
		os.Stdout = out
	}
	if h, _ := windows.GetStdHandle(windows.STD_ERROR_HANDLE); h == 0 {
		os.Stderr = out
	}
}

// Detached reports whether buddyd runs without a terminal, so the log needs a file: the GUI
// build started from Explorer or autostart (no console at all). A console build started by a
// double-click gets its own console window, which is hidden.
func Detached() bool {
	hwnd, _, _ := getConsoleWindow.Call()
	if hwnd == 0 {
		return true
	}
	var pids [2]uint32
	n, _, _ := getConsoleProcessList.Call(uintptr(unsafe.Pointer(&pids[0])), 2)
	if n != 1 { // shared with a shell
		return false
	}
	const swHide = 0
	showWindow.Call(hwnd, swHide)
	return true
}
