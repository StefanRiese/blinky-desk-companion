package desktop

import "golang.org/x/sys/windows"

// Open shows a folder in Explorer, a file in its default app or a URL in the browser,
// without a console window.
func Open(path string) error {
	verb, _ := windows.UTF16PtrFromString("open")
	file, err := windows.UTF16PtrFromString(path)
	if err != nil {
		return err
	}
	return windows.ShellExecute(0, verb, file, nil, nil, windows.SW_SHOWNORMAL)
}
