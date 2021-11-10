

class FScene : public FSceneInterface {
	TSparseArray<FLightSceneInfoCompact> Lights;
}

class FSceneRenderer {
	TArray<FVisibleLightInfo> VisibleLightInfos;
}

class FLightSceneInfoCompact {
	VectorRegister BoundingSphereVector; // XYZ: origin, W:sphere radius
	FLinearColor Color;
	FLightSceneInfo* LightSceneInfo; // must not be 0
	uint32 LightType : 2;	// LightType_Directional, LightType_Point, LightType_Spot, LightType_Rect
	uint32 bCastDynamicShadow : 1;
	uint32 bCastStaticShadow : 1;
	uint32 bStaticLighting : 1;
	uint32 bAffectReflection : 1;
	uint32 bAffectGlobalIllumination : 1;
	uint32 bCastRaytracedShadow : 1;
};

class FLightSceneInfo : public FRenderResource {
	FLightSceneProxy* Proxy;/** The light's scene proxy. */	
	int32 Id;/** If bVisible == true, this is the index of the primitive in Scene->Lights. */
}




class ENGINE_API FLightSceneProxy {
	const ULightComponent* LightComponent;
	FLightSceneInfo* LightSceneInfo;
	FVector4 Position;
	FLinearColor Color = LightComponent->ComputeLightBrightness() * FLinearColor(LightComponent->LightColor);
}

class FDirectionalLightSceneProxy : public FLightSceneProxy {}

class FLocalLightSceneProxy : public FLightSceneProxy {
	float Radius = Component->AttenuationRadius;
}

class FPointLightSceneProxy : public FLocalLightSceneProxy {
	const uint32 bInverseSquared = Component->bUseInverseSquaredFalloff;
	float FalloffExponent = Component->LightFalloffExponent;
	float SourceRadius = Component->SourceRadius;
	float SoftSourceRadius = Component->SoftSourceRadius;
	float SourceLength = Component->SourceLength;
};

class FSpotLightSceneProxy : public FPointLightSceneProxy{}
class FRectLightSceneProxy : public FLocalLightSceneProxy{}


float UPointLightComponent::ComputeLightBrightness() const
{
	float LightBrightness = Intensity;

	if (bUseInverseSquaredFalloff)
	{
		if (IntensityUnits == ELightUnits::Candelas)
		{
			LightBrightness *= (100.f * 100.f); // Conversion from cm2 to m2
		}
		else if (IntensityUnits == ELightUnits::Lumens)
		{
			LightBrightness *= (100.f * 100.f / 4 / PI); // Conversion from cm2 to m2 and 4PI from the sphere area in the 1/r2 attenuation
		}
		else
		{
			LightBrightness *= 16; // Legacy scale of 16
		}
	}
	return LightBrightness;
}