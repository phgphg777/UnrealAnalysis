

// -Inf < hardness < 1.0,  0.0 < radius < Inf
// Returns (1 - dist(A,B) / radius) / (1 - hardness)
// Falloff starts from distance of (radius*hardness) to distance of (radius) linearly.
float SphereMask(float3 A, float3 B, float radius=256.0, float hardness=1.0)
{
	float distance = sqrt(dot(A-B, A-B));
	float invRadius = 1.0 / max(0.00001, radius);
	float normalizeDistance = distance * invRadius;
	float invHardness = 1 / max(0.00001, 1 - hardness);
	float maskUnclamped = (1 - normalizeDistance) * invHardness;
	return clamp(maskUnclamped, 0.0, 1.0);
}
