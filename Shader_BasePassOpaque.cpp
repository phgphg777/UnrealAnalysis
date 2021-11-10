
#define LIGHTMAP_VT_ENABLED 0
#define USES_AO_MATERIAL_MASK 0 // FHLSLMaterialTranslator::bUsesAOMaterialMask
#define OUTPUT_PIXEL_DEPTH_OFFSET 0
#define USE_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS 0 
#define MATERIAL_SHADINGMODEL_THIN_TRANSLUCENT 0
#define MATERIAL_SHADINGMODEL_SINGLELAYERWATER 0
#define NUM_VIRTUALTEXTURE_SAMPLES 0
#define GBUFFER_HAS_VELOCITY 0
#define GBUFFER_HAS_TANGENT 0
#define WRITES_VELOCITY_TO_GBUFFER 0
#define EARLY_Z_PASS_ONLY_MATERIAL_MASKING 0 // TEXT("r.EarlyZPassOnlyMaterialMasking")
#define USE_DBUFFER 0
#define NEEDS_BASEPASS_VERTEX_FOGGING 0
#define NEEDS_BASEPASS_PIXEL_FOGGING 0
#define NEEDS_BASEPASS_PIXEL_VOLUMETRIC_FOGGING 0
#define SIMPLE_FORWARD_DIRECTIONAL_LIGHT 0
#define MATERIAL_NORMAL_CURVATURE_TO_ROUGHNESS 0 // UMaterial::bNormalCurvatureToRoughness (default 0)
#define TRANSLUCENCY_LIGHTING_SURFACE_FORWARDSHADING 0 // UMaterial::TranslucencyLightingMode == TLM_SurfacePerPixelLighting
#define USE_DEVELOPMENT_SHADERS 0 // TEXT("r.CompileShadersForDevelopment")
#define NUM_MATERIAL_OUTPUTS_GETBENTNORMAL 0 // No BentNormal CostumOut Node 
#define USE_EDITOR_COMPOSITING 0


float3 ComputeVolumetricLightmapBrickTextureUVs(float3 WorldPosition)
{
	// Compute indirection UVs from world position
	float3 IndirectionVolumeUVs = clamp(WorldPosition * View.VolumetricLightmapWorldToUVScale + View.VolumetricLightmapWorldToUVAdd, 0.0f, .99f);
	float3 IndirectionTextureTexelCoordinate = IndirectionVolumeUVs * View.VolumetricLightmapIndirectionTextureSize;
	float4 BrickOffsetAndSize = View.VolumetricLightmapIndirectionTexture.Load(int4(IndirectionTextureTexelCoordinate, 0));

	float PaddedBrickSize = View.VolumetricLightmapBrickSize + 1;
	return (BrickOffsetAndSize.xyz * PaddedBrickSize + frac(IndirectionTextureTexelCoordinate / BrickOffsetAndSize.w) * View.VolumetricLightmapBrickSize + .5f) * View.VolumetricLightmapBrickTexelSize;
}


// Whether to fetch primitive values (eg LocalToWorld) by dynamically indexing a scene-wide buffer, 
// or to reference a single Primitive uniform buffer (thus, an uniform buffer per primitive)
#define VF_SUPPORTS_PRIMITIVE_SCENE_DATA (FVertexFactoryType::bSupportsPrimitiveIdStream && bUseGPUScene)
#define VF_USE_PRIMITIVE_SCENE_DATA VF_SUPPORTS_PRIMITIVE_SCENE_DATA

