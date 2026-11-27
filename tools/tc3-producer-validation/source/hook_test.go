package main

import (
	"math"
	"net"
	"os"
	"strings"
	"testing"
	"time"
)

func putMatrix(m *mock, a uint32, p matrix) {
	for i, x := range p {
		m.put(a+uint32(i*4), math.Float32bits(float32(x)), 4)
	}
}
func getWord(m *mock, a uint32) uint32 {
	var b [4]byte
	for i := range b {
		b[i] = m.mem[a+uint32(i)]
	}
	return le.Uint32(b[:])
}
func getMatrix(m *mock, a uint32) (p matrix) {
	for i := range p {
		p[i] = float64(math.Float32frombits(getWord(m, a+uint32(i*4))))
	}
	return
}
func setup(t *testing.T) (*mock, *hookSession, func()) {
	t.Helper()
	elf, e := os.ReadFile(os.Getenv("TC3_PRIVATE_ELF"))
	if e != nil {
		t.Skip("private verified original ELF unavailable")
	}
	m := &mock{mem: map[uint32]byte{}, crc: "7290669C"}
	for _, span := range [][2]uint32{{0x180240, 136}, {0x180170, 128}, {0x10fb30, 192}, {0x10fbf0, 192}} {
		for i := uint32(0); i < span[1]; i++ {
			m.mem[span[0]+i] = elf[span[0]-0xfff00+i]
		}
	}
	for _, a := range []uint32{ctx - 8, ctx - 4, 0x019a665c} {
		m.put(a, ctx, 4)
	}
	m.put(0x43ab80, 2, 4)
	m.put(0x43ab84, 123, 4)
	m.put(0x43ab88, 500, 4)
	p := poseMatrix(pose{25, 2, -20, 0, 2.6, 0})
	for _, a := range []uint32{primaryMatrix, globalMatrix, renderMatrix} {
		putMatrix(m, a, p)
	}
	c, s := net.Pipe()
	go m.serve(s)
	return m, &hookSession{p: &pine{c}}, func() { c.Close() }
}
func tickMock(m *mock, propagate bool) func() {
	done := make(chan struct{})
	ended := make(chan struct{})
	go func() {
		defer close(ended)
		t := time.NewTicker(5 * time.Millisecond)
		defer t.Stop()
		for {
			select {
			case <-done:
				return
			case <-t.C:
				m.mu.Lock()
				m.put(0x43ab88, getWord(m, 0x43ab88)+1, 4)
				if propagate && getWord(m, hookSite) == 0 {
					p := getMatrix(m, primaryMatrix)
					putMatrix(m, globalMatrix, p)
					putMatrix(m, renderMatrix, p)
				}
				m.mu.Unlock()
			}
		}
	}()
	return func() { close(done); <-ended }
}
func TestFrozenSimulationMovement(t *testing.T) {
	m, h, close := setup(t)
	defer close()
	stop := tickMock(m, true)
	defer stop()
	if e := h.enable(); e != nil {
		t.Fatal(e)
	}
	if e := h.move("a"); e != nil {
		t.Fatal(e)
	}
	s, e := h.p.renderSample()
	if e != nil || !s.published(h.desired) || s.sim != 123 || s.pause != 2 || s.code != 0 {
		t.Fatalf("bad published frozen pose %+v %v", s, e)
	}
	if e = h.disable(); e != nil {
		t.Fatal(e)
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if getWord(m, hookSite) != originalCall || getWord(m, 0x180190) != originalCall {
		t.Fatal("wrong restored code")
	}
	for _, w := range m.writes {
		if w[0] != hookSite && (w[0] < primaryMatrix || w[0] >= primaryMatrix+64) {
			t.Fatalf("unexpected write %x", w)
		}
	}
}
func TestUnpublishedMovement(t *testing.T) {
	m, h, close := setup(t)
	defer close()
	stop := tickMock(m, false)
	defer stop()
	if e := h.enable(); e != nil {
		t.Fatal(e)
	}
	e := h.move("a")
	if e == nil || !strings.Contains(e.Error(), "not published") {
		t.Fatalf("unexpected %v", e)
	}
	if e = h.disable(); e != nil {
		t.Fatal(e)
	}
}
func TestAbandonedHookRecovery(t *testing.T) {
	m, h, close := setup(t)
	defer close()
	m.mu.Lock()
	m.put(hookSite, 0, 4)
	m.mu.Unlock()
	if e := h.enable(); e == nil {
		t.Fatal("admitted abandoned hook")
	}
	if e := h.recover(); e != nil {
		t.Fatal(e)
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if getWord(m, hookSite) != originalCall {
		t.Fatal("not restored")
	}
}
func TestUnknownPatchRefusal(t *testing.T) {
	m, h, close := setup(t)
	defer close()
	m.mu.Lock()
	m.put(hookSite, 0x12345678, 4)
	m.mu.Unlock()
	if e := h.enable(); e == nil {
		t.Fatal("admitted unknown patch")
	}
	if e := h.recover(); e == nil {
		t.Fatal("recovered unknown patch")
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	if len(m.writes) != 0 {
		t.Fatal("wrote on refusal")
	}
}
func TestPoseRoundTrip(t *testing.T) {
	for _, p := range []pose{{25, 2, -20, 0, 2.6, 0}, {-78, 1.2, 42, -.065, 1.14, .04}, {1000, 2, -20, .5, -2, 1}} {
		m := poseMatrix(p)
		if !rigid(m) || !near(poseMatrix(matrixPose(m)), m) {
			t.Fatalf("round trip %v", p)
		}
	}
}
