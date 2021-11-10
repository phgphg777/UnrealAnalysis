
class FVolumetricFogMaterialSetupCS : public FGlobalShader {
	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER(FLinearColor, GlobalAlbedo)	// (1.0f / 10000.0f) * FLinearColor(Component->VolumetricFogAlbedo)
		SHADER_PARAMETER(FLinearColor, GlobalEmissive)	// Component->VolumetricFogEmissive
		SHADER_PARAMETER(float, GlobalExtinctionScale)	// Component->VolumetricFogExtinctionScale
		SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FFogUniformParameters, Fog)
		SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVBufferA)
		SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float4>, RWVBufferB)
	END_SHADER_PARAMETER_STRUCT()
}

float ExponentialFogDensity; 		// Component->FogDensity / 1000.0f
float ExponentialFogFalloff; 		// Component->FogHeightFalloff / 1000.0f
float ExponentialFogBaseHeight;  	// Component->GetComponentLocation().Z

void MaterialSetupCS( 
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	uint3 GridCoordinate = DispatchThreadId;
	float VoxelOffset = 0.5;
	float3 WorldPosition = ComputeCellWorldPosition(GridCoordinate, VoxelOffset);
	float VoxelHeight = WorldPosition.z - ExponentialFogBaseHeight;
	float VoxelFogDensity = 0.5 * ExponentialFogDensity * exp2(-ExponentialFogFalloff * VoxelHeight);
	
	float VoxelExtinction = VoxelFogDensity * GlobalExtinctionScale;
	float3 VoxelScattering = GlobalAlbedo * VoxelExtinction;
	float3 VoxelEmissive = GlobalEmissive;

	if (all((int3)GridCoordinate < VolumetricFog.GridSizeInt))
	{
		RWVBufferA[GridCoordinate] = float4(VoxelScattering, VoxelExtinction);
		RWVBufferB[GridCoordinate] = float4(VoxelEmissive, 0);
	}
}

float UseFogColorForSky; // Component->bOverrideLightColorsWithFogInscatteringColors
float UseFogColorForSun; /* Component->bOverrideLightColorsWithFogInscatteringColors && 
							Component->InscatteringColorCubemap == nullptr && 
							Scene->AtmosphereLights[0] != nullptr */
float3 SunScatteringColor; // Component->DirectionalInscatteringColor * Scene->AtmosphereLights[0]->Proxy->GetColor().ComputeLuminance()

void LightScatteringCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	float3 LightScattering = 0;
	
	uint3 GridCoordinate = DispatchThreadId;
	float3 CellOffset = FrameJitterOffsets[0].xyz;
	float SceneDepth;
	float3 WorldPosition = ComputeCellWorldPosition(GridCoordinate, CellOffset, SceneDepth);
	float CameraVectorLength = length(WorldPosition - View.WorldCameraOrigin);
	float3 CameraVector = (WorldPosition - View.WorldCameraOrigin) / CameraVectorLength;

	if (ForwardLightData.HasDirectionalLight)
	{
		float ShadowFactor = 1;
		if (UseDirectionalLightShadowing > 0) // ULightComponentBase::bCastVolumetricShadow
		{
			ShadowFactor *= ComputeDirectionalLightStaticShadowing(WorldPosition);
			ShadowFactor *= ComputeDirectionalLightDynamicShadowing(WorldPosition, SceneDepth);
		}

		float3 SunLighting = (UseFogColorForSun == 0) ? ForwardLightData.DirectionalLightColor :
			SunScatteringColor * Luminance(ForwardLightData.DirectionalLightColor); // Too Bright, repeated luminance !!
			
		LightScattering += SunLighting * ShadowFactor
			* PhaseFunction(PhaseG, dot(ForwardLightData.DirectionalLightDirection, -CameraVector))
			* ForwardLightData.DirectionalLightVolumetricScatteringIntensity; // ULightComponentBase::VolumetricScatteringIntensity
	}

	if (SkyLightVolumetricScatteringIntensity > 0) // ULightComponentBase::VolumetricScatteringIntensity
	{
		float3 SkyLighting;

		FTwoBandSHVector RotatedHGZonalHarmonic;
		RotatedHGZonalHarmonic.V = float4(1.0f, CameraVector.y, CameraVector.z, CameraVector.x) * float4(1.0f, PhaseG, PhaseG, PhaseG);
		
		if (UseFogColorForSky == 0)
		{
			FTwoBandSHVectorRGB SkyIrradianceSH;
			SkyIrradianceSH.R.V = SkySH[0] / PI;
			SkyIrradianceSH.G.V = SkySH[1] / PI;
			SkyIrradianceSH.B.V = SkySH[2] / PI;
			SkyLighting = View.SkyLightColor.rgb * max(DotSH(SkyIrradianceSH, RotatedHGZonalHarmonic), 0);
		}
		else
		{
			float3 HeightFogColor = ComputeInscatteringColor(CameraVector, CameraVectorLength);
			
			FTwoBandSHVectorRGB SkyIrradianceSH;
			SkyIrradianceSH.R.V = float4(SHAmbientFunction(), 0, 0, 0);
			SkyIrradianceSH.G.V = float4(SHAmbientFunction(), 0, 0, 0);
			SkyIrradianceSH.B.V = float4(SHAmbientFunction(), 0, 0, 0);
			SkyLighting = HeightFogColor * max(DotSH(SkyIrradianceSH, RotatedHGZonalHarmonic), 0);
		}

		float ShadowFactor = ComputeSkyVisibility(WorldPosition, ComputeVolumetricLightmapBrickTextureUVs(WorldPosition));
		LightScattering += SkyLighting * ShadowFactor * SkyLightVolumetricScatteringIntensity;
	}

	if (all(GridCoordinate < VolumetricFog.GridSizeInt))
	{
		
		RWLightScattering[GridCoordinate] = float4(LightScattering * Scattering + Emissive, Extinction);
	}
}

void FinalIntegrationCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	uint3 GridCoordinate = DispatchThreadId;

	float3 AccumulatedLighting = 0;
	float AccumulatedTransmittance = 1.0f;
	float3 PreviousSliceWorldPosition = View.WorldCameraOrigin;

	for (uint LayerIndex = 0; LayerIndex < VolumetricFog.GridSizeInt.z; LayerIndex++)
	{
		uint3 LayerCoordinate = uint3(GridCoordinate.xy, LayerIndex);
		float4 ScatteringAndExtinction = DecodeHDR(LightScattering[LayerCoordinate]);

		float3 LayerWorldPosition = ComputeCellWorldPosition(LayerCoordinate, .5f);
		float StepLength = length(LayerWorldPosition - PreviousSliceWorldPosition);
		PreviousSliceWorldPosition = LayerWorldPosition;

		float Transmittance = exp(-ScatteringAndExtinction.w * StepLength);

		#if 1
			float3 ScatteringIntegratedOverSlice = (ScatteringAndExtinction.rgb * (1 - * Transmittance)) / ScatteringAndExtinction.w;
			AccumulatedLighting += ScatteringIntegratedOverSlice * AccumulatedTransmittance;
		#else
			AccumulatedLighting += ScatteringAndExtinction.rgb * AccumulatedTransmittance * StepLength;
		#endif
		
		AccumulatedTransmittance *= Transmittance;

		RWIntegratedLightScattering[LayerCoordinate] = float4(AccumulatedLighting, AccumulatedTransmittance);
	}
}
