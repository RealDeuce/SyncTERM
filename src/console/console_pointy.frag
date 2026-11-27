#version 330 core
// SPDX-License-Identifier: LGPL-2.1-or-later
// Pointy corner rules from st_pointy.c (Stephen Hurd, BSD-2-Clause).
// xBR rules adapted from st_xbr.c (LGPL-2.1-or-later).
// Copyright 2011-2012 Hyllian/Jararaca, 2014 Arwa Arif, 2026 Stephen Hurd.
uniform sampler2D pixels;
uniform ivec2 multiplier;
uniform bool repeatOnly;
out vec4 colour;

uint sampleRGB(ivec2 p)
{
	p = clamp(p, ivec2(0), textureSize(pixels, 0) - 1);
	uvec3 v = uvec3(texelFetch(pixels, p, 0).rgb * 255.0 + 0.5);
	return (v.r << 16) | (v.g << 8) | v.b;
}
vec4 unpackRGB(uint p)
{
	return vec4((p >> 16) & 255u, (p >> 8) & 255u, p & 255u, 255u) / 255.0;
}
ivec3 ycocg(uint p)
{
	int b = int(p & 255u);
	int co = int(p >> 16) - b;
	int tmp = b + co / 2;
	int cg = int((p >> 8) & 255u) - tmp;
	return ivec3(tmp + cg / 2, co, cg);
}
int difference(uint a, uint b)
{
	ivec3 v = abs(ycocg(a) - ycocg(b));
	return v.x + v.y / 2 + v.z / 2;
}
bool similar(uint a, uint b) { return difference(a,b) < 155; }

uint mixPacked(uint a, uint b, uint weight, uint shift)
{
	uint rb = a & 0xff00ffu, g = a & 0xff00u;
	return ((rb + ((((b & 0xff00ffu) - rb) * weight) >> shift)) & 0xff00ffu)
	     | ((g + ((((b & 0xff00u) - g) * weight) >> shift)) & 0xff00u);
}
uint halfBlend(uint a, uint b) { return ((a & 0xfefefeu) >> 1) + ((b & 0xfefefeu) >> 1); }

// The four rotated corner rules update the same block in order. Some of the
// blends read a previously updated neighbour, so keep the entire block here.
uint block[16];
int n;
ivec2 dx, dy;
int indexOf(int i)
{
	ivec2 origin = (max(-dx, ivec2(0)) + max(-dy, ivec2(0))) * (n - 1);
	ivec2 p = origin + dx * (i % n) + dy * (i / n);
	return p.y * n + p.x;
}
#define E(i) block[indexOf(i)]
void corner(ivec2 p, uint center)
{
	uint right = sampleRGB(p + dx), below = sampleRGB(p + dy);
	if (center == below || center == right) return;
	uint diag = sampleRGB(p + dx + dy);
	uint upperRight = sampleRGB(p + dx - dy), lowerLeft = sampleRGB(p - dx + dy);
	uint left = sampleRGB(p - dx), up = sampleRGB(p - dy);
	uint farRight = sampleRGB(p + 2*dx), farBelow = sampleRGB(p + 2*dy);
	uint farDiagX = sampleRGB(p + 2*dx + dy), farDiagY = sampleRGB(p + dx + 2*dy);
	int e = difference(center,upperRight) + difference(center,lowerLeft)
	      + difference(diag,farBelow) + difference(diag,farRight) + 4*difference(below,right);
	int i = difference(below,left) + difference(below,farDiagY)
	      + difference(right,farDiagX) + difference(right,up) + 4*difference(center,diag);
	if (e > i) return;
	uint ink = difference(center,right) <= difference(center,below) ? right : below;
	int last = n*n-1;
	if (e == i || !((!similar(right,up) && !similar(below,left))
	    || (similar(center,diag) && !similar(right,farDiagX) && !similar(below,farDiagY))
	    || similar(center,lowerLeft) || similar(center,upperRight))) {
		E(last) = halfBlend(E(last), ink);
		return;
	}
	int ke = difference(right,lowerLeft), ki = difference(below,upperRight);
	bool alongX = 2*ke <= ki && center != lowerLeft && left != lowerLeft;
	bool alongY = ke >= 2*ki && center != upperRight && up != upperRight;
	if (n == 2) {
		if (alongX && alongY) {
			E(3) = mixPacked(E(3),ink,7u,3u);
			E(2) = mixPacked(E(2),ink,1u,2u);
			E(1) = E(2);
		} else if (alongX || alongY) {
			int edge = alongX ? 2 : 1;
			E(3) = mixPacked(E(3),ink,3u,2u);
			E(edge) = mixPacked(E(edge),ink,1u,2u);
		} else E(3) = halfBlend(E(3),ink);
	} else if (alongX && alongY) {
		E(13) = mixPacked(E(13),ink,3u,2u);
		E(12) = mixPacked(E(12),ink,1u,2u);
		E(15) = E(14) = E(11) = ink;
		E(10) = E(3) = E(12);
		E(7) = E(13);
	} else if (alongX || alongY) {
		int near1 = alongX ? 11 : 14, near2 = alongX ? 13 : 7;
		int far1 = alongX ? 12 : 3;
		E(near1) = mixPacked(E(near1),ink,3u,2u);
		E(near2) = mixPacked(E(near2),ink,3u,2u);
		E(10) = mixPacked(E(10),ink,1u,2u);
		E(far1) = mixPacked(E(far1),ink,1u,2u);
		E(alongX ? 14 : 11) = E(15) = ink;
	} else {
		E(11) = halfBlend(E(11),ink);
		E(14) = halfBlend(E(14),ink);
		E(15) = ink;
	}
}
void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	ivec2 p = pixel / multiplier;
	uint center = sampleRGB(p);
	n = multiplier.x;
	ivec2 local = pixel % multiplier;
	if (!repeatOnly && (n & 1) != 0) {
		ivec2 fromEdge = min(local, n - 1 - local);
		if (fromEdge.x + fromEdge.y < n / 2) {
			ivec2 dir = ivec2(sign(local - n / 2));
			uint side = sampleRGB(p + ivec2(dir.x, 0));
			if (sampleRGB(p + dir) != center && sampleRGB(p + ivec2(0, dir.y)) == side)
				center = side;
		}
	} else if (!repeatOnly) {
		for (int i = 0; i < 16; ++i) block[i] = center;
		dx = ivec2(1,0); dy = ivec2(0,1);
		for (int i = 0; i < 4; ++i) {
			corner(p, center);
			ivec2 next = -dy;
			dy = dx;
			dx = next;
		}
		center = block[local.y*n + local.x];
	}
	colour = unpackRGB(center);
}
