package tempemail

import (
	"bytes"
	"encoding/json"
	"net/http"
	"runtime"
	"sync"
	"time"
)

const (
	telemetryMaxBatch   = 32
	telemetryFlushEvery = 2 * time.Second
)

type telemetryEvent struct {
	Operation     string `json:"operation"`
	Channel       string `json:"channel"`
	Success       bool   `json:"success"`
	AttemptCount  int    `json:"attempt_count"`
	ChannelsTried int    `json:"channels_tried,omitempty"`
	Error         string `json:"error,omitempty"`
	TsMs          int64  `json:"ts_ms"`
}

type telemetryBatchEnvelope struct {
	SchemaVersion int              `json:"schema_version"`
	SDKLanguage   string           `json:"sdk_language"`
	SDKVersion    string           `json:"sdk_version"`
	OS            string           `json:"os"`
	Arch          string           `json:"arch"`
	Events        []telemetryEvent `json:"events"`
}

var (
	telemetryQueueMu sync.Mutex
	telemetryQueue   []telemetryEvent
	telemetryFlushMu sync.Mutex /* 避免并发 POST 同一批 */
)

func init() {
	go telemetryPeriodicFlush()
}

func telemetryPeriodicFlush() {
	t := time.NewTicker(telemetryFlushEvery)
	for range t.C {
		flushTelemetryQueue()
	}
}

func flushTelemetryQueue() {
	cfg := GetConfig()
	if !telemetryOn(cfg) {
		telemetryQueueMu.Lock()
		telemetryQueue = nil
		telemetryQueueMu.Unlock()
		return
	}

	telemetryFlushMu.Lock()
	defer telemetryFlushMu.Unlock()

	telemetryQueueMu.Lock()
	if len(telemetryQueue) == 0 {
		telemetryQueueMu.Unlock()
		return
	}
	events := telemetryQueue
	telemetryQueue = nil
	telemetryQueueMu.Unlock()

	url := telemetryURLResolved(cfg)
	if url == "" {
		return
	}

	ver := SDKVersion()
	env := telemetryBatchEnvelope{
		SchemaVersion: 2,
		SDKLanguage:   "go",
		SDKVersion:    ver,
		OS:            runtime.GOOS,
		Arch:          runtime.GOARCH,
		Events:        events,
	}
	body, err := json.Marshal(env)
	if err != nil {
		return
	}

	go func(postURL string, jsonBody []byte, uaVer string) {
		req, err := http.NewRequest(http.MethodPost, postURL, bytes.NewReader(jsonBody))
		if err != nil {
			return
		}
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("User-Agent", "tempmail-sdk-go/"+uaVer)
		resp, err := telemetryHTTP.Do(req)
		if resp != nil {
			_ = resp.Body.Close()
		}
		_ = err
	}(url, body, ver)
}

func enqueueTelemetryEvent(operation, channel string, success bool, attemptCount, channelsTried int, errMsg string) {
	cfg := GetConfig()
	if !telemetryOn(cfg) {
		return
	}

	ev := telemetryEvent{
		Operation:     operation,
		Channel:       channel,
		Success:       success,
		AttemptCount:  attemptCount,
		ChannelsTried: channelsTried,
		Error:         sanitizeTelemetryError(errMsg),
		TsMs:          time.Now().UnixMilli(),
	}

	telemetryQueueMu.Lock()
	telemetryQueue = append(telemetryQueue, ev)
	n := len(telemetryQueue)
	telemetryQueueMu.Unlock()

	if n >= telemetryMaxBatch {
		flushTelemetryQueue()
	}
}
