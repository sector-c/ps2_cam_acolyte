// SPDX-License-Identifier: BSD-3-Clause
package main

import (
	"crypto/sha256"
	"errors"
	"fmt"
	"math"
	"time"
)

const (
	hookSite      uint32 = 0x180260
	originalCall  uint32 = 0x0c043ecc
	primaryMatrix uint32 = 0x019a5840
	globalMatrix  uint32 = 0x00441a50
	renderMatrix  uint32 = 0x01ea9980
)

type renderSample struct {
	refs                                   [3]uint32
	code, splitCode, pause, sim, clock, vm uint32
	local, global, render                  matrix
}

func (p *pine) renderSample() (s renderSample, err error) {
	var b []byte
	for _, a := range []uint32{ctx - 8, ctx - 4, 0x019a665c, hookSite, 0x180190, 0x0043ab80, 0x0043ab84, 0x0043ab88} {
		b = readOp(b, a, 4)
	}
	for _, base := range []uint32{primaryMatrix, globalMatrix, renderMatrix} {
		for i := 0; i < 16; i++ {
			b = readOp(b, base+uint32(i*4), 4)
		}
	}
	b = append(b, 15)
	r, e := p.exchange(b, 57*4)
	if e != nil {
		return s, e
	}
	u := func(i int) uint32 { return le.Uint32(r[4*i:]) }
	s.refs = [3]uint32{u(0), u(1), u(2)}
	s.code = u(3)
	s.splitCode = u(4)
	s.pause = u(5)
	s.sim = u(6)
	s.clock = u(7)
	s.vm = u(56)
	for i := 0; i < 16; i++ {
		s.local[i] = float64(math.Float32frombits(u(8 + i)))
		s.global[i] = float64(math.Float32frombits(u(24 + i)))
		s.render[i] = float64(math.Float32frombits(u(40 + i)))
	}
	return s, nil
}
func (s renderSample) primary() bool {
	return s.refs == [3]uint32{ctx, ctx, ctx} && s.splitCode == originalCall
}
func near(a, b matrix) bool {
	for i, v := range a {
		tol := .003
		if i >= 12 && i <= 14 {
			tol = .025
		}
		if math.IsNaN(v) || math.IsInf(v, 0) || math.IsNaN(b[i]) || math.IsInf(b[i], 0) || math.Abs(v-b[i]) > tol {
			return false
		}
	}
	return true
}
func rigid(m matrix) bool {
	for _, v := range m {
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return false
		}
	}
	for i := 0; i < 3; i++ {
		if math.Abs(m[i*4+3]) > .002 || math.Abs(m[12+i]) > 10000 {
			return false
		}
		for j := 0; j < 3; j++ {
			d := 0.
			for k := 0; k < 3; k++ {
				d += m[i*4+k] * m[j*4+k]
			}
			want := 0.
			if i == j {
				want = 1
			}
			if math.Abs(d-want) > .003 {
				return false
			}
		}
	}
	det := m[0]*(m[5]*m[10]-m[6]*m[9]) - m[1]*(m[4]*m[10]-m[6]*m[8]) + m[2]*(m[4]*m[9]-m[5]*m[8])
	return math.Abs(det-1) < .004 && math.Abs(m[15]-1) < .002
}
func (s renderSample) published(m matrix) bool {
	return near(s.local, m) && near(s.global, m) && near(s.render, m)
}
func (p *pine) codeCheck(allowPatched bool) error {
	for _, v := range []struct {
		a    uint32
		n    int
		hash string
	}{
		{0x180240, 136, "084b5ffefb6790a8ac44da16dcdfc8d2e5bc07de7d7b7f2f102023fbd27dd4f2"},
		{0x180170, 128, "0551ad7fecf2293892e907210f58c6d481264a819be57018c86beda8f47bcdd1"},
		{0x10fb30, 192, "f718448ffa494fc2c04b889c831e7f4ceccc742624e13d37b136225ccc600fa2"},
		{0x10fbf0, 192, "50dbf8388e395e3316b0b11ab8c7914e3eb2b74b4a8d71ebf036938c0ee56d9a"},
	} {
		b, e := p.readSpan(v.a, v.n, 4)
		if e != nil {
			return e
		}
		if v.a == 0x180240 && allowPatched && le.Uint32(b[32:]) == 0 {
			le.PutUint32(b[32:], originalCall)
		}
		if fmt.Sprintf("%x", sha256.Sum256(b)) != v.hash {
			return fmt.Errorf("original camera code mismatch at %08X (another patch or unsupported revision)", v.a)
		}
	}
	return nil
}
func write32(b []byte, a, v uint32) []byte {
	b = append(b, 6)
	b = le.AppendUint32(b, mirror+a)
	return le.AppendUint32(b, v)
}
func (p *pine) setCall(v uint32) error {
	if v != 0 && v != originalCall {
		return errors.New("invalid hook word")
	}
	_, e := p.exchange(write32(nil, hookSite, v), 0)
	return e
}
func (p *pine) writeMatrix(m matrix) error {
	if !rigid(m) {
		return errors.New("invalid rigid matrix")
	}
	var b []byte
	for i, v := range m {
		b = write32(b, primaryMatrix+uint32(4*i), math.Float32bits(float32(v)))
	}
	_, e := p.exchange(b, 0)
	return e
}

