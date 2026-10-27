package tray

import "golang.org/x/sys/windows"

// open shows a folder in Explorer or a file in its default app, without a console window.
func open(path string) error {
	verb, _ := windows.UTF16PtrFromString("open")
	file, err := windows.UTF16PtrFromString(path)
	if err != nil {
		return err
	}
	return windows.ShellExecute(0, verb, file, nil, nil, windows.SW_SHOWNORMAL)
}
