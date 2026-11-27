// SPDX-License-Identifier: BSD-3-Clause
package main

import (
	"bufio"
	"fmt"
	"io"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"time"
)

func main() {
	out := io.Writer(os.Stdout)
	if exe, e := os.Executable(); e == nil {
		if f, e := os.OpenFile(filepath.Join(filepath.Dir(exe), "TC3-Producer-Hook.log"), os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0600); e == nil {
			defer f.Close()
			out = io.MultiWriter(os.Stdout, f)
		}
	}
	log := func(f string, a ...any) { fmt.Fprintf(out, f+"\n", a...) }
	log("TC3 producer-hook P1 | %s", time.Now().Format(time.RFC3339))
	log("US SLUS-20645 CRC7290669C. Single-player primary render route only. No track writes or save states. Do not shoot while enabled; close with quit.")
	c, e := net.DialTimeout("tcp", "127.0.0.1:28011", 2*time.Second)
	if e != nil {
		log("Connection: %v", e)
		fmt.Scanln()
		return
	}
	defer c.Close()
	h := hookSession{p: &pine{c}}
	defer func() {
		if e := h.disable(); e != nil {
			log("RESTORE FAILED: %v. Restart game before testing again.", e)
		}
	}()
	if e = h.p.identity(true); e != nil {
		log("Refused: %v", e)
		fmt.Scanln()
		return
	}
	log("Commands: enable | w/a/s/d | q/e | left/right/up/down | status | disable | recover (abandoned P1 hook ONLY, close other tools first) | quit")
	cmds := make(chan string)
	go func() {
		defer close(cmds)
		sc := bufio.NewScanner(os.Stdin)
		for sc.Scan() {
			cmds <- strings.ToLower(strings.TrimSpace(sc.Text()))
		}
	}()
	sig := make(chan os.Signal, 1)
	signal.Notify(sig, os.Interrupt)
	defer signal.Stop(sig)
	ticker := time.NewTicker(100 * time.Millisecond)
	defer ticker.Stop()
	for {
		select {
		case <-sig:
			return
		case <-ticker.C:
			if h.active {
				s, e := h.p.renderSample()
				if e != nil || !s.primary() || s.code != 0 {
					log("Render context or connection changed; restoring hook")
					if e := h.disable(); e != nil {
						log("RESTORE FAILED: %v", e)
						return
					}
				}
			}
		case cmd, ok := <-cmds:
			if !ok || cmd == "quit" {
				return
			}
			if cmd == "" {
				continue
			}
			log("> %s", cmd)
			var e error
			switch cmd {
			case "enable":
				t := time.Now()
				e = h.enable()
				if e == nil {
					log("Producer hook enabled in %.1f ms", float64(time.Since(t).Microseconds())/1000)
				}
			case "disable":
				e = h.disable()
				if e == nil {
					log("Original producer instruction restored")
				}
			case "recover":
				e = h.recover()
				if e == nil {
					log("Abandoned producer hook restored")
				}
			case "status":
				var s renderSample
				s, e = h.p.renderSample()
				if e == nil {
					log("Active=%v uncertain=%v code=%08X alternate=%08X refs=%08X pause=%d sim=%d main=%d VM=%d", h.active, h.uncertain, s.code, s.splitCode, s.refs, s.pause, s.sim, s.clock, s.vm)
					log("Context XYZ %.3f %.3f %.3f | global %.3f %.3f %.3f | render %.3f %.3f %.3f", s.local[12], s.local[13], s.local[14], s.global[12], s.global[13], s.global[14], s.render[12], s.render[13], s.render[14])
				}
			case "w", "a", "s", "d", "q", "e", "left", "right", "up", "down":
				e = h.move(cmd)
				if e == nil {
					log("Published XYZ %.3f %.3f %.3f", h.target[0], h.target[1], h.target[2])
				} else if h.active {
					log("Movement failed; restoring native call")
					if r := h.disable(); r != nil {
						log("RESTORE FAILED: %v", r)
						return
					}
				}
			default:
				e = fmt.Errorf("unknown command")
			}
			if e != nil {
				log("Refused: %v", e)
				if h.uncertain {
					if r := h.disable(); r != nil {
						log("RESTORE FAILED: %v", r)
						return
					}
				}
			}
		}
	}
}