#if VF_USE_PRIMITIVE_SCENE_DATA
	// Must match FPrecomputedLightingUniformParameters in C++
	struct FLightmapSceneData
	{
		float4 StaticShadowMapMasks;
		float4 InvUniformPenumbraSizes;
		float4 ShadowMapCoordinateScaleBias;
		float4 LightMapCoordinateScaleBias;
		float4 LightMapScale[2];
		float4 LightMapAdd[2];
		uint4 LightmapVTPackedPageTableUniform[2];
		uint4 LightmapVTPackedUniform[5];
	};

	// Stride of a single lightmap data entry in float4's, must match C++
	#define LIGHTMAP_SCENE_DATA_STRIDE 15

	// Fetch from scene lightmap data buffer
	FLightmapSceneData GetLightmapData(uint LightmapDataIndex) 
	{
		uint LightmapDataBaseOffset = LightmapDataIndex * LIGHTMAP_SCENE_DATA_STRIDE;
		
		FLightmapSceneData LightmapData;
		LightmapData.StaticShadowMapMasks = View.LightmapSceneData[LightmapDataBaseOffset + 0];
		LightmapData.InvUniformPenumbraSizes = View.LightmapSceneData[LightmapDataBaseOffset + 1];
		LightmapData.LightMapCoordinateScaleBias = View.LightmapSceneData[LightmapDataBaseOffset + 2];
		LightmapData.ShadowMapCoordinateScaleBias = View.LightmapSceneData[LightmapDataBaseOffset + 3];
		LightmapData.LightMapScale[0] = View.LightmapSceneData[LightmapDataBaseOffset + 4];
		LightmapData.LightMapScale[1] = View.LightmapSceneData[LightmapDataBaseOffset + 5];
		LightmapData.LightMapAdd[0] = View.LightmapSceneData[LightmapDataBaseOffset + 6];
		LightmapData.LightMapAdd[1] = View.LightmapSceneData[LightmapDataBaseOffset + 7];
		
		return LightmapData;
	}
#else 
	#define GetLightmapData(x) PrecomputedLightingBuffer
#endif

void GetLightMapColorHQ( , 
	float2 LightmapUV0, 
	float2 LightmapUV1, 
	uint LightmapDataIndex, 
	half3 WorldNormal, 
	float2 SvPositionXY, 
	uint ShadingModel, 
	out half3 OutDiffuseLighting, 
	out half3 OutSubsurfaceLighting )
{
	OutSubsurfaceLighting = 0;

	half4 Lightmap0;
	half4 Lightmap1;
	Lightmap0 = Texture2DSample( LightmapResourceCluster.LightMapTexture, LightmapResourceCluster.LightMapSampler, LightmapUV0 );
	Lightmap1 = Texture2DSample( LightmapResourceCluster.LightMapTexture, LightmapResourceCluster.LightMapSampler, LightmapUV1 );

	half LogL = Lightmap0.w;

	// Add residual
	LogL += Lightmap1.w * (1.0 / 255) - (0.5 / 255);

	// Range scale LogL
	LogL = LogL * GetLightmapData(LightmapDataIndex).LightMapScale[0].w + GetLightmapData(LightmapDataIndex).LightMapAdd[0].w;
		
	// Range scale UVW
	half3 UVW = Lightmap0.rgb * Lightmap0.rgb * GetLightmapData(LightmapDataIndex).LightMapScale[0].rgb + GetLightmapData(LightmapDataIndex).LightMapAdd[0].rgb;

	// LogL -> L
	const half LogBlackPoint = 0.01858136;
	half L = exp2( LogL ) - LogBlackPoint;

#if USE_LM_DIRECTIONALITY
	// Range scale SH. Alpha doesn't matter, will scale with zero
	float4 SH = Lightmap1 * GetLightmapData(LightmapDataIndex).LightMapScale[1] + GetLightmapData(LightmapDataIndex).LightMapAdd[1];
	// Sample SH with normal
	half Directionality = max( 0.0, dot( SH, float4(WorldNormal.yzx, 1) ) );
#else
	half Directionality = 0.6;
#endif

	half Luma = L * Directionality;
	half3 Color = Luma * UVW;

	OutDiffuseLighting = Color;
}

