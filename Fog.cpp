//phgphg
UCLASS()
class ENGINE_API UTexturedCurve : public UCurveFloat
{
	GENERATED_UCLASS_BODY()

	class UTexture* Texture1D;
	bool bNeedUpdate;

	UTexture* GetTexture();
	virtual void OnCurveChanged(const TArray<FRichCurveEditInfo>& ChangedCurveEditInfos) override;
};
//phgphg


// ExponentialHeightFogComponent.h
struct FExponentialHeightFogData
{
	float FogDensity = 0.02f;
	float FogHeightFalloff = 0.2f;
	float FogHeightOffset = 0.0f;	/** Height offset, relative to the actor position Z. */
}
class UExponentialHeightFogComponent : public USceneComponent
{
	FLinearColor FogInscatteringColor;
	float FogDensity;
	float FogHeightFalloff;
	float FogMaxOpacity;
	float StartDistance;
	float FogCutoffDistance; /** Scene elements past this distance will not have fog applied.  This is useful for excluding skyboxes which already have fog baked in. */
	FExponentialHeightFogData SecondFogData;

	UTextureCube* InscatteringColorCubemap;
	float InscatteringColorCubemapAngle;
	FLinearColor InscatteringTextureTint;
	float FullyDirectionalInscatteringColorDistance;
	float NonDirectionalInscatteringColorDistance;
};
// HeightFogComponent.cpp
UExponentialHeightFogComponent::UExponentialHeightFogComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	FogInscatteringColor = FLinearColor(0.447f, 0.638f, 1.0f);
	FogDensity = 0.02f;
	FogHeightFalloff = 0.2f;
	FogMaxOpacity = 1.0f;
	StartDistance = 0.0f;
	FogCutoffDistance = 0; // disabled by default
	SecondFogData.FogDensity = 0.0f; // No influence from the second fog as default

	InscatteringTextureTint = FLinearColor::White;
	FullyDirectionalInscatteringColorDistance = 100000.0f;
	NonDirectionalInscatteringColorDistance = 1000.0f;
}
void UExponentialHeightFogComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	FogDensity = FMath::Clamp(FogDensity, 0.0f, 10.0f);
	SecondFogData.FogDensity = FMath::Clamp(SecondFogData.FogDensity, 0.0f, 10.0f);
	FogHeightFalloff = FMath::Clamp(FogHeightFalloff, 0.0f, 2.0f);
	SecondFogData.FogHeightFalloff = FMath::Clamp(SecondFogData.FogHeightFalloff, 0.0f, 2.0f);
	
	FogMaxOpacity = FMath::Clamp(FogMaxOpacity, 0.0f, 1.0f);
	StartDistance = FMath::Clamp(StartDistance, 0.0f, (float)WORLD_MAX);
	FogCutoffDistance = FMath::Clamp(FogCutoffDistance, 0.0f, (float)(10 * WORLD_MAX));
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
}


// SceneCore.h
class FExponentialHeightFogSceneInfo
{
	//float LightTerminatorAngle;
	const UExponentialHeightFogComponent* Component;
	
	FLinearColor FogColor;
	float FogMaxOpacity;
	float StartDistance; 
	float FogCutoffDistance;
	struct FExponentialHeightFogSceneData
	{
		float Height;
		float Density;
		float HeightFalloff;
	} FogData[2];

	UTextureCube* InscatteringColorCubemap;
	float InscatteringColorCubemapAngle;
	float FullyDirectionalInscatteringColorDistance;
	float NonDirectionalInscatteringColorDistance;
};
// SceneCore.cpp
FExponentialHeightFogSceneInfo::FExponentialHeightFogSceneInfo(const UExponentialHeightFogComponent* InComponent):
	Component(InComponent),
	FogMaxOpacity(InComponent->FogMaxOpacity),
	StartDistance(InComponent->StartDistance),
	FogCutoffDistance(InComponent->FogCutoffDistance),
	// LightTerminatorAngle(0),
	DirectionalInscatteringExponent(InComponent->DirectionalInscatteringExponent),
	DirectionalInscatteringStartDistance(InComponent->DirectionalInscatteringStartDistance),
	DirectionalInscatteringColor(InComponent->DirectionalInscatteringColor)
{
	FogColor = InComponent->InscatteringColorCubemap ? InComponent->InscatteringTextureTint : InComponent->FogInscatteringColor;

	FogData[0].Height = InComponent->GetComponentLocation().Z;
	FogData[1].Height = InComponent->GetComponentLocation().Z + InComponent->SecondFogData.FogHeightOffset;
	FogData[0].Density = InComponent->FogDensity / 1000.0f;	
	FogData[0].HeightFalloff = InComponent->FogHeightFalloff / 1000.0f;
	FogData[1].Density = InComponent->SecondFogData.FogDensity / 1000.0f;
	FogData[1].HeightFalloff = InComponent->SecondFogData.FogHeightFalloff / 1000.0f;

	InscatteringColorCubemap = InComponent->InscatteringColorCubemap;
	InscatteringColorCubemapAngle = InComponent->InscatteringColorCubemapAngle * (PI / 180.f);
	FullyDirectionalInscatteringColorDistance = InComponent->FullyDirectionalInscatteringColorDistance;
	NonDirectionalInscatteringColorDistance = InComponent->NonDirectionalInscatteringColorDistance;
}


