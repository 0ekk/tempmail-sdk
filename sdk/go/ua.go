package tempemail

import (
	"math/rand"
	"sync"

	"github.com/bogdanfinn/tls-client/profiles"
)

/**
 * 浏览器配置池
 * 每个配置包含 TLS 指纹 profile 与匹配的 User-Agent
 * 确保 TLS 握手特征与 HTTP 层 UA 一致，避免被检测
 */

/**
 * BrowserConfig TLS 指纹 profile 与 User-Agent 的配对
 * @field Profile - tls-client 浏览器指纹配置
 * @field UA      - 与指纹匹配的 User-Agent 字符串
 */
type BrowserConfig struct {
	Profile profiles.ClientProfile
	UA      string
}

/* 浏览器配置池：profile + UA 配对，覆盖多平台多浏览器 */
var browserConfigs = []BrowserConfig{
	/* Chrome - Windows */
	{profiles.Chrome_131, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"},
	{profiles.Chrome_133, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36"},
	{profiles.Chrome_144, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36"},
	{profiles.Chrome_146, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/146.0.0.0 Safari/537.36"},

	/* Chrome - macOS */
	{profiles.Chrome_131, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"},
	{profiles.Chrome_133, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36"},
	{profiles.Chrome_144, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36"},

	/* Chrome - Linux */
	{profiles.Chrome_131, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36"},
	{profiles.Chrome_133, "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36"},

	/* Edge - Windows（使用 Chrome 指纹，Edge 基于 Chromium） */
	{profiles.Chrome_131, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0"},
	{profiles.Chrome_133, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36 Edg/133.0.0.0"},
	{profiles.Chrome_144, "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/144.0.0.0 Safari/537.36 Edg/144.0.0.0"},

	/* Edge - macOS */
	{profiles.Chrome_133, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/133.0.0.0 Safari/537.36 Edg/133.0.0.0"},

	/* Firefox - Windows */
	{profiles.Firefox_135, "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:135.0) Gecko/20100101 Firefox/135.0"},
	{profiles.Firefox_147, "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:147.0) Gecko/20100101 Firefox/147.0"},

	/* Firefox - macOS */
	{profiles.Firefox_135, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:135.0) Gecko/20100101 Firefox/135.0"},
	{profiles.Firefox_147, "Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:147.0) Gecko/20100101 Firefox/147.0"},

	/* Firefox - Linux */
	{profiles.Firefox_135, "Mozilla/5.0 (X11; Linux x86_64; rv:135.0) Gecko/20100101 Firefox/135.0"},

	/* Safari - iOS */
	{profiles.Safari_IOS_18_0, "Mozilla/5.0 (iPhone; CPU iPhone OS 18_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Mobile/15E148 Safari/604.1"},
	{profiles.Safari_IOS_18_5, "Mozilla/5.0 (iPhone; CPU iPhone OS 18_5 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.5 Mobile/15E148 Safari/604.1"},
}

var (
	currentBrowser *BrowserConfig
	browserMu      sync.RWMutex
)

/**
 * RandomBrowserConfig 从配置池中随机选取一个浏览器配置
 * @returns BrowserConfig 随机选取的 profile+UA 配对
 */
func RandomBrowserConfig() BrowserConfig {
	return browserConfigs[rand.Intn(len(browserConfigs))]
}

/**
 * GetCurrentBrowser 获取当前生效的浏览器配置
 * 如果尚未初始化，返回随机配置（不缓存）
 */
func GetCurrentBrowser() BrowserConfig {
	browserMu.RLock()
	defer browserMu.RUnlock()
	if currentBrowser != nil {
		return *currentBrowser
	}
	return RandomBrowserConfig()
}

/**
 * setCurrentBrowser 设置当前生效的浏览器配置（SDK 内部使用）
 */
func setCurrentBrowser(bc BrowserConfig) {
	browserMu.Lock()
	defer browserMu.Unlock()
	currentBrowser = &bc
}

/**
 * GetCurrentUA 获取当前 TLS 客户端对应的 User-Agent
 * 返回与当前 TLS 指纹匹配的 UA 字符串，确保 TLS 层和 HTTP 层指纹一致
 */
func GetCurrentUA() string {
	return GetCurrentBrowser().UA
}
