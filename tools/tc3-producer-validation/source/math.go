// SPDX-License-Identifier: BSD-3-Clause
package main

import "math"

type matrix [16]float64

func poseMatrix(p pose) matrix {
	sx, cx := math.Sincos(p[3])
	sy, cy := math.Sincos(p[4])
	sz, cz := math.Sincos(p[5])
	return matrix{cz*cy + sz*sx*sy, sz * cx, -cz*sy + sz*sx*cy, 0, -sz*cy + cz*sx*sy, cz * cx, sz*sy + cz*sx*cy, 0, cx * sy, -sx, cx * cy, 0, p[0], p[1], p[2], 1}
}
func product(a, b matrix) (c matrix) {
	for i := 0; i < 4; i++ {
		for j := 0; j < 4; j++ {
			for k := 0; k < 4; k++ {
				c[4*i+j] += a[4*i+k] * b[4*k+j]
			}
		}
	}
	return
}
func inverseRigid(m matrix) (r matrix) {
	r[15] = 1
	for i := 0; i < 3; i++ {
		for j := 0; j < 3; j++ {
			r[i*4+j] = m[j*4+i]
		}
	}
	for j := 0; j < 3; j++ {
		for k := 0; k < 3; k++ {
			r[12+j] -= m[12+k] * r[4*k+j]
		}
	}
	return
}
func matrixPose(m matrix) pose {
	pitch := math.Asin(math.Max(-1, math.Min(1, -m[9])))
	yaw, roll := math.Atan2(m[8], m[10]), math.Atan2(m[1], m[5])
	if math.Abs(math.Cos(pitch)) < 1e-7 {
		roll = 0
		yaw = math.Atan2(-m[2], m[0])
	}
	return pose{m[12], m[13], m[14], pitch, yaw, roll}
}
