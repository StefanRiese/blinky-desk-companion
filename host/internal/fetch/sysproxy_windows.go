package fetch

import "golang.org/x/sys/windows/registry"

// readSystemProxy reads the proxy of the Windows Internet settings (as in the Control Panel).
var readSystemProxy = func() (sysProxy, error) {
	k, err := registry.OpenKey(registry.CURRENT_USER, `Software\Microsoft\Windows\CurrentVersion\Internet Settings`, registry.QUERY_VALUE)
	if err != nil {
		return sysProxy{}, err
	}
	defer k.Close()
	enabled, _, _ := k.GetIntegerValue("ProxyEnable")
	server, _, _ := k.GetStringValue("ProxyServer")
	override, _, _ := k.GetStringValue("ProxyOverride")
	return parseWindows(enabled == 1, server, override), nil
}
