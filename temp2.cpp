

half4 GetExponentialHeightFog(float3 WorldPositionRelativeToCamera, float ExcludeDistance)
{
	float rayOrigin0 = FogStruct.ExponentialFogParameters.x;
	float rayOrigin1 = FogStruct.ExponentialFogParameters2.x;
	float falloff0 = FogStruct.ExponentialFogParameters.y;
	float falloff1 = FogStruct.ExponentialFogParameters2.y;
	float density0 = FogStruct.ExponentialFogParameters3.x;
	float density1 = FogStruct.ExponentialFogParameters2.z;
	float height0 = FogStruct.ExponentialFogParameters3.y;
	float height1 = FogStruct.ExponentialFogParameters2.w;
	const half MinFogOpacity = FogStruct.ExponentialFogColorParameter.w;
	float startDistance = FogStruct.ExponentialFogParameters.w;
	float cutoffDistance = FogStruct.ExponentialFogParameters3.w;
	
	float3 CameraToReceiver = WorldPositionRelativeToCamera;
	float CameraToReceiverLengthSqr = dot(CameraToReceiver, CameraToReceiver);
	float CameraToReceiverLengthInv = rsqrt(CameraToReceiverLengthSqr);
	float CameraToReceiverLength = CameraToReceiverLengthSqr * CameraToReceiverLengthInv;
	half3 CameraToReceiverNormalized = CameraToReceiver * CameraToReceiverLengthInv;

	float RayOriginTerms = FogStruct.ExponentialFogParameters.x;		// CollapsedFogParameter[0]
	float RayOriginTermsSecond = FogStruct.ExponentialFogParameters2.x;	// CollapsedFogParameter[1]
	float RayLength = CameraToReceiverLength;
	float RayDirectionZ = CameraToReceiver.z;

	if (0 < cutoffDistance && cutoffDistance < CameraToReceiverLength)
	{
		return half4(0,0,0,1);
	}

	// Factor in StartDistance
	ExcludeDistance = max(ExcludeDistance, startDistance);

	if (ExcludeDistance > 0)
	{
		float ExcludeIntersectionTime = ExcludeDistance * CameraToReceiverLengthInv;
		float CameraToExclusionIntersectionZ = ExcludeIntersectionTime * CameraToReceiver.z;
		float ExclusionIntersectionZ = View.WorldCameraOrigin.z + CameraToExclusionIntersectionZ;
		float ExclusionIntersectionToReceiverZ = CameraToReceiver.z - CameraToExclusionIntersectionZ;

		// Calculate fog off of the ray starting from the exclusion distance, instead of starting from the camera
		RayLength = (1.0f - ExcludeIntersectionTime) * CameraToReceiverLength;
		RayDirectionZ = ExclusionIntersectionToReceiverZ;
		RayOriginTerms       = density0 * exp2( -max(falloff0*(ExclusionIntersectionZ-height0), -127.0f) );
		RayOriginTermsSecond = density1 * exp2( -max(falloff1*(ExclusionIntersectionZ-height1), -127.0f) );
	}

	// Calculate the "shared" line integral (this term is also used for the directional light inscattering) by adding the two line integrals together (from two different height falloffs and densities)
	float ExponentialHeightLineIntegralShared = 
		  CalculateLineIntegralShared(falloff0, RayDirectionZ, RayOriginTerms) 
		+ CalculateLineIntegralShared(falloff1, RayDirectionZ, RayOriginTermsSecond);

	float ExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * RayLength;

	half3 InscatteringColor = ComputeInscatteringColor(CameraToReceiver, CameraToReceiverLength);
	half3 DirectionalInscattering = 0;

	// Calculate the amount of light that made it through the fog using the transmission equation
	half ExpFogFactor = max(exp2(-ExponentialHeightLineIntegral), MinFogOpacity);

	// Fog color is unused when additive / modulate blend modes are active.
	#if (MATERIALBLENDING_ADDITIVE || MATERIALBLENDING_MODULATE)
		half3 FogColor = 0.0;
	#else
		half3 FogColor = (InscatteringColor) * (1 - ExpFogFactor) + DirectionalInscattering;
	#endif

	return half4(FogColor, ExpFogFactor);
	// FinalColor.rgb = lerp(ExpFogFactor, InscatteringColor, Dst.rgb) + DirectionalInscattering
}

// Calculate the line integral of the ray from the camera to the receiver position through the fog density function
// The exponential fog density function is d = GlobalDensity * exp(-HeightFalloff * z)
float CalculateLineIntegralShared(float FogHeightFalloff, float RayDirectionZ, float RayOriginTerms)
{
	float Falloff = max(-127.0f, FogHeightFalloff * RayDirectionZ);    // if it's lower than -127.0, then exp2() goes crazy in OpenGL's GLSL.
	float LineIntegral = ( 1.0f - exp2(-Falloff) ) / Falloff;
	float LineIntegralTaylor = log(2.0) - ( 0.5 * Pow2( log(2.0) ) ) * Falloff;		// Taylor expansion around 0
	
	return RayOriginTerms * ( abs(Falloff) > FLT_EPSILON2 ? LineIntegral : LineIntegralTaylor );
}