void GetPrecomputedIndirectLightingAndSkyLight(
	FMaterialPixelParameters MaterialParameters, 
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	VTPageTableResult LightmapVTPageTableResult,
	FGBufferData GBuffer,
	float3 DiffuseDir,
	float3 VolumetricLightmapBrickTextureUVs,
	out float3 OutDiffuseLighting,
	out float3 OutSubsurfaceLighting,
	out float OutIndirectIrradiance)
{
	OutIndirectIrradiance = 0;
	OutDiffuseLighting = 0;
	OutSubsurfaceLighting = 0;
	float2 SkyOcclusionUV = 0;
	uint SkyOcclusionDataIndex = 0u;

	#if PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING
		FThreeBandSHVectorRGB IrradianceSH = GetVolumetricLightmapSH3(VolumetricLightmapBrickTextureUVs);
		FThreeBandSHVector DiffuseTransferSH = CalcDiffuseTransferSH3(DiffuseDir, 1);// Diffuse convolution
		OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) / PI;

	#elif HQ_TEXTURE_LIGHTMAP
		float2 LightmapUV0, LightmapUV1;
		uint LightmapDataIndex;
		GetLightMapCoordinates(Interpolants, LightmapUV0, LightmapUV1, LightmapDataIndex);
		
		GetLightMapColorHQ(, 
			LightmapUV0, 
			LightmapUV1, 
			LightmapDataIndex, 
			DiffuseDir, 
			MaterialParameters.SvPosition.xy, 
			GBuffer.ShadingModelID, 
			OutDiffuseLighting, 
			OutSubsurfaceLighting);

		SkyOcclusionUV = LightmapUV0;
		SkyOcclusionDataIndex = LightmapDataIndex;

	#elif LQ_TEXTURE_LIGHTMAP
		float2 LightmapUV0, LightmapUV1;
		uint LightmapDataIndex;
		GetLightMapCoordinates(Interpolants, LightmapUV0, LightmapUV1, LightmapDataIndex);
		OutDiffuseLighting = GetLightMapColorLQ(LightmapUV0, LightmapUV1, LightmapDataIndex, DiffuseDir).rgb;
	#endif

	// Apply indirect lighting scale while we have only accumulated lightmaps
	OutDiffuseLighting *= View.IndirectLightingColorScale;
	OutSubsurfaceLighting *= View.IndirectLightingColorScale;

	float3 SkyDiffuseLighting;
	float3 SkySubsurfaceLighting;
	GetSkyLighting(MaterialParameters, LightmapVTPageTableResult, GBuffer, DiffuseDir, SkyOcclusionUV, SkyOcclusionDataIndex, VolumetricLightmapBrickTextureUVs, SkyDiffuseLighting, SkySubsurfaceLighting);

	OutSubsurfaceLighting += SkySubsurfaceLighting;	
	OutDiffuseLighting += SkyDiffuseLighting;
	OutIndirectIrradiance = Luminance(OutDiffuseLighting);
}

half4 GetPrecomputedShadowMasks(
	VTPageTableResult LightmapVTPageTableResult, 
	FVertexFactoryInterpolantsVSToPS Interpolants, 
	uint PrimitiveId, 
	float3 WorldPosition, 
	float3 VolumetricLightmapBrickTextureUVs)
{
#if STATICLIGHTING_TEXTUREMASK && STATICLIGHTING_SIGNEDDISTANCEFIELD
	float2 ShadowMapCoordinate;
	uint LightmapDataIndex;
	GetShadowMapCoordinate(Interpolants, ShadowMapCoordinate, LightmapDataIndex);

	half4 DistanceField = Texture2DSample(LightmapResourceCluster.StaticShadowTexture, LIGHTMAP_SHARED_SAMPLER(StaticShadowTextureSampler), ShadowMapCoordinate);
	
	float4 InvUniformPenumbraSizes = GetLightmapData(LightmapDataIndex).InvUniformPenumbraSizes;
	half4 ShadowFactors = saturate( 
		(DistanceField-0.5f) * InvUniformPenumbraSizes + 0.5f 
	);
	
	return GetLightmapData(LightmapDataIndex).StaticShadowMapMasks * ShadowFactors * ShadowFactors;

#elif HQ_TEXTURE_LIGHTMAP || LQ_TEXTURE_LIGHTMAP
	// Mark as shadowed for lightmapped objects with no shadowmap
	// This is necessary because objects inside a light's influence that were determined to be completely shadowed won't be rendered with STATICLIGHTING_TEXTUREMASK==1
	return 0;
#else

	float DirectionalLightShadowing = 1.0f;

	if (GetPrimitiveData(PrimitiveId).UseVolumetricLightmapShadowFromStationaryLights > 0)
	{
		#if !PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING
			// Compute brick UVs if we haven't already
			VolumetricLightmapBrickTextureUVs = ComputeVolumetricLightmapBrickTextureUVs(WorldPosition);
		#endif

		DirectionalLightShadowing = GetVolumetricLightmapDirectionalLightShadowing(VolumetricLightmapBrickTextureUVs);
	}

	// Directional light is always packed into the first static shadowmap channel, so output the per-primitive directional light shadowing there if requested
	return half4(DirectionalLightShadowing, 1, 1, 1);

#endif
}

