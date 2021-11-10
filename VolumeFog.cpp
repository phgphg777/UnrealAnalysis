//DATA///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SceneRendering.h (FViewInfo)
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FVolumetricFogGlobalData,) 
	SHADER_PARAMETER(FIntVector, GridSizeInt)
	SHADER_PARAMETER(FVector, GridSize)
	SHADER_PARAMETER(FVector, GridZParams)
	//SHADER_PARAMETER(FVector2D, SVPosToVolumeUV)
	SHADER_PARAMETER(FIntPoint, FogGridToPixelXY)
	SHADER_PARAMETER(float, MaxDistance)
	SHADER_PARAMETER(FVector, HeightFogInscatteringColor)
	SHADER_PARAMETER(FVector, HeightFogDirectionalLightInscatteringColor)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
// VolumetricFog.cpp
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumetricFogGlobalData, "VolumetricFog");
void FDeferredShadingSceneRenderer::SetupVolumetricFog()
{
	const FScene* Scene = (FScene*)View.Family->Scene;
	const FExponentialHeightFogSceneInfo& FogInfo = Scene->ExponentialFogs[0];
	const FIntVector VolumetricFogGridSize = GetVolumetricFogGridSize(View.ViewRect.Size());

	FVolumetricFogGlobalData Parameters;
	Parameters.GridSizeInt = VolumetricFogGridSize;
	Parameters.GridSize = FVector(VolumetricFogGridSize);
	FVector ZParams = GetVolumetricFogGridZParams(View.NearClippingDistance, FogInfo.VolumetricFogDistance, VolumetricFogGridSize.Z);
	Parameters.GridZParams = ZParams;
	Parameters.FogGridToPixelXY = FIntPoint(GVolumetricFogGridPixelSize, GVolumetricFogGridPixelSize);
	Parameters.MaxDistance = FogInfo.VolumetricFogDistance;
	Parameters.HeightFogInscatteringColor = View.ExponentialFogColor;
	Parameters.HeightFogDirectionalLightInscatteringColor 
	 	= (View.bUseDirectionalInscattering && !View.FogInscatteringColorCubemap)
		? FVector(View.DirectionalInscatteringColor) : FVector::ZeroVector;

	View.VolumetricFogResources.VolumetricFogGlobalData = TUniformBufferRef<FVolumetricFogGlobalData>::CreateUniformBufferImmediate(GlobalData, UniformBuffer_SingleFrame);
}

// VolumetricFogShared.h
struct FVolumetricFogIntegrationParameterData
{
	bool bTemporalHistoryIsValid;
	TArray<FVector4, TInlineAllocator<16>> FrameJitterOffsetValues;
	/* Actully, not used !!
	const FRDGTexture* VBufferA;
	const FRDGTexture* VBufferB;
	const FRDGTextureUAV* VBufferA_UAV;
	const FRDGTextureUAV* VBufferB_UAV;
	const FRDGTexture* LightScattering;
	const FRDGTextureUAV* LightScatteringUAV;
	*/
};
class FVolumetricFogIntegrationParameters
{
	FShaderUniformBufferParameter VolumetricFogData; // FVolumetricFogGlobalData
	/* Actully, not used !!
	FRWShaderParameter VBufferA;
	FRWShaderParameter VBufferB;
	FRWShaderParameter LightScattering;
	FRWShaderParameter IntegratedLightScattering;
	FShaderResourceParameter IntegratedLightScatteringSampler;
	*/
	FShaderParameter UnjitteredClipToTranslatedWorld;
	FShaderParameter UnjitteredPrevWorldToClip;
	FShaderParameter FrameJitterOffsets;
	FShaderParameter HistoryWeight;
	FShaderParameter HistoryMissSuperSampleCount;
}
template<typename ShaderRHIParamRef>
void FVolumetricFogIntegrationParameters::Set(
	FRHICommandList& RHICmdList, 
	const ShaderRHIParamRef& ShaderRHI, 
	const FViewInfo& View, 
	const FVolumetricFogIntegrationParameterData& IntegrationData)
{
	VolumetricFogData = View.VolumetricFogResources.VolumetricFogGlobalData; // SetUniformBufferParameter(RHICmdList, ShaderRHI, VolumetricFogData, View.VolumetricFogResources.VolumetricFogGlobalData);
	...
	HistoryWeight = IntegrationData.bTemporalHistoryIsValid ? GVolumetricFogHistoryWeight : 0.0f;
	HistoryMissSuperSampleCount = GVolumetricFogHistoryMissSupersampleCount
}


//CPU////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FVolumetricFogMaterialSetupCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, GlobalAlbedo)
		SHADER_PARAMETER(FLinearColor, GlobalEmissive)
		SHADER_PARAMETER(float, GlobalExtinctionScale)
		SHADER_PARAMETER_STRUCT_REF(FFogUniformParameters, FogUniformParameters)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVBufferA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVBufferB)
	END_SHADER_PARAMETER_STRUCT()
