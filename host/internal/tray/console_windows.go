package tray

import (
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	kernel32              = windows.NewLazySystemDLL("kernel32.dll")
	getConsoleProcessList = kernel32.NewProc("GetConsoleProcessList")
	getConsoleWindow      = kernel32.NewProc("GetConsoleWindow")
	showWindow            = windows.NewLazySystemDLL("user32.dll").NewProc("ShowWindow")
)

// HideOwnConsole hides the console window if buddyd got its own one, i.e. it was started by
// a double-click in Explorer and not from a terminal (where the log should stay visible).
// It reports whether it did.
func HideOwnConsole() bool {
	var pids [2]uint32
	n, _, _ := getConsoleProcessList.Call(uintptr(unsafe.Pointer(&pids[0])), 2)
	if n != 1 { // shared with a shell (or no console at all)
		return false
	}
	hwnd, _, _ := getConsoleWindow.Call()
	if hwnd == 0 {
		return false
	}
	const swHide = 0
	showWindow.Call(hwnd, swHide)
	return true
}
