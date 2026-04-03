package tempemail

import (
	"runtime/debug"
	"strings"
	"sync"
)

var (
	cachedSDKVersion     string
	cachedSDKVersionOnce sync.Once
)

/*
 * SDKVersion 返回当前模块版本（来自 go build 注入的 build info，随 go.mod / tag 变化）
 * 本地开发常见为 (devel) 或 vcs.revision 短哈希
 */
func SDKVersion() string {
	cachedSDKVersionOnce.Do(func() {
		cachedSDKVersion = "0.0.0"
		bi, ok := debug.ReadBuildInfo()
		if !ok {
			return
		}
		v := bi.Main.Version
		if v != "" && v != "(devel)" {
			cachedSDKVersion = strings.TrimPrefix(v, "v")
			return
		}
		for _, s := range bi.Settings {
			if s.Key == "vcs.revision" && len(s.Value) >= 7 {
				cachedSDKVersion = s.Value[:7]
				return
			}
		}
		cachedSDKVersion = "devel"
	})
	return cachedSDKVersion
}