private:
	FVolumetricFogIntegrationParameters VolumetricFogParameters;
};
IMPLEMENT_SHADER_TYPE(, FVolumetricFogMaterialSetupCS, TEXT("/Engine/Private/VolumetricFog.usf"), TEXT("MaterialSetupCS"), SF_Compute);

class TVolumetricFogLightScatteringCS : public FGlobalShader
{
	class FTemporalReprojection			: SHADER_PERMUTATION_BOOL("USE_TEMPORAL_REPROJECTION");
	class FDistanceFieldSkyOcclusion	: SHADER_PERMUTATION_BOOL("DISTANCE_FIELD_SKY_OCCLUSION");
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VBufferA) // <- FDeferredShadingSceneRenderer::VoxelizeFogVolumePrimitives() <- FVolumetricFogMaterialSetupCS
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VBufferB) // <- FDeferredShadingSceneRenderer::VoxelizeFogVolumePrimitives() <- FVolumetricFogMaterialSetupCS
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LocalShadowedLightScattering) // <- FDeferredShadingSceneRenderer::RenderLocalLightsForVolumetricFog()
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, LightFunctionTexture) // // <- FDeferredShadingSceneRenderer::RenderLightFunctionForVolumetricFog()
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2D, RWLightScattering)
	END_SHADER_PARAMETER_STRUCT()
private:
	// FShaderResourceParameter LocalShadowedLightScattering;
	FShaderResourceParameter LightScatteringHistory; // <- View.ViewState->LightScatteringHistory
	FShaderResourceParameter LightScatteringHistorySampler; // in TVolumetricFogLightScatteringCS::SetParameters()
	// FShaderResourceParameter LightFunctionTexture; 
	FShaderResourceParameter LightFunctionSampler; // in TVolumetricFogLightScatteringCS::SetParameters()

	FShaderParameter DirectionalLightFunctionWorldToShadow;
	FShaderParameter StaticLightingScatteringIntensity;
	FShaderParameter SkyLightUseStaticShadowing;
	FShaderParameter SkyLightVolumetricScatteringIntensity;
	FShaderParameter SkySH;
	FShaderParameter PhaseG;
	FShaderParameter InverseSquaredLightDistanceBiasScale;
	FShaderParameter UseHeightFogColors;
	FShaderParameter UseDirectionalLightShadowing;

	FAOParameters AOParameters;
	FGlobalDistanceFieldParameters GlobalDistanceFieldParameters;
	FVolumetricFogIntegrationParameters VolumetricFogParameters;
};
IMPLEMENT_GLOBAL_SHADER(TVolumetricFogLightScatteringCS, "/Engine/Private/VolumetricFog.usf", "LightScatteringCS", SF_Compute);

class FVolumetricFogFinalIntegrationCS : public FGlobalShader
{
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture3D<float4>, LightScattering)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWIntegratedLightScattering)
	END_SHADER_PARAMETER_STRUCT()
private:
	FVolumetricFogIntegrationParameters VolumetricFogParameters;
};
IMPLEMENT_SHADER_TYPE(, FVolumetricFogFinalIntegrationCS, TEXT("/Engine/Private/VolumetricFog.usf"), TEXT("FinalIntegrationCS"), SF_Compute);


//First Pass///////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sigma_a : Absorption Coeff
// sigma_s : Scattering Coeff
// sigma_t = sigma_a + sigma_s : Extinction Coeff
// sigma_s / sigma_t : Single Scattering Albedo

RWTexture3D<float4> RWVBufferA;
RWTexture3D<float4> RWVBufferB;

[numthreads(4, 4, 4)]
void MaterialSetupCS( 
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	uint3 GridCoordinate = DispatchThreadId;
	float3 WorldPosition = ComputeCellWorldPosition(GridCoordinate, 0.5f, float());
	float HeightFogDensity = FogStruct.density0 * exp2(-FogStruct.falloff0 * (WorldPosition.z - FogStruct.height0))
						+ FogStruct.density1 * exp2(-FogStruct.falloff1 * (WorldPosition.z - FogStruct.height1));
	float Extinction = HeightFogDensity * GlobalExtinctionScale * 0.5f;
	
	if (all((int3)GridCoordinate < VolumetricFog.GridSizeInt))
	{
		RWVBufferA[GridCoordinate] = float4(Extinction * GlobalAlbedo, Extinction);		// <-------- HeightFogDensity & VolumetricFogExtinctionScale & VolumetricFogAlbedo
		RWVBufferB[GridCoordinate] = float4(GlobalEmissive, 0);
	}
}


// VolumetricFogVoxelization
TStaticBlendState<
		CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
		CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>
