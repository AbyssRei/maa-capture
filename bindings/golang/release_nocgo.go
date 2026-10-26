//go:build windows && amd64 && !cgo

package maa_capture

import "errors"

func (m *Manager) RegisterCustomUnit(key string, capture func() *CaptureFrame, inactive func()) error {
	return errors.New("custom callbacks require CGO_ENABLED=1")
}
