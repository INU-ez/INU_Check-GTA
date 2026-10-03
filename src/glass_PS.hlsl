// gtacheck — a pane of liquid glass over the 3D map / backdrop art.
// Drawn as an im2d quad through librw's im2dOverridePS: TEXCOORD0.xy is where the pixel lies in the
// source texture (the screen position is recovered from it), COLOR0.rgb is the pane tint, COLOR0.a the fade.
// Compile: fxc /nologo /T ps_2_b /Vn glass_PS /Fh glass_PS.h glass_PS.hlsl
//
// The pane is a rounded rectangle (or two joined with a round fillet), so its distance field is analytic. A circular bevel over the outer
// `bevel` pixels turns that distance into a height; its slope bends the sample outwards (refraction),
// splits the colours along the edge normal (dispersion), and catches a light (specular). The taps that
// carry the dispersion are spread over a small disc as well, which is the frost.

sampler2D src : register(s0);

float4 srcRect : register(c1);	// the source texture on screen: x, y, w, h
float4 pane    : register(c2);	// box A: centre x, y, half width, half height (open edges pushed far out)
float4 shape   : register(c3);	// box A corner radius, bevel width, refraction px, dispersion px
float4 look    : register(c4);	// frost px, specular, light dir x, y (screen, y down)
float4 mixp    : register(c5);	// tint inside, tint on the bevel, darkening on the slope, chroma boost
float4 clampUV : register(c6);	// min u, min v, max u, max v (half a texel in)
float4 boxB    : register(c7);	// box B, joined to A (a menu and the strip it hangs from): centre, half size
float4 joinB   : register(c8);	// box B corner radius, the fillet where A and B meet, 1 = B is there, 0

float4 railFrame : register(c9); // rail width, upper bar bottom, lower bar top, junction radius

struct PS_in {
	float3 uv  : TEXCOORD0;
	float4 col : COLOR0;
};

float3 fetch(float2 p)
{
	float2 uv = (p - srcRect.xy) / srcRect.zw;
	return tex2D(src, clamp(uv, clampUV.xy, clampUV.zw)).rgb;
}

// signed distance to a rounded rectangle (< 0 inside)
float roundBox(float2 rel, float2 half, float r)
{
	float2 q = abs(rel) - (half - r);
	return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// the pane's shape: box A, or A and B joined with a round fillet of radius f in the corners where they meet
// (hg_sdf's fOpUnionRound — near the edges it is the true distance, which is all the bevel reads)
float paneDist(float2 p, float rA, float rB)
{
	float dA = roundBox(p - pane.xy, pane.zw, rA);
	float dB = roundBox(p - boxB.xy, boxB.zw, rB);
	float f = max(joinB.y, 1e-3);
	float2 u = max(float2(f - dA, f - dB), 0.0);
	float dJ = max(f, min(dA, dB)) - length(u);
	float d = joinB.z > 0.5 ? dJ : dA;
	// Outside an open-ended rounded viewport = the rail and both bars.
	// Include it when a menu joins the bar, so opening a menu cannot restore
	// the old separate rail corner.
	float r = railFrame.w;
	float2 q = float2(railFrame.x + r - p.x,
		abs(p.y - (railFrame.y + railFrame.z) * 0.5) - (railFrame.z - railFrame.y) * 0.5 + r);
	float df = r - length(max(q, 0.0)) - min(max(q.x, q.y), 0.0);
	return joinB.w > 0.5 ? min(d, df) : d;
}

static const int TAPS = 10;	// 12 no longer fit ps_2_b's 32 constant registers next to the joined shape

float4 main(PS_in i) : COLOR
{
	float2 p = srcRect.xy + i.uv.xy * srcRect.zw;
	float bevel = shape.y;

	float cov = saturate(0.5 - paneDist(p, shape.x, joinB.x));
	// Joined bars tile on integer rows. Their shared straight boundary is
	// fully covered; only the exposed curved shoulders need antialiasing.
	if(shape.x == 0.0 && joinB.z > 0.5)
		cov = max(cov, roundBox(p - pane.xy, pane.zw, 0.0) <= 0.0 ? 1.0 : 0.0);

	// The normals come from boxes whose corners are at least as round as the bevel is wide: the diagonal
	// crease of the distance field then starts where the bevel is already flat. The direction is the
	// field's own gradient, so it turns with the fillets as well.
	float rA2 = min(max(shape.x, bevel), min(pane.z, pane.w));
	float rB2 = min(max(joinB.x, bevel), min(boxB.z, boxB.w));
	float d0 = paneDist(p, rA2, rB2);
	float2 g = float2(paneDist(p + float2(1.0, 0.0), rA2, rB2), paneDist(p + float2(0.0, 1.0), rA2, rB2)) - d0;
	float2 dir = g / max(length(g), 1e-4);
	float t = -d0;	// distance from the edge, inwards

	float s = saturate(t / max(bevel, 1e-3));
	float u = 1.0 - s;
	float h = sqrt(max(1.0 - u * u, 0.0));	// circular bevel: 0 at the edge, 1 where it flattens
	float slope = u / max(h, 0.05);
	slope = 1.6 * tanh(slope / 1.6);	// thin spots and the very edge: a capped slope, not a spike
	float3 n = normalize(float3(dir * slope, 1.0));
	float slopeN = saturate(slope * 0.6);

	float2 off = n.xy * shape.z;
	float edge = 1.0 - h;
	float w = shape.w * edge;
	float fr = look.x + 1.2;

	float3 num = 0.0;
	float3 den = 0.0;
	[unroll] for(int k = 0; k < TAPS; k++){
		float tt = (k + 0.5) / TAPS;
		float sp = tt * 2.0 - 1.0;			// red bends least, blue most
		float ga = k * 2.39996323;			// golden angle: the disc fills evenly
		float2 o = float2(cos(ga), sin(ga)) * fr * sqrt(tt);
		float3 x = (tt - float3(0.0, 0.5, 1.0)) / 0.30;
		float3 wt = exp(-x * x) + 1e-4;
		num += wt * fetch(p + off + o + dir * (w * sp));
		den += wt;
	}
	float3 col = num / den;

	float lum = dot(col, float3(0.2126, 0.7152, 0.0722));
	col = lum + (col - lum) * lerp(1.0, mixp.w, edge);
	col *= 1.0 - mixp.z * slopeN;

	float ta = lerp(mixp.y, mixp.x, s * s * (3.0 - 2.0 * s));
	col = lerp(col, i.col.rgb, ta);

	float3 L = normalize(float3(look.z, look.w, 0.75));
	col += pow(saturate(dot(n, L)), 18.0) * look.y * slopeN;
	float3 L2 = normalize(float3(-look.z, -look.w, 0.75));	// the far rim catches a little of it too
	col += pow(saturate(dot(n, L2)), 18.0) * look.y * 0.35 * slopeN;

	return float4(saturate(col), cov * i.col.a);
}