FinalColor[0].rgb = Src.rgb + Dst.rgb;
FinalColor[0].a = Src.a + Dst.a;
FinalColor[1].rgb = Src.rgb + Dst.rgb;
FinalColor[1].a = Src.a + Dst.a;

void VoxelizePS(
	FVoxelizeVolumePrimitiveGSToPS Interpolants,
	in float4 SvPosition : SV_Position,
	out float4 OutVBufferA : SV_Target0,
	out float4 OutVBufferB : SV_Target1
	)
{
	ResolvedView = ResolveView();
	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(Interpolants.FactoryInterpolants, SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;
	CalcMaterialParameters(MaterialParameters, PixelMaterialInputs, SvPosition, true);
	float3 EmissiveColor = clamp(GetMaterialEmissiveRaw(PixelMaterialInputs), 0.0f, 65000.0f);
	float3 Albedo = GetMaterialBaseColor(PixelMaterialInputs);
	float Extinction = clamp(GetMaterialOpacityRaw(PixelMaterialInputs), 0.0f, 65000.0f);

	float FadeStart = .6f;
	float SliceFadeAlpha = 1 - saturate((SvPosition.w / VolumetricFog.MaxDistance - FadeStart) / (1 - FadeStart));
	float Scale = 0.01f * pow(SliceFadeAlpha, 3);

	// Would be faster to branch around the whole material evaluation, but that would cause divergent flow control for gradient operations
	Scale *= ComputeVolumeShapeMasking(MaterialParameters.AbsoluteWorldPosition, MaterialParameters.PrimitiveId, Interpolants.FactoryInterpolants);

	OutVBufferA = float4(Albedo * Extinction * Scale, Extinction * Scale);
	OutVBufferB = float4(EmissiveColor * Scale, 0);
}

//Main Pass////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture3D<float4> VBufferA;
Texture3D<float4> VBufferB;
Texture3D<float4> LightScatteringHistory;
SamplerState LightScatteringHistorySampler;
Texture3D<float4> LocalShadowedLightScattering;
RWTexture3D<float4> RWLightScattering;

[numthreads(8, 8, 1)]
void LightScatteringCS(uint3 DispatchThreadId : SV_DispatchThreadID)
{
	uint3 GridCoordinate = DispatchThreadId;
	float3 LightScattering = 0;					// <-------- Lighting + shadowing + Phase + VolumetricScatteringIntensity
	uint NumSuperSamples = 1;

#if USE_TEMPORAL_REPROJECTION
	float3 HistoryUV = ComputeVolumeUV( ComputeCellWorldPosition(GridCoordinate, .5f), UnjitteredPrevWorldToClip );
	float HistoryAlpha = (any(HistoryUV < 0) || any(HistoryUV > 1)) ? 0 : HistoryWeight;
	NumSuperSamples = HistoryAlpha < .001f && all(GridCoordinate < VolumetricFog.GridSizeInt) ? HistoryMissSuperSampleCount : 1;
#endif

	for (uint SampleIndex = 0; SampleIndex < NumSuperSamples; SampleIndex++)
	{
		float3 CellOffset = FrameJitterOffsets[SampleIndex].xyz;
		LightScattering += CalculateLightScattering(GridCoordinate, CellOffset) / (float) NumSuperSamples;
	}
	LightScattering += LocalShadowedLightScattering[GridCoordinate].xyz;
	 
	float4 ScatteringAndExtinction = float4(LightScattering * VBufferA[GridCoordinate].xyz + VBufferB[GridCoordinate].xyz, VBufferA[GridCoordinate].w);

#if USE_TEMPORAL_REPROJECTION
	if (HistoryAlpha > 0)
	{
		float4 HistoryScatteringAndExtinction = Texture3DSampleLevel(LightScatteringHistory, LightScatteringHistorySampler, HistoryUV, 0);
		ScatteringAndExtinction = lerp(ScatteringAndExtinction, HistoryScatteringAndExtinction, HistoryAlpha);
	}
#endif

	if (all(GridCoordinate < VolumetricFog.GridSizeInt))
	{
		RWLightScattering[GridCoordinate] = ScatteringAndExtinction;
	}
}
//Last Pass////////////////////////////////////////////////////////////////////////////////////////////////////////////////
Texture3D<float4> LightScattering;
RWTexture3D<float4> RWIntegratedLightScattering;

[numthreads(8, 8, 1)]
void FinalIntegrationCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	uint3 GridCoordinate = DispatchThreadId;

	float3 AccumulatedLighting = 0;
	float AccumulatedTransmittance = 1.0f;
	float3 PreviousSliceWorldPosition = View.WorldCameraOrigin;

	for (uint z = 0; z < VolumetricFog.GridSizeInt.z; z++)
	{
		uint3 LayerCoordinate = uint3(GridCoordinate.xy, z);
		float4 ScatteringAndExtinction = LightScattering[LayerCoordinate];

		float3 LayerWorldPosition = ComputeCellWorldPosition(LayerCoordinate, .5f);
		float StepLength = length(LayerWorldPosition - PreviousSliceWorldPosition);
		PreviousSliceWorldPosition = LayerWorldPosition;

		float Transmittance = exp(-ScatteringAndExtinction.w * StepLength);
		float mul = bEnergyConserve ? (1 - Transmittance) / ScatteringAndExtinction.w : StepLength;
		
		AccumulatedLighting += ScatteringAndExtinction.rgb * AccumulatedTransmittance * mul;
		AccumulatedTransmittance *= Transmittance;	
		
		RWIntegratedLightScattering[LayerCoordinate] = float4(AccumulatedLighting, AccumulatedTransmittance);
	}
}











