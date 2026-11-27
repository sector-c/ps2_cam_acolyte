package main

import (
	"io"
	"net"
	"sync"
)

type mock struct {
	mu     sync.Mutex
	mem    map[uint32]byte
	writes [][2]uint32
	crc    string
	status uint32
}

func (m *mock) put(a uint32, v uint32, n int) {
	for i := 0; i < n; i++ {
		m.mem[a+uint32(i)] = byte(v >> uint(i*8))
	}
}
func (m *mock) serve(c net.Conn) {
	defer c.Close()
	for {
		var h [4]byte
		if _, e := io.ReadFull(c, h[:]); e != nil {
			return
		}
		n := int(le.Uint32(h[:]))
		if n < 5 || n > 4096 {
			return
		}
		b := make([]byte, n-4)
		if _, e := io.ReadFull(c, b); e != nil {
			return
		}
		r := []byte{0, 0, 0, 0, 0}
		m.mu.Lock()
		for len(b) > 0 {
			op := b[0]
			b = b[1:]
			switch op {
			case 12, 13, 14:
				str := "SLUS-20645"
				if op == 13 {
					str = m.crc
				}
				if op == 14 {
					str = "1.00"
				}
				r = le.AppendUint32(r, uint32(len(str)+1))
				r = append(r, []byte(str)...)
				r = append(r, 0)
			case 15:
				r = le.AppendUint32(r, m.status)
			case 1, 2, 5, 6:
				if len(b) < 4 {
					panic("truncated command")
				}
				a := le.Uint32(b)
				b = b[4:]
				if a < mirror || a >= mirror+ram {
					panic("bad alias")
				}
				a -= mirror
				width := 4
				if op == 1 || op == 5 {
					width = 2
				}
				if op == 1 || op == 2 {
					for j := 0; j < width; j++ {
						r = append(r, m.mem[a+uint32(j)])
					}
				} else {
					if len(b) < width {
						panic("truncated write")
					}
					for j := 0; j < width; j++ {
						m.mem[a+uint32(j)] = b[j]
					}
					m.writes = append(m.writes, [2]uint32{a, uint32(width)})
					b = b[width:]
				}
			default:
				panic("unexpected opcode, especially save/load")
			}
		}
		m.mu.Unlock()
		le.PutUint32(r, uint32(len(r)))
		if _, e := c.Write(r[:3]); e != nil {
			return
		}
		if _, e := c.Write(r[3:]); e != nil {
			return
		}
	}
}