void FPixelShaderInOut_MainPS(
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	in FPixelShaderIn In,
	inout FPixelShaderOut Out)
{
	const uint EyeIndex = 0;
	ResolvedView = ResolveView();

	float4 OutVelocity = 0;// Velocity
	float4 OutGBufferD = 0;// CustomData
	float4 OutGBufferE = 0;// PreShadowFactor
	float4 OutGBufferF = 0;// Wolrd Space Tangent 

	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(Interpolants, In.SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;

	VTPageTableResult LightmapVTPageTableResult = (VTPageTableResult)0.0f;

	float4 ScreenPosition = SvPositionToResolvedScreenPosition(In.SvPosition);
	float3 TranslatedWorldPosition = SvPositionToResolvedTranslatedWorld(In.SvPosition);
	CalcMaterialParametersEx(MaterialParameters, PixelMaterialInputs, In.SvPosition, ScreenPosition, In.bIsFrontFace, TranslatedWorldPosition, TranslatedWorldPosition);

	const bool bEditorWeightedZBuffering = false;

	//Clip if the blend mode requires it.
	if (!bEditorWeightedZBuffering)
	{
		GetMaterialCoverageAndClipping(MaterialParameters, PixelMaterialInputs);
	}

	// Store the results in local variables and reuse instead of calling the functions multiple times.
	half3 BaseColor = GetMaterialBaseColor(PixelMaterialInputs);
	half  Metallic = GetMaterialMetallic(PixelMaterialInputs);
	half  Specular = GetMaterialSpecular(PixelMaterialInputs);
	float MaterialAO = GetMaterialAmbientOcclusion(PixelMaterialInputs);
	float Roughness = GetMaterialRoughness(PixelMaterialInputs);
	float Anisotropy = GetMaterialAnisotropy(PixelMaterialInputs);
	uint ShadingModel = GetMaterialShadingModel(PixelMaterialInputs);
	half Opacity = GetMaterialOpacity(PixelMaterialInputs);

	float3 VolumetricLightmapBrickTextureUVs;

#if PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING
	VolumetricLightmapBrickTextureUVs = ComputeVolumetricLightmapBrickTextureUVs(MaterialParameters.AbsoluteWorldPosition);
#endif

	FGBufferData GBuffer = (FGBufferData)0;

	GBuffer.GBufferAO = MaterialAO;
	GBuffer.PerObjectGBufferData = GetPrimitiveData(MaterialParameters.PrimitiveId).PerObjectGBufferData;
	GBuffer.Depth = MaterialParameters.ScreenPosition.w;
	GBuffer.PrecomputedShadowFactors = GetPrecomputedShadowMasks(
		LightmapVTPageTableResult, 
		Interpolants, 
		MaterialParameters.PrimitiveId, 
		MaterialParameters.AbsoluteWorldPosition, 
		VolumetricLightmapBrickTextureUVs);

	const float GBufferDither = InterleavedGradientNoise(MaterialParameters.SvPosition.xy, View.StateFrameIndexMod8);
	// Use GBuffer.ShadingModelID after SetGBufferForShadingModel(..) because the ShadingModel input might not be the same as the output
	SetGBufferForShadingModel(
		/*out*/GBuffer,		// Especially, GBuffer.CostumData is filled.
		MaterialParameters,
		Opacity,
		BaseColor,
		Metallic,
		Specular,
		Roughness,
		Anisotropy,
		0,
		0,
		GBufferDither,
		ShadingModel);

#if USES_GBUFFER
	GBuffer.SelectiveOutputMask = GetSelectiveOutputMask();
	GBuffer.Velocity = 0;
#endif

	GBuffer.SpecularColor = lerp(float3(0.08f*Specular), BaseColor, Metallic);
	GBuffer.DiffuseColor = BaseColor * (1.0 - Metallic);

#if !FORCE_FULLY_ROUGH
	if (View.RenderingReflectionCaptureMask)
#endif
	{
		//EnvBRDFApproxFullyRough(GBuffer.DiffuseColor, GBuffer.SpecularColor);
		GBuffer.DiffuseColor += GBuffer.SpecularColor * 0.45;
		GBuffer.SpecularColor = 0;
	}

	float3 BentNormal = MaterialParameters.WorldNormal;
	
	float DiffOcclusion = MaterialAO;
	float SpecOcclusion = MaterialAO;
	//ApplyBentNormal( MaterialParameters, GBuffer.Roughness, /*out*/BentNormal, /*out*/DiffOcclusion, /*out*/SpecOcclusion );
	GBuffer.GBufferAO = AOMultiBounce( Luminance( GBuffer.SpecularColor ), SpecOcclusion ).g;

	half3 DiffuseColor = 0;
	half3 Color = 0;

#if !MATERIAL_SHADINGMODEL_UNLIT
	float3 DiffuseDir = BentNormal;
	float3 DiffuseColorForIndirect = GBuffer.DiffuseColor;

	float3 DiffuseIndirectLighting;
	float3 SubsurfaceIndirectLighting;
	float IndirectIrradiance;

	GetPrecomputedIndirectLightingAndSkyLight(
		MaterialParameters, 
		Interpolants, 
		BasePassInterpolants, 
		LightmapVTPageTableResult, 
		GBuffer, 
		DiffuseDir, 
		VolumetricLightmapBrickTextureUVs, 
		/*out*/DiffuseIndirectLighting, 
		/*out*/SubsurfaceIndirectLighting, 
		/*out*/IndirectIrradiance);
	
	DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect) * AOMultiBounce( GBuffer.BaseColor, DiffOcclusion );
#endif

	float4 Fogging = float4(0,0,0,1);
	half3 Emissive = GetMaterialEmissive(PixelMaterialInputs);

	Color += DiffuseColor;
	Color += Emissive;
	Color = Color * Fogging.a + Fogging.rgb;

	Out.MRT[0] = Color;

#if !USES_GBUFFER
	// Without deferred shading the SSS pass will not be run to reset scene color alpha for opaque / masked to 0
	// Scene color alpha is used by scene captures and planar reflections
	Out.MRT[0].a = 0;
#endif

#if USES_GBUFFER
	GBuffer.IndirectIrradiance = IndirectIrradiance;
	float QuantizationBias = PseudoRandom( MaterialParameters.SvPosition.xy ) - 0.5f;
	EncodeGBuffer(GBuffer, Out.MRT[1], Out.MRT[2], Out.MRT[3], OutGBufferD, OutGBufferE, OutGBufferF, OutVelocity, QuantizationBias);
#endif 

#if USES_GBUFFER
	Out.MRT[4] = OutGBufferD;

	#if GBUFFER_HAS_PRECSHADOWFACTOR
		Out.MRT[5] = OutGBufferE;
	#endif
#endif

#if !MATERIALBLENDING_MODULATE && USE_PREEXPOSURE
	// We need to multiply pre-exposure by all components including A, otherwise the ratio of
	// diffuse to specular lighting will get messed up in the SSS pass.
	// RGB: Full color (Diffuse + Specular)
	// A:   Diffuse Intensity, but only if we are not blending
	#if MATERIAL_DOMAIN_POSTPROCESS || MATERIALBLENDING_ALPHAHOLDOUT || MATERIALBLENDING_ALPHACOMPOSITE || MATERIALBLENDING_TRANSLUCENT || MATERIALBLENDING_ADDITIVE
		Out.MRT[0].rgb *= View.PreExposure;
	#else
		Out.MRT[0].rgba *= View.PreExposure;
	#endif
#endif

#if MATERIAL_IS_SKY
	// Sky materials can result in high luminance values, e.g. the sun disk. So we make sure to at least stay within the boundaries of fp10 for some platforms.
	Out.MRT[0].xyz = min(Out.MRT[0].xyz, Max10BitsFloat.xxx);
#endif
}