// SceneRendering.h
class FViewInfo : public FSceneView
{
	/** Parameters for exponential height fog. */
	FVector ExponentialFogColor;
	float FogMaxOpacity;
	FVector4 ExponentialFogParameters;
	FVector4 ExponentialFogParameters2;
	FVector4 ExponentialFogParameters3;
	
	FVector2D SinCosInscatteringColorCubemapRotation;
	UTexture* FogInscatteringColorCubemap;
	FVector FogInscatteringTextureParameters;

	/** Parameters for directional inscattering of exponential height fog. */
	bool bUseDirectionalInscattering;
	float DirectionalInscatteringExponent;
	float DirectionalInscatteringStartDistance;
	FVector InscatteringLightDirection;
	FLinearColor DirectionalInscatteringColor;
}
void FSceneRenderer::InitFogConstants()
{
	FViewInfo& View = Views[0];
	InitAtmosphereConstantsInView(View);

	if (Scene->ExponentialFogs.Num() > 0)
	{
		const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];
		
		float CollapsedFogParameter[2];
		for (int i = 0; i < FExponentialHeightFogSceneInfo::NumFogs; i++)
		{
			const float CollapsedFogParameterPower = FMath::Clamp(
				-FogInfo.FogData[i].HeightFalloff * (View.ViewMatrices.GetViewOrigin().Z - FogInfo.FogData[i].Height),
				-126.f + 1.f, // min and max exponent values for IEEE floating points (http://en.wikipedia.org/wiki/IEEE_floating_point)
				+127.f - 1.f
			);

			CollapsedFogParameter[i] = FogInfo.FogData[i].Density * FMath::Pow(2.0f, CollapsedFogParameterPower);

		}

		View.ExponentialFogParameters.X = CollapsedFogParameter[0];
		View.ExponentialFogParameters.Y = FogInfo.FogData[0].HeightFalloff;
		View.ExponentialFogParameters3.X = FogInfo.FogData[0].Density;
		View.ExponentialFogParameters3.Y = FogInfo.FogData[0].Height;

		View.ExponentialFogParameters2.X = CollapsedFogParameter[1];
		View.ExponentialFogParameters2.Y = FogInfo.FogData[1].HeightFalloff;
		View.ExponentialFogParameters2.Z = FogInfo.FogData[1].Density;
		View.ExponentialFogParameters2.W = FogInfo.FogData[1].Height;

		// View.ExponentialFogParameters.Z = FMath::Clamp( FMath::Cos(FogInfo.LightTerminatorAngle * PI/180.0f), -1.0f + DELTA, 1.0f - DELTA );
		View.ExponentialFogParameters3.Z = FogInfo.InscatteringColorCubemap ? 1.0f : 0.0f;

		View.ExponentialFogParameters.W = FogInfo.StartDistance;
		View.ExponentialFogParameters3.W = FogInfo.FogCutoffDistance;

		View.ExponentialFogColor = FVector(FogInfo.FogColor);
		View.FogMaxOpacity = FogInfo.FogMaxOpacity;

		const float InvRange = 1.0f / (FogInfo.FullyDirectionalInscatteringColorDistance - FogInfo.NonDirectionalInscatteringColorDistance);

		View.SinCosInscatteringColorCubemapRotation = FVector2D(Sin(FogInfo.InscatteringColorCubemapAngle), Cos(FogInfo.InscatteringColorCubemapAngle));
		View.FogInscatteringColorCubemap = FogInfo.InscatteringColorCubemap;
		View.FogInscatteringTextureParameters.X = InvRange;
		View.FogInscatteringTextureParameters.Y = -InvRange * FogInfo.NonDirectionalInscatteringColorDistance;
		View.FogInscatteringTextureParameters.Z = !FogInfo.InscatteringColorCubemap ? 1.0 : FogInfo.InscatteringColorCubemap->GetNumMips();
	}
}