type hookSession struct {
	p                 *pine
	active, uncertain bool
	target            pose
	desired           matrix
}

func (h *hookSession) enable() error {
	if h.active || h.uncertain {
		return errors.New("disable or recover current hook first")
	}
	if e := h.p.identity(true); e != nil {
		return e
	}
	if e := h.p.codeCheck(false); e != nil {
		return e
	}
	s, e := h.p.renderSample()
	if e != nil {
		return e
	}
	if s.vm != 0 {
		return errors.New("resume PCSX2 itself; native in-game freeze is allowed")
	}
	if !s.primary() || s.code != originalCall || !rigid(s.local) {
		return errors.New("no valid primary gameplay render matrix")
	}
	// No original track, callback, parent, cursor, or pose output is consulted.
	h.desired = s.local
	h.target = matrixPose(s.local)
	if !near(poseMatrix(h.target), s.local) {
		return errors.New("unsupported camera matrix convention")
	}
	h.uncertain = true
	if e = h.p.setCall(0); e != nil {
		return e
	}
	if e = h.p.writeMatrix(h.desired); e != nil {
		return e
	}
	if e = h.waitPublish(h.desired, s.clock); e != nil {
		r := h.disable()
		if r != nil {
			return fmt.Errorf("%v; restore failed: %v", e, r)
		}
		return e
	}
	h.active = true
	h.uncertain = false
	return nil
}
func (h *hookSession) waitPublish(m matrix, clock uint32) error {
	deadline := time.Now().Add(300 * time.Millisecond)
	for time.Now().Before(deadline) {
		s, e := h.p.renderSample()
		if e != nil {
			return e
		}
		if s.code != 0 || !s.primary() {
			return errors.New("hook or active camera changed")
		}
		if s.clock-clock >= 2 && s.clock-clock < 0x80000000 && s.published(m) {
			return nil
		}
		time.Sleep(5 * time.Millisecond)
	}
	return errors.New("matrix not published by selected render route (or PCSX2 code cache did not honor patch)")
}
func (h *hookSession) move(cmd string) error {
	if !h.active || h.uncertain {
		return errors.New("enable first")
	}
	if e := h.p.identity(false); e != nil {
		return e
	}
	s, e := h.p.renderSample()
	if e != nil {
		return e
	}
	if !s.primary() || s.code != 0 {
		return errors.New("primary render hook lost")
	}
	if s.vm != 0 {
		return errors.New("PCSX2 itself paused; resume emulator")
	}
	target := move(h.target, cmd)
	m := poseMatrix(target)
	if e = h.p.writeMatrix(m); e != nil {
		h.uncertain = true
		return e
	}
	if e = h.waitPublish(m, s.clock); e != nil {
		return e
	}
	h.target = target
	h.desired = m
	return nil
}
func (h *hookSession) disable() error {
	if !h.active && !h.uncertain {
		return nil
	}
	if e := h.p.identity(true); e != nil {
		return e
	}
	if e := h.p.codeCheck(true); e != nil {
		return e
	}
	s, e := h.p.renderSample()
	if e != nil {
		return e
	}
	if s.code == 0 {
		if e = h.p.setCall(originalCall); e != nil {
			return e
		}
	}
	s, e = h.p.renderSample()
	if e != nil {
		return e
	}
	if s.code != originalCall {
		return errors.New("original instruction not restored")
	}
	h.active = false
	h.uncertain = false
	return nil
}
func (h *hookSession) recover() error {
	if h.active || h.uncertain {
		return errors.New("disable current session instead")
	}
	if e := h.p.identity(true); e != nil {
		return e
	}
	if e := h.p.codeCheck(true); e != nil {
		return e
	}
	s, e := h.p.renderSample()
	if e != nil {
		return e
	}
	if s.code != 0 {
		return errors.New("no abandoned producer hook")
	}
	h.uncertain = true
	return h.disable()
}