float3 CalculateLightScattering(uint3 GridCoordinate, float3 CellOffset)
{
	float3 LightScattering = 0;

	float SceneDepth;
	float3 WorldPosition = ComputeCellWorldPosition(GridCoordinate, CellOffset, SceneDepth);
	float CameraVectorLength = length(WorldPosition - View.WorldCameraOrigin);
	float3 CameraVector = (WorldPosition - View.WorldCameraOrigin) / CameraVectorLength;


	uint GridIndex = ComputeLightGridCellIndex(GridCoordinate.xy * VolumetricFog.FogGridToPixelXY, SceneDepth, 0);
	const FCulledLightsGridData CulledLightsGrid = GetCulledLightsGrid(GridIndex, 0);
	float CellRadius = length(WorldPosition - ComputeCellWorldPosition(GridCoordinate + uint3(1, 1, 1), CellOffset));
	float DistanceBias = max(CellRadius * InverseSquaredLightDistanceBiasScale, 1);	
	for (uint LocalLightListIndex = 0; LocalLightListIndex < CulledLightsGrid.NumLocalLights; LocalLightListIndex++)
	{
		const FLocalLightData LocalLight = GetLocalLightData(CulledLightsGrid.DataStartIndex + LocalLightListIndex, 0);
		float VolumetricScatteringIntensity = f16tof32(asuint(LocalLight.SpotAnglesAndSourceRadiusPacked.w) >> 16);
		if (VolumetricScatteringIntensity > 0)
		{
			FDeferredLightData LightData = ConvertToDeferredLightData(LocalLight);	
			float3 LightColor = LightData.Color;
			float3 L = 0;
			float3 ToLight = 0;
			float LightMask = GetLocalLightAttenuation(WorldPosition, LightData, ToLight, L);
			float Lighting;	
			FCapsuleLight Capsule = GetCapsule(ToLight, LightData);
			Capsule.DistBiasSqr = DistanceBias * DistanceBias;
			Lighting = IntegrateLight(Capsule, LightData.bInverseSquared);
			float CombinedAttenuation = Lighting * LightMask;

			LightScattering += LightColor * (PhaseFunction(PhaseG, dot(L, -CameraVector)) * CombinedAttenuation * VolumetricScatteringIntensity);
		}
	}

	if (ForwardLightData.HasDirectionalLight)
	{
		float ShadowFactor = GetLightFunction(WorldPosition);
		
		if (UseDirectionalLightShadowing > 0) // UDirectionalLightComponent::bCastVolumetricShadow
		{
			ShadowFactor *= ComputeDirectionalLightStaticShadowing(WorldPosition);
			ShadowFactor *= ComputeDirectionalLightDynamicShadowing(WorldPosition, SceneDepth);
		}

		float3 DirectionalLightColor;
		if (UseHeightFogColors > 0) // UExponentialHeightFogComponent::bOverrideLightColorsWithFogInscatteringColors
		{
			// DirectionalLightColor = VolumetricFog.HeightFogDirectionalLightInscatteringColor;  ???
			// [Luminance(ForwardLightData.DirectionalLightColor)] is already reflected in [VolumetricFog.HeightFogDirectionalLightInscatteringColor] !!!
			// See FSceneRenderer::InitFogConstants() !!!
			DirectionalLightColor = VolumetricFog.HeightFogDirectionalLightInscatteringColor * Luminance(ForwardLightData.DirectionalLightColor);
		}
		else
		{
			DirectionalLightColor = ForwardLightData.DirectionalLightColor;
		}

		float DirectionalLightScale = 
			ForwardLightData.DirectionalLightVolumetricScatteringIntensity // UDirectionalLightComponent::VolumetricScatteringIntensity
			* PhaseFunction(PhaseG, dot(ForwardLightData.DirectionalLightDirection, -CameraVector))
			* ShadowFactor;

		LightScattering += DirectionalLightColor * DirectionalLightScale;
	}


	return LightScattering;
}