// FogRendering.h,  FogRendering.cpp
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters,)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters2)
	SHADER_PARAMETER(FVector4, ExponentialFogColorParameter)
	SHADER_PARAMETER(FVector4, ExponentialFogParameters3)
	SHADER_PARAMETER(FVector4, InscatteringLightDirection) // non negative DirectionalInscatteringStartDistance in .W
	SHADER_PARAMETER(FVector4, DirectionalInscatteringColor)
	SHADER_PARAMETER(FVector2D, SinCosInscatteringColorCubemapRotation)
	SHADER_PARAMETER(FVector, FogInscatteringTextureParameters)
	SHADER_PARAMETER(float, ApplyVolumetricFog)
	SHADER_PARAMETER_TEXTURE(TextureCube, FogInscatteringColorCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, FogInscatteringColorSampler)
	SHADER_PARAMETER_TEXTURE(Texture3D, IntegratedLightScattering)
	SHADER_PARAMETER_SAMPLER(SamplerState, IntegratedLightScatteringSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FFogUniformParameters, "FogStruct");
void SetupFogUniformParameters(const FViewInfo& View, FFogUniformParameters& OutParameters)
{
	// Exponential Height Fog
	{
		const FTexture* Cubemap = GWhiteTextureCube;

		if (View.FogInscatteringColorCubemap)
		{
			Cubemap = View.FogInscatteringColorCubemap->Resource;
		}

		OutParameters.ExponentialFogColorParameter = FVector4(View.ExponentialFogColor, 1.0f - View.FogMaxOpacity);
		OutParameters.ExponentialFogParameters = View.ExponentialFogParameters;
		OutParameters.ExponentialFogParameters2 = View.ExponentialFogParameters2;
		OutParameters.ExponentialFogParameters3 = View.ExponentialFogParameters3;
		OutParameters.FogInscatteringColorCubemap = Cubemap->TextureRHI;
		OutParameters.FogInscatteringColorSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		OutParameters.SinCosInscatteringColorCubemapRotation = View.SinCosInscatteringColorCubemapRotation;
		OutParameters.FogInscatteringTextureParameters = View.FogInscatteringTextureParameters;
		//OutParameters.InscatteringLightDirection = View.InscatteringLightDirection;
		//OutParameters.InscatteringLightDirection.W = View.bUseDirectionalInscattering ? FMath::Max(0.f, View.DirectionalInscatteringStartDistance) : -1.f;
		//OutParameters.DirectionalInscatteringColor = FVector4(FVector(View.DirectionalInscatteringColor), FMath::Clamp(View.DirectionalInscatteringExponent, 0.000001f, 1000.0f));
	}
}



// FogRendering.cpp
TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha> 	// FinalColor.rgb = Src.rgb + Dst.rgb * Src.a;

void ExponentialPixelMain(
	float2 TexCoord : TEXCOORD0,
	float3 ScreenVector : TEXCOORD1,
	float4 SVPos : SV_POSITION,
	out float4 OutColor : SV_Target0
	)
{
	float DeviceZ = Texture2DSampleLevel(SceneTexturesStruct.SceneDepthTexture, SceneTexturesStruct.SceneDepthTextureSampler, TexCoord, 0).r;
	float SceneDepth = ConvertFromDeviceZ(DeviceZ);
	float3 WorldPositionRelativeToCamera = ScreenVector.xyz * SceneDepth;

	float4 FogInscatteringAndOpacity = GetExponentialHeightFog(WorldPositionRelativeToCamer, 0);
	
	OutColor = float4(FogInscatteringAndOpacity.rgb, FogInscatteringAndOpacity.w);
}

