#ifndef BRDF_HLSLI
#define BRDF_HLSLI

#include "math.hlsli"
#include "random.hlsli"
#include "light_source.hlsli"

struct surface_info
{
	// Set from outside.
	float3 P;
	float3 N;
	float3 V;

	float4 albedo;
	float roughness;
	float metallic;
	float3 emission;

	// Inferred from properties above.
	float alphaRoughness;
	float alphaRoughnessSquared;
	float NdotV;
	float3 F0;
	float3 F;
	float3 R;

	inline void inferRemainingProperties()
	{
		alphaRoughness = roughness * roughness;
		alphaRoughnessSquared = alphaRoughness * alphaRoughness;
		NdotV = saturate(dot(N, V));
		F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo.xyz, metallic);
		R = reflect(-V, N);

		float F90 = saturate(50.f * dot(F0, (1.f / 3.f)));
		F = F0 + (F90 - F0) * pow(1.f - NdotV, 5.f); // Fresnel.
	}
};

struct light_info
{
	float3 L;
	float3 H;
	float NdotL;
	float NdotH;
	float LdotH;
	float VdotH;

	float3 radiance;
	float distanceToLight; // Only set if using a specialized initialize function. 

	inline void initialize(surface_info surface, float3 L_, float3 rad)
	{
		L = L_;
		H = normalize(L + surface.V);

		NdotL = saturate(dot(surface.N, L));
		NdotH = saturate(dot(surface.N, H));
		LdotH = saturate(dot(L, H));
		VdotH = saturate(dot(surface.V, H));

		radiance = rad;
	}

	inline void initializeFromPointLight(surface_info surface, point_light_cb pl)
	{
		float3 L = pl.position - surface.P;
		distanceToLight = length(L);
		L /= distanceToLight;

		initialize(surface, L, pl.radiance * getAttenuation(distanceToLight, pl.radius) * LIGHT_RADIANCE_SCALE);
	}

	inline void initializeFromRandomPointOnSphereLight(surface_info surface, point_light_cb pl, float radius, inout uint randSeed)
	{
		float3 randomPointOnLight = pl.position + getRandomPointOnSphere(randSeed, radius);

		float3 L = randomPointOnLight - surface.P;
		distanceToLight = length(L);
		L /= distanceToLight;

		initialize(surface, L, pl.radiance * getAttenuation(distanceToLight, pl.radius) * LIGHT_RADIANCE_SCALE);
	}

	inline void initializeFromSpotLight(surface_info surface, spot_light_cb sl)
	{
		float3 L = (sl.position - surface.P);
		distanceToLight = length(L);
		L /= distanceToLight;

		float innerCutoff = sl.getInnerCutoff();
		float outerCutoff = sl.getOuterCutoff();
		float epsilon = innerCutoff - outerCutoff;

		float theta = dot(-L, sl.direction);
		float attenuation = getAttenuation(distanceToLight, sl.maxDistance);
		float intensity = saturate((theta - outerCutoff) / epsilon) * attenuation;

		initialize(surface, L, sl.radiance * intensity * LIGHT_RADIANCE_SCALE);
	}
};

// http://graphicrants.blogspot.com/2013/08/specular-brdf-reference.html

// ----------------------------------------
// FRESNEL (Surface becomes more reflective when seen from a grazing angle).
// ----------------------------------------

static float3 fresnelSchlick(float LdotH, float3 F0)
{
	return F0 + (float3(1.f, 1.f, 1.f) - F0) * pow(1.f - LdotH, 5.f);
}

static float3 fresnelSchlick(float3 F0, float F90, float VdotH)
{
	return F0 + (F90 - F0) * pow(1.f - VdotH, 5.f);
}

static float3 fresnelSchlickRoughness(float LdotH, float3 F0, float roughness)
{
	float v = 1.f - roughness;
	return F0 + (max(float3(v, v, v), F0) - F0) * pow(1.f - LdotH, 5.f);
}

// ----------------------------------------
// DISTRIBUTION (Microfacets' orientation based on roughness).
// ----------------------------------------

static float distributionGGX(float NdotH, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;
	float NdotH2 = NdotH * NdotH;

	float d = (NdotH2 * (a2 - 1.f) + 1.f);
	return a2 / max(d * d * M_PI, 0.001f);
}

static float distributionGGX(surface_info surface, light_info light)
{
	float NdotH = light.NdotH;
	float NdotH2 = NdotH * NdotH;
	float a2 = surface.alphaRoughnessSquared;
	float d = (NdotH2 * (a2 - 1.f) + 1.f);
	return a2 / max(d * d * M_PI, 0.001f);
}

// ----------------------------------------
// GEOMETRIC MASKING (Microfacets may shadow each-other).
// ----------------------------------------

static float geometrySmith(float NdotL, float NdotV, float roughness)
{
	float k = (roughness * roughness) * 0.5f;

	float ggx2 = NdotV / (NdotV * (1.f - k) + k);
	float ggx1 = NdotL / (NdotL * (1.f - k) + k);

	return ggx1 * ggx2;
}

static float geometrySmith(surface_info surface, light_info light)
{
	float k = surface.alphaRoughness * 0.5f;

	float ggx2 = surface.NdotV / (surface.NdotV * (1.f - k) + k);
	float ggx1 = light.NdotL / (light.NdotL * (1.f - k) + k);

	return ggx1 * ggx2;
}

// ----------------------------------------
// IMPORTANCE SAMPLING
// ----------------------------------------

// When using this function to sample, the probability density is:
//      pdf = D * NdotH / (4 * HdotV)
static float3 importanceSampleGGX(inout uint randSeed, float3 N, float roughness)
{
	// Get our uniform random numbers.
	float2 randVal = float2(nextRand(randSeed), nextRand(randSeed));

	// Get an orthonormal basis from the normal.
	float3 B = getPerpendicularVector(N);
	float3 T = cross(B, N);

	// GGX NDF sampling.
	float a2 = roughness * roughness;
	float cosThetaH = sqrt(max(0.f, (1.f - randVal.x) / ((a2 - 1.f) * randVal.x + 1.f)));
	float sinThetaH = sqrt(max(0.f, 1.f - cosThetaH * cosThetaH));
	float phiH = randVal.y * M_PI * 2.f;

	// Get our GGX NDF sample (i.e., the half vector).
	return T * (sinThetaH * cos(phiH)) +
		B * (sinThetaH * sin(phiH)) +
		N * cosThetaH;
}

// Call this with a hammersley distribution as Xi.
static float4 importanceSampleGGX(float2 Xi, float3 N, float roughness)
{
	float a = roughness * roughness;
	float a2 = a * a;

	float phi = 2.f * M_PI * Xi.x;
	float cosTheta = sqrt((1.f - Xi.y) / (1.f + (a2 - 1.f) * Xi.y));
	float sinTheta = sqrt(1.f - cosTheta * cosTheta);

	// From spherical coordinates to cartesian coordinates.
	float3 H;
	H.x = cos(phi) * sinTheta;
	H.y = sin(phi) * sinTheta;
	H.z = cosTheta;

	// From tangent-space vector to world-space sample vector.
	float3 up = abs(N.z) < 0.999f ? float3(0.f, 0.f, 1.f) : float3(1.f, 0.f, 0.f);
	float3 tangent = normalize(cross(up, N));
	float3 bitangent = cross(N, tangent);

	H = tangent * H.x + bitangent * H.y + N * H.z;

	float d = (cosTheta * a2 - cosTheta) * cosTheta + 1.f;
	float D = a2 / (M_PI * d * d);
	float pdf = D * cosTheta;
	return float4(normalize(H), pdf);
}

// W = PDF = 1 / 4pi
static float4 uniformSampleSphere(float2 E)
{
	float phi = 2 * M_PI * E.x;
	float cosTheta = 1.f - 2.f * E.y;
	float sinTheta = sqrt(1.f - cosTheta * cosTheta);

	float3 H;
	H.x = sinTheta * cos(phi);
	H.y = sinTheta * sin(phi);
	H.z = cosTheta;

	float PDF = 1.f / (4.f * M_PI);

	return float4(H, PDF);
}
#endif