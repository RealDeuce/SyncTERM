#version 330 core
uniform sampler2D pixels;
uniform ivec2 destination;
uniform ivec2 origin;
uniform int nearest;
out vec4 colour;

// Match the software scaler's fixed-point axis map and signed YCoCg rounding.
ivec3 ycocg(ivec3 rgb)
{
	int co = rgb.r - rgb.b;
	int tmp = rgb.b + co / 2;
	int cg = rgb.g - tmp;
	return ivec3(tmp + cg / 2, co, cg);
}

ivec3 blend(ivec3 first, ivec3 second, int weight)
{
	if (weight == 0 || all(equal(first, second))) return first;
	ivec3 v = (ycocg(first) * (65535 - weight) + ycocg(second) * weight) / 65535;
	int tmp = v.x - v.z / 2;
	int b = tmp - v.y / 2;
	return clamp(ivec3(b + v.y, v.z + tmp, b), 0, 255);
}

ivec3 sampleRGB(ivec2 coord)
{
	return ivec3(texelFetch(pixels, coord, 0).rgb * 255.0 + 0.5);
}

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy) - origin;
	p.y = destination.y - 1 - p.y;
	ivec2 size = textureSize(pixels, 0);
	if (nearest != 0) {
		ivec2 cell = ivec2((vec2(p) + 0.5) * vec2(size) / vec2(destination));
		colour = vec4(texelFetch(pixels, min(cell, size - 1), 0).rgb, 1);
		return;
	}
	uvec2 repeatCount = max(uvec2(1), uvec2(destination / size));
	uvec2 expanded = uvec2(size) * repeatCount;
	uvec2 position = uvec2(p) * ((expanded << 16u) / uvec2(destination));
	uvec2 first = position >> 16u;
	ivec2 a = ivec2(first / repeatCount);
	ivec2 b = ivec2(min(first + 1u, expanded - 1u) / repeatCount);
	ivec2 weight = ivec2(position & 65535u);
	ivec3 left = blend(sampleRGB(a), sampleRGB(ivec2(a.x, b.y)), weight.y);
	ivec3 right = blend(sampleRGB(ivec2(b.x, a.y)), sampleRGB(b), weight.y);
	colour = vec4(vec3(blend(left, right, weight.x)) / 255.0, 1);
}