half4 GetExponentialHeightFog(float3 WorldPositionRelativeToCamera, float ExcludeDistance)
{
	float RayOriginTerms = FogStruct.ExponentialFogParameters.x;		// CollapsedFogParameter[0]
	float RayOriginTermsSecond = FogStruct.ExponentialFogParameters2.x;	// CollapsedFogParameter[1]
	float falloff0 = FogStruct.ExponentialFogParameters.y;
	float falloff1 = FogStruct.ExponentialFogParameters2.y;
	float density0 = FogStruct.ExponentialFogParameters3.x;
	float density1 = FogStruct.ExponentialFogParameters2.z;
	float height0 = FogStruct.ExponentialFogParameters3.y;
	float height1 = FogStruct.ExponentialFogParameters2.w;
	const half MinDstWeight = FogStruct.ExponentialFogColorParameter.w;
	float startDistance = FogStruct.ExponentialFogParameters.w;
	float cutoffDistance = FogStruct.ExponentialFogParameters3.w;
	
	float3 CameraToReceiver = WorldPositionRelativeToCamera;
	float CameraToReceiverLengthSqr = dot(CameraToReceiver, CameraToReceiver);
	float CameraToReceiverLengthInv = rsqrt(CameraToReceiverLengthSqr);
	float CameraToReceiverLength = CameraToReceiverLengthSqr * CameraToReceiverLengthInv;
	half3 CameraToReceiverNormalized = CameraToReceiver * CameraToReceiverLengthInv;

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
		RayOriginTerms       = density0 * exp2( -max(falloff0 * (ExclusionIntersectionZ - height0), -127.0f) );
		RayOriginTermsSecond = density1 * exp2( -max(falloff1*  (ExclusionIntersectionZ - height1), -127.0f) );
	}

	// Calculate the "shared" line integral (this term is also used for the directional light inscattering) by adding the two line integrals together (from two different height falloffs and densities)
	float ExponentialHeightLineIntegralShared = 
		  CalculateLineIntegralShared(falloff0, RayDirectionZ, RayOriginTerms) 
		+ CalculateLineIntegralShared(falloff1, RayDirectionZ, RayOriginTermsSecond);

	float ExponentialHeightLineIntegral = ExponentialHeightLineIntegralShared * RayLength;

	half3 InscatteringColor = ComputeInscatteringColor(CameraToReceiver, CameraToReceiverLength);
	half3 DirectionalInscattering = 0;

	// Calculate the amount of light that made it through the fog using the transmission equation
	half DstWeight = max(exp2(-ExponentialHeightLineIntegral), MinDstWeight);

	// Fog color is unused when additive / modulate blend modes are active.
	#if (MATERIALBLENDING_ADDITIVE || MATERIALBLENDING_MODULATE)
		half3 FogColor = 0.0;
	#else
		half3 FogColor = (InscatteringColor) * (1 - DstWeight) + DirectionalInscattering;
	#endif

	return half4(FogColor, DstWeight);
	// FinalColor.rgb = lerp(InscatteringColor, Dst.rgb, DstWeight) + DirectionalInscattering
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

float3 ComputeInscatteringColor(float3 CameraToReceiver, float CameraToReceiverLength)
{
	half3 Inscattering = FogStruct.ExponentialFogColorParameter.xyz;
	half3 DirectionalInscattering = 0;

#if SUPPORT_FOG_INSCATTERING_TEXTURE
	if (FogStruct.ExponentialFogParameters3.z > 0)
	{
		float FadeAlpha = saturate(
			CameraToReceiverLength * FogStruct.FogInscatteringTextureParameters.x 
			+ FogStruct.FogInscatteringTextureParameters.y  );
		float3 CubemapLookupVector = CameraToReceiver;
		// Rotate around Z axis
		CubemapLookupVector.xy = float2(
			dot(CubemapLookupVector.xy, float2(FogStruct.SinCosInscatteringColorCubemapRotation.y, -FogStruct.SinCosInscatteringColorCubemapRotation.x)), 
			dot(CubemapLookupVector.xy, FogStruct.SinCosInscatteringColorCubemapRotation.xy)  );
		
		float3 DirectionalColor = TextureCubeSampleLevel(FogStruct.FogInscatteringColorCubemap, FogStruct.FogInscatteringColorSampler, CubemapLookupVector, 0).xyz;
		float3 NonDirectionalColor = TextureCubeSampleLevel(FogStruct.FogInscatteringColorCubemap, FogStruct.FogInscatteringColorSampler, CubemapLookupVector, FogStruct.FogInscatteringTextureParameters.z).xyz;
		Inscattering *= lerp(NonDirectionalColor, DirectionalColor, FadeAlpha);
	}
#endif

	return Inscattering;
}


float4 CombineVolumetricFog(float4 GlobalFog, float3 VolumeUV, )
{
	float4 VolumetricFogLookup = float4(0, 0, 0, 1);

	if (FogStruct.ApplyVolumetricFog > 0)
	{
		VolumetricFogLookup = Texture3DSampleLevel(FogStruct.IntegratedLightScattering, SharedIntegratedLightScatteringSampler, VolumeUV, 0);
	}

	// Visualize depth distribution
	//VolumetricFogLookup.rgb += .1f * frac(min(ZSlice, 1.0f) / View.VolumetricFogInvGridSize.z);

	return float4(VolumetricFogLookup.rgb + GlobalFog.rgb * VolumetricFogLookup.a, VolumetricFogLookup.a * GlobalFog.a);
}