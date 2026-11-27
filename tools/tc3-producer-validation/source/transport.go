// SPDX-License-Identifier: BSD-3-Clause
package main

import (
	"encoding/binary"
	"errors"
	"fmt"
	"io"
	"math"
	"net"
	"strings"
	"time"
)

const mirror uint32 = 0x80000000
const ram uint32 = 0x02000000
const ctx uint32 = 0x019a4bf0

var le = binary.LittleEndian

type pose [6]float64

func clamp(v float64) float64 { return math.Max(-10000, math.Min(10000, v)) }
func move(p pose, cmd string) pose {
	x, y, z := p[3], p[4], p[5]
	sx, cx := math.Sincos(x)
	sy, cy := math.Sincos(y)
	sz, cz := math.Sincos(z)
	right := [3]float64{cz*cy + sz*sx*sy, sz * cx, -cz*sy + sz*sx*cy}
	front := [3]float64{-cx * sy, sx, -cx * cy}
	side, f, up := 0., 0., 0.
	switch cmd {
	case "w":
		f = .5
	case "s":
		f = -.5
	case "a":
		side = -.5
	case "d":
		side = .5
	case "q":
		up = -.5
	case "e":
		up = .5
	case "left":
		p[4] += math.Pi / 18
	case "right":
		p[4] -= math.Pi / 18
	case "up":
		p[3] += math.Pi / 18
	case "down":
		p[3] -= math.Pi / 18
	}
	for i := 0; i < 3; i++ {
		p[i] = clamp(p[i] + side*right[i] + f*front[i])
	}
	p[1] = clamp(p[1] + up)
	return p
}

type pine struct{ c net.Conn }

func (p *pine) exchange(body []byte, n int) ([]byte, error) {
	if len(body) == 0 || len(body) > 4096 {
		return nil, errors.New("invalid local request")
	}
	if e := p.c.SetDeadline(time.Now().Add(3 * time.Second)); e != nil {
		return nil, e
	}
	req := make([]byte, 4+len(body))
	le.PutUint32(req, uint32(len(req)))
	copy(req[4:], body)
	for len(req) > 0 {
		k, e := p.c.Write(req)
		if e != nil {
			return nil, e
		}
		if k == 0 {
			return nil, io.ErrUnexpectedEOF
		}
		req = req[k:]
	}
	var h [4]byte
	if _, e := io.ReadFull(p.c, h[:]); e != nil {
		return nil, e
	}
	size := le.Uint32(h[:])
	if size < 5 || size > 65536 {
		return nil, fmt.Errorf("invalid PINE response length %d", size)
	}
	r := make([]byte, size-4)
	if _, e := io.ReadFull(p.c, r); e != nil {
		return nil, e
	}
	if r[0] != 0 {
		return nil, fmt.Errorf("PINE refused command (status 0x%02x)", r[0])
	}
	r = r[1:]
	if n >= 0 && len(r) != n {
		return nil, fmt.Errorf("PINE reply length %d, expected %d", len(r), n)
	}
	return r, nil
}
func (p *pine) meta(op byte) (string, error) {
	r, e := p.exchange([]byte{op}, -1)
	if e != nil {
		return "", e
	}
	if len(r) < 5 || int(le.Uint32(r)) != len(r)-4 || r[len(r)-1] != 0 {
		return "", errors.New("invalid metadata string")
	}
	return string(r[4 : len(r)-1]), nil
}
func (p *pine) identity(full bool) error {
	crc, e := p.meta(13)
	if e != nil {
		return e
	}
	if !strings.EqualFold(crc, "7290669C") {
		return fmt.Errorf("unsupported game CRC %s (need 7290669C)", crc)
	}
	if full {
		serial, e := p.meta(12)
		if e != nil {
			return e
		}
		rev, e := p.meta(14)
		if e != nil {
			return e
		}
		if serial != "SLUS-20645" || rev != "1.00" {
			return fmt.Errorf("unsupported disc %s revision %s", serial, rev)
		}
	}
	return nil
}
func readOp(b []byte, a uint32, width int) []byte {
	op := byte(2)
	if width == 2 {
		op = 1
	}
	b = append(b, op)
	return le.AppendUint32(b, mirror+a)
}
func (p *pine) readSpan(a uint32, n int, width int) ([]byte, error) {
	if a >= ram || n <= 0 || n%width != 0 || a%uint32(width) != 0 || uint64(a)+uint64(n) > uint64(ram) {
		return nil, errors.New("invalid read span")
	}
	r := make([]byte, 0, n)
	for off := 0; off < n; {
		k := n - off
		if k > 512 {
			k = 512
		}
		var b []byte
		for j := 0; j < k; j += width {
			b = readOp(b, a+uint32(off+j), width)
		}
		x, e := p.exchange(b, k)
		if e != nil {
			return nil, e
		}
		r = append(r, x...)
		off += k
	}
	return r, nil
}
