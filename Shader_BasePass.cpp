// Simple version
void FPixelShaderInOut_MainPS(
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	in FPixelShaderIn In,
	inout FPixelShaderOut Out)
{
	ResolvedView = ResolveView();

	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(Interpolants, In.SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;
	{
		float4 ScreenPosition = SvPositionToResolvedScreenPosition(In.SvPosition);
		float3 TranslatedWorldPosition = SvPositionToResolvedTranslatedWorld(In.SvPosition);
		CalcMaterialParametersEx(MaterialParameters, PixelMaterialInputs, In.SvPosition, ScreenPosition, In.bIsFrontFace, TranslatedWorldPosition, TranslatedWorldPosition);
	}

	float3  BaseColor = GetMaterialBaseColor(PixelMaterialInputs);
	float  Metallic = GetMaterialMetallic(PixelMaterialInputs);
	float  Specular = GetMaterialSpecular(PixelMaterialInputs);

	float MaterialAO = GetMaterialAmbientOcclusion(PixelMaterialInputs);
	float Roughness = GetMaterialRoughness(PixelMaterialInputs);

	float SubsurfaceProfile = 0;
	float3 SubsurfaceColor = 0;

	float  Opacity = GetMaterialOpacity(PixelMaterialInputs);
	float3 VolumetricLightmapBrickTextureUVs;

	FGBufferData GBuffer = (FGBufferData)0;
	GBuffer.GBufferAO = MaterialAO;
	GBuffer.PerObjectGBufferData = GetPrimitiveData(MaterialParameters.PrimitiveId).PerObjectGBufferData;
	GBuffer.Depth = MaterialParameters.ScreenPosition.w;
	GBuffer.PrecomputedShadowFactors = GetPrecomputedShadowMasks(Interpolants, MaterialParameters.PrimitiveId, MaterialParameters.AbsoluteWorldPosition, VolumetricLightmapBrickTextureUVs);

	const float GBufferDither = InterleavedGradientNoise(MaterialParameters.SvPosition.xy, View_StateFrameIndexMod8);
	SetGBufferForShadingModel(
		GBuffer,
		MaterialParameters,
		Opacity,
		BaseColor,
		Metallic,
		Specular,
		Roughness,
		SubsurfaceColor,
		SubsurfaceProfile,
		GBufferDither);

	GBuffer.SelectiveOutputMask = GetSelectiveOutputMask();
	GBuffer.Velocity = 0;
	GBuffer.SpecularColor = ComputeF0(Specular, BaseColor, Metallic);
	GBuffer.DiffuseColor = BaseColor - BaseColor * Metallic;
	{

		GBuffer.DiffuseColor = GBuffer.DiffuseColor * View_DiffuseOverrideParameter.w + View_DiffuseOverrideParameter.xyz;
		GBuffer.SpecularColor = GBuffer.SpecularColor * View_SpecularOverrideParameter.w + View_SpecularOverrideParameter.xyz;
	}

	float3 BentNormal = MaterialParameters.WorldNormal;
	float DiffOcclusion = MaterialAO;
	float SpecOcclusion = MaterialAO;
	ApplyBentNormal( MaterialParameters, GBuffer.Roughness, BentNormal, DiffOcclusion, SpecOcclusion );

	GBuffer.GBufferAO = AOMultiBounce( Luminance( GBuffer.SpecularColor ), SpecOcclusion ).g;

	float3  DiffuseColor = 0;
	float3  Color = 0;
	float IndirectIrradiance = 0;

#if !MATERIAL_SHADINGMODEL_UNLIT	
	float3 DiffuseDir = BentNormal;
	float3 DiffuseColorForIndirect = GBuffer.DiffuseColor;

	float3 DiffuseIndirectLighting;
	float3 SubsurfaceIndirectLighting;
	GetPrecomputedIndirectLightingAndSkyLight(MaterialParameters, Interpolants, BasePassInterpolants, DiffuseDir, VolumetricLightmapBrickTextureUVs, DiffuseIndirectLighting, SubsurfaceIndirectLighting, IndirectIrradiance);

	float IndirectOcclusion = 1.0f;
	float2 NearestResolvedDepthScreenUV = 0;

	DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor) * AOMultiBounce( GBuffer.BaseColor, DiffOcclusion );
#endif

	float4 HeightFogging = float4(0,0,0,1);
	float4 Fogging = HeightFogging;

	float3  Emissive = GetMaterialEmissive(PixelMaterialInputs);

	Color += DiffuseColor;
	Color += Emissive;

	{
		FLightAccumulator LightAccumulator = (FLightAccumulator)0;
		Color = Color * Fogging.a + Fogging.rgb;
		LightAccumulator_Add(LightAccumulator, Color, 0, 1.0f, false);
		Out.MRT[0] =  ( LightAccumulator_GetResult(LightAccumulator) ) ;
	}

	GBuffer.IndirectIrradiance = IndirectIrradiance;

	float4 OutVelocity = 0;
	float4 OutGBufferD = 0;
	float4 OutGBufferE = 0;
	float QuantizationBias = PseudoRandom( MaterialParameters.SvPosition.xy ) - 0.5f;
	EncodeGBuffer(GBuffer, Out.MRT[1], Out.MRT[2], Out.MRT[3], OutGBufferD, OutGBufferE, OutVelocity, QuantizationBias);

	Out.MRT[4] = OutGBufferD;
	Out.MRT[5] = OutGBufferE;
}


/** Calculates indirect lighting contribution on this object from precomputed data. */
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
		FThreeBandSHVector DiffuseTransferSH = CalcDiffuseTransferSH3(DiffuseDir, 1);
		OutDiffuseLighting = max(float3(0,0,0), DotSH3(IrradianceSH, DiffuseTransferSH)) / PI;

	#elif HQ_TEXTURE_LIGHTMAP
		float2 LightmapUV0, LightmapUV1;
		uint LightmapDataIndex;
		GetLightMapCoordinates(Interpolants, LightmapUV0, LightmapUV1, LightmapDataIndex);
		GetLightMapColorHQ(
			LightmapVTPageTableResult, 
			LightmapUV0, LightmapUV1, LightmapDataIndex, DiffuseDir, 
			MaterialParameters.SvPosition.xy, 
			GBuffer.ShadingModelID, 
			/*Out*/OutDiffuseLighting, 
			/*Out*/OutSubsurfaceLighting);
		
		SkyOcclusionUV = LightmapUV0;
		SkyOcclusionDataIndex = LightmapDataIndex;
	#endif

	OutDiffuseLighting *= View.IndirectLightingColorScale;
	OutSubsurfaceLighting *= View.IndirectLightingColorScale;

	float3 SkyDiffuseLighting;
	float3 SkySubsurfaceLighting;
	GetSkyLighting(
		MaterialParameters, 
		LightmapVTPageTableResult, 
		GBuffer, 
		DiffuseDir, 
		SkyOcclusionUV, 
		SkyOcclusionDataIndex, 
		VolumetricLightmapBrickTextureUVs, 
		SkyDiffuseLighting, 
		SkySubsurfaceLighting);

	OutDiffuseLighting += SkyDiffuseLighting; 
	OutSubsurfaceLighting += SkySubsurfaceLighting;

	OutIndirectIrradiance = Luminance(OutDiffuseLighting);
}

/** Computes sky diffuse lighting, including precomputed shadowing. */
void GetSkyLighting(
	FMaterialPixelParameters MaterialParameters, , 
	FGBufferData GBuffer, 
	float3 WorldNormal, // ApplyBentNormal( MaterialParameters, GBuffer.Roughness, MaterialParameters.WorldNormal, ,  );
	float2 LightmapUV, 
	uint LightmapDataIndex, 
	float3 SkyOcclusionUV3D, 
	out float3 OutDiffuseLighting, 
	out float3 OutSubsurfaceLighting)
{
	OutDiffuseLighting = 0;
	OutSubsurfaceLighting = 0;

#if !ENABLE_SKY_LIGHT // return if not stationary skylight
	return;
#endif

	float SkyVisibility = 1;
	float GeometryTerm = 1;
	float3 SkyLightingNormal = WorldNormal;

	if (ShouldSkyLightApplyPrecomputedBentNormalShadowing()) // bCastShadow == ture
	{
	#if PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING
		SkyLightingNormal = GetVolumetricLightmapSkyBentNormal(SkyOcclusionUV3D);
		SkyVisibility = length(SkyLightingNormal);
		SkyLightingNormal = SkyLightingNormal / max(SkyVisibility, .0001f);

	#elif HQ_TEXTURE_LIGHTMAP
		float4 Temp = GetSkyBentNormalAndOcclusion(, LightmapUV * float2(1, 2), LightmapDataIndex, MaterialParameters.SvPosition.xy);
		SkyLightingNormal = normalize(Temp.xyz);// Renormalize as vector was quantized and compressed
		SkyVisibility = Temp.w;
	#endif
		
		float w = 1 - (1 - SkyVisibility) * (1 - SkyVisibility);// Weight toward the material normal to increase directionality
		GeometryTerm = lerp(saturate(dot(SkyLightingNormal, WorldNormal)), 1, w);
		SkyLightingNormal = lerp(SkyLightingNormal, WorldNormal, w);
	}
		
	OutDiffuseLighting = GetSkySHDiffuse(SkyLightingNormal) * ResolvedView.SkyLightColor.rgb * SkyVisibility * GeometryTerm;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define USES_GBUFFER 					(MATERIALBLENDING_SOLID || MATERIALBLENDING_MASKED)  // BLEND_Opaque or BLEND_Masked
#define SELECTIVE_BASEPASS_OUTPUTS 		TEXT("r.SelectiveBasePassOutputs") 	// default 0
#define GBUFFER_HAS_VELOCITY			TEXT("r.BasePassOutputsVelocity")	// default 0
#define USES_EMISSIVE_COLOR				FHLSLMaterialTranslator::bUsesEmissiveColor
#define WANT_PIXEL_DEPTH_OFFSET			FHLSLMaterialTranslator::bUsesPixelDepthOffset
#define USE_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS	FHLSLMaterialTranslator::bNeedsWorldPositionExcludingShaderOffsets
#define EDITOR_PRIMITIVE_MATERIAL		FMaterial::bUsedWithEditorCompositing	// always 0 ?
#define NEEDS_BASEPASS_VERTEX_FOGGING	(TRANSLUCENCY_NEEDS_BASEPASS_FOGGING && !MATERIAL_COMPUTE_FOG_PER_PIXEL || OPAQUE_NEEDS_BASEPASS_FOGGING && PROJECT_VERTEX_FOGGING_FOR_OPAQUE)
#define NEEDS_BASEPASS_PIXEL_FOGGING	(TRANSLUCENCY_NEEDS_BASEPASS_FOGGING && MATERIAL_COMPUTE_FOG_PER_PIXEL || OPAQUE_NEEDS_BASEPASS_FOGGING && !PROJECT_VERTEX_FOGGING_FOR_OPAQUE)
#define OUTPUT_PIXEL_DEPTH_OFFSET 		(WANT_PIXEL_DEPTH_OFFSET && (MATERIALBLENDING_SOLID || MATERIALBLENDING_MASKED))
#define POST_PROCESS_SUBSURFACE 		((MATERIAL_SHADINGMODEL_SUBSURFACE_PROFILE || MATERIAL_SHADINGMODEL_EYE) && USES_GBUFFER)
#define USE_EDITOR_COMPOSITING 			(USE_EDITOR_SHADERS && EDITOR_PRIMITIVE_MATERIAL)

// the following needs to match to the code in FSceneRenderTargets::GetGBufferRenderTargets()
#define PIXELSHADEROUTPUT_BASEPASS 1
#if USES_GBUFFER
	#define PIXELSHADEROUTPUT_MRT0 (!SELECTIVE_BASEPASS_OUTPUTS || NEEDS_BASEPASS_VERTEX_FOGGING || USES_EMISSIVE_COLOR || ALLOW_STATIC_LIGHTING)
	#define PIXELSHADEROUTPUT_MRT1 (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT)
	#define PIXELSHADEROUTPUT_MRT2 (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT)
	#define PIXELSHADEROUTPUT_MRT3 (!SELECTIVE_BASEPASS_OUTPUTS || !MATERIAL_SHADINGMODEL_UNLIT)
	#if GBUFFER_HAS_VELOCITY || GBUFFER_HAS_TANGENT
		#define PIXELSHADEROUTPUT_MRT4 WRITES_VELOCITY_TO_GBUFFER || GBUFFER_HAS_TANGENT
		#define PIXELSHADEROUTPUT_MRT5 (!SELECTIVE_BASEPASS_OUTPUTS || WRITES_CUSTOMDATA_TO_GBUFFER)
		#define PIXELSHADEROUTPUT_MRT6 (GBUFFER_HAS_PRECSHADOWFACTOR && (!SELECTIVE_BASEPASS_OUTPUTS || WRITES_PRECSHADOWFACTOR_TO_GBUFFER && !MATERIAL_SHADINGMODEL_UNLIT))
	#else //GBUFFER_HAS_VELOCITY || GBUFFER_HAS_TANGENT
		#define PIXELSHADEROUTPUT_MRT4 (!SELECTIVE_BASEPASS_OUTPUTS || WRITES_CUSTOMDATA_TO_GBUFFER)
		#define PIXELSHADEROUTPUT_MRT5 (GBUFFER_HAS_PRECSHADOWFACTOR && (!SELECTIVE_BASEPASS_OUTPUTS || WRITES_PRECSHADOWFACTOR_TO_GBUFFER && !MATERIAL_SHADINGMODEL_UNLIT))
	#endif //GBUFFER_HAS_VELOCITY
#else //USES_GBUFFER
	#define PIXELSHADEROUTPUT_MRT0 1
	// we also need MRT for thin translucency due to dual blending if we are not on the fallback path
	#define PIXELSHADEROUTPUT_MRT1 (WRITES_VELOCITY_TO_GBUFFER || (MATERIAL_SHADINGMODEL_THIN_TRANSLUCENT && THIN_TRANSLUCENT_USE_DUAL_BLEND))
#endif //USES_GBUFFER
#define PIXELSHADEROUTPUT_A2C ((EDITOR_ALPHA2COVERAGE) != 0)
#define PIXELSHADEROUTPUT_COVERAGE (MATERIALBLENDING_MASKED_USING_COVERAGE && !EARLY_Z_PASS_ONLY_MATERIAL_MASKING)


#define PIXELSHADEROUTPUT_INTERPOLANTS 0
#define PIXELSHADEROUTPUT_BASEPASS 1
#define PIXELSHADEROUTPUT_MESHDECALPASS 0
#define PIXELSHADEROUTPUT_MRT0 ?
#define PIXELSHADEROUTPUT_MRT1 ?
#define PIXELSHADEROUTPUT_MRT2 ?
#define PIXELSHADEROUTPUT_MRT3 ?
#define PIXELSHADEROUTPUT_MRT4 ?
#define PIXELSHADEROUTPUT_MRT5 ?
#define PIXELSHADEROUTPUT_MRT6 ?
#define PIXELSHADEROUTPUT_MRT7 ?
#define PIXELSHADEROUTPUT_COVERAGE ?
#define PIXELSHADEROUTPUT_A2C ?
#define PIXELSHADER_EARLYDEPTHSTENCIL ?


PIXELSHADER_EARLYDEPTHSTENCIL
void MainPS(
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	in float4 SvPosition : SV_Position		// after all interpolators
		
		OPTIONAL_IsFrontFace

#if PIXELSHADEROUTPUT_MRT0
		, out float4 OutTarget0 : SV_Target0
#endif

#if PIXELSHADEROUTPUT_MRT1
		, out float4 OutTarget1 : SV_Target1
#endif

#if PIXELSHADEROUTPUT_MRT2
		, out float4 OutTarget2 : SV_Target2
#endif

#if PIXELSHADEROUTPUT_MRT3
		, out float4 OutTarget3 : SV_Target3
#endif

#if PIXELSHADEROUTPUT_MRT4
		, out float4 OutTarget4 : SV_Target4
#endif

#if PIXELSHADEROUTPUT_MRT5
		, out float4 OutTarget5 : SV_Target5
#endif

#if PIXELSHADEROUTPUT_MRT6
		, out float4 OutTarget6 : SV_Target6
#endif

#if PIXELSHADEROUTPUT_MRT7
		, out float4 OutTarget7 : SV_Target7
#endif

		OPTIONAL_OutDepthConservative

#if PIXELSHADEROUTPUT_COVERAGE || PIXELSHADEROUTPUT_A2C
	#if PIXELSHADEROUTPUT_A2C
		, in uint InCoverage : SV_Coverage
	#endif
		, out uint OutCoverage : SV_Coverage
#endif
	)
{
	// ---------------------------------------------------------------------------------

	FPixelShaderIn PixelShaderIn = (FPixelShaderIn)0;
	FPixelShaderOut PixelShaderOut = (FPixelShaderOut)0;

#if PIXELSHADEROUTPUT_COVERAGE || PIXELSHADEROUTPUT_A2C
#if PIXELSHADEROUTPUT_A2C
	PixelShaderIn.Coverage = InCoverage;
#else
	PixelShaderIn.Coverage = 0xF;
#endif
	PixelShaderOut.Coverage = PixelShaderIn.Coverage;
#endif 

	PixelShaderIn.SvPosition = SvPosition;
	PixelShaderIn.bIsFrontFace = bIsFrontFace;

	FPixelShaderInOut_MainPS(Interpolants, BasePassInterpolants, PixelShaderIn, PixelShaderOut);


#if PIXELSHADEROUTPUT_MRT0
	OutTarget0 = PixelShaderOut.MRT[0];
#endif

#if PIXELSHADEROUTPUT_MRT1
	OutTarget1 = PixelShaderOut.MRT[1];
#endif

#if PIXELSHADEROUTPUT_MRT2
	OutTarget2 = PixelShaderOut.MRT[2];
#endif

#if PIXELSHADEROUTPUT_MRT3
	OutTarget3 = PixelShaderOut.MRT[3];
#endif

#if PIXELSHADEROUTPUT_MRT4
	OutTarget4 = PixelShaderOut.MRT[4];
#endif

#if PIXELSHADEROUTPUT_MRT5
	OutTarget5 = PixelShaderOut.MRT[5];
#endif

#if PIXELSHADEROUTPUT_MRT6
	OutTarget6 = PixelShaderOut.MRT[6];
#endif

#if PIXELSHADEROUTPUT_MRT7
	OutTarget7 = PixelShaderOut.MRT[7];
#endif

#if PIXELSHADEROUTPUT_COVERAGE || PIXELSHADEROUTPUT_A2C
	OutCoverage = PixelShaderOut.Coverage;
#endif 

#if OUTPUT_PIXEL_DEPTH_OFFSET
	OutDepth = PixelShaderOut.Depth;
#endif 
}


struct FPixelShaderIn
{
	float4 SvPosition;// read only
	uint Coverage;// Pixel Shader InCoverage, only usable if PIXELSHADEROUTPUT_COVERAGE is 1
	bool bIsFrontFace;
};
struct FPixelShaderOut
{
	float4 MRT[8];// [0..7], only usable if PIXELSHADEROUTPUT_MRT0, PIXELSHADEROUTPUT_MRT1, ... is 1
	uint Coverage;// Pixel Shader OutCoverage, only usable if PIXELSHADEROUTPUT_COVERAGE is 1
	float Depth;// Pixel Shader OutDepth
};

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

struct FGBufferData
{
	// GBufferA
	float3 WorldNormal;
	float PerObjectGBufferData;	// 0..1, 2 bits, use HasDistanceFieldRepresentation(GBuffer) or HasDynamicIndirectShadowCasterRepresentation(GBuffer) to extract

	// GBufferB
	float Metallic;
	float Specular;
	float Roughness;
	uint ShadingModelID;
	uint SelectiveOutputMask;	// 0..255 

	// GBufferC
	float3 BaseColor;			// 0..1, white for SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE (apply BaseColor after scattering is more correct and less blurry)
	float IndirectIrradiance;	// Indirect irradiance luma
	float GBufferAO;			// 0..1 ambient occlusion  e.g.SSAO, wet surface mask, skylight mask, ...
	
	// GBufferD
	float4 CustomData;			// 0..1

	// GBufferE
	float4 PrecomputedShadowFactors;	// Static shadow factors for channels assigned by Lightmass. Lights using static shadowing will pick up the appropriate channel in their deferred pass
	
	// GBufferF
	float3 WorldTangent;	// normalized, only valid if GBUFFER_HAS_TANGENT
	float Anisotropy;		// -1..1, only valid if GBUFFER_HAS_TANGENT

	// GBufferVelocity
	float4 Velocity;		// Velocity for motion blur (only used when WRITES_VELOCITY_TO_GBUFFER is enabled)

	float3 DiffuseColor;	// 0..1 (derived from BaseColor, Metalness, Specular)
	float3 SpecularColor;	// 0..1 (derived from BaseColor, Metalness, Specular)
	float Depth;			// in unreal units (linear), can be used to reconstruct world position, only valid when decoding the GBuffer as the value gets reconstructed from the Z buffer
	float CustomDepth;		// in world units
	uint CustomStencil;		// Custom depth stencil value
	float3 StoredBaseColor;	// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply BaseColor later
	float StoredSpecular;	// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply Specular later
	float StoredMetallic;	// 0..1, only needed by SHADINGMODELID_EYE which encodes Iris Distance inside Metallic
};

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

#if USE_EDITOR_COMPOSITING
	const bool bEditorWeightedZBuffering = true;
#else
	const bool bEditorWeightedZBuffering = false;
#endif

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

	// 0..1, SubsurfaceProfileId = int(x * 255)
	float SubsurfaceProfile = 0;

	// If we don't use this shading model the color should be black (don't generate shader code for unused data, don't do indirectlighting cache lighting with this color).
	float3 SubsurfaceColor = 0;

#if MATERIAL_SHADINGMODEL_SUBSURFACE || MATERIAL_SHADINGMODEL_PREINTEGRATED_SKIN || MATERIAL_SHADINGMODEL_SUBSURFACE_PROFILE || MATERIAL_SHADINGMODEL_TWOSIDED_FOLIAGE || MATERIAL_SHADINGMODEL_CLOTH || MATERIAL_SHADINGMODEL_EYE
	if (ShadingModel == SHADINGMODELID_SUBSURFACE || ShadingModel == SHADINGMODELID_PREINTEGRATED_SKIN || ShadingModel == SHADINGMODELID_SUBSURFACE_PROFILE || ShadingModel == SHADINGMODELID_TWOSIDED_FOLIAGE || ShadingModel == SHADINGMODELID_CLOTH || ShadingModel == SHADINGMODELID_EYE)
	{
		float4 SubsurfaceData = GetMaterialSubsurfaceData(PixelMaterialInputs);

		if (false) // Dummy if to make the ifdef logic play nicely
		{
		}

	#if MATERIAL_SHADINGMODEL_SUBSURFACE || MATERIAL_SHADINGMODEL_PREINTEGRATED_SKIN || MATERIAL_SHADINGMODEL_TWOSIDED_FOLIAGE
		else if (ShadingModel == SHADINGMODELID_SUBSURFACE || ShadingModel == SHADINGMODELID_PREINTEGRATED_SKIN || ShadingModel == SHADINGMODELID_TWOSIDED_FOLIAGE)
		{
			SubsurfaceColor = SubsurfaceData.rgb * View.DiffuseOverrideParameter.w + View.DiffuseOverrideParameter.xyz;
		}
	#endif
	
	#if MATERIAL_SHADINGMODEL_CLOTH
		else if (ShadingModel == SHADINGMODELID_CLOTH)
		{
			SubsurfaceColor = SubsurfaceData.rgb;
		}
	#endif

		SubsurfaceProfile = SubsurfaceData.a;
	}
#endif

	float3 VolumetricLightmapBrickTextureUVs;

#if PRECOMPUTED_IRRADIANCE_VOLUME_LIGHTING
	VolumetricLightmapBrickTextureUVs = ComputeVolumetricLightmapBrickTextureUVs(MaterialParameters.AbsoluteWorldPosition);
#endif

	FGBufferData GBuffer = (FGBufferData)0;

	GBuffer.GBufferAO = MaterialAO;
	GBuffer.PerObjectGBufferData = GetPrimitiveData(MaterialParameters.PrimitiveId).PerObjectGBufferData;
	GBuffer.Depth = MaterialParameters.ScreenPosition.w;
	GBuffer.PrecomputedShadowFactors = GetPrecomputedShadowMasks(LightmapVTPageTableResult, Interpolants, MaterialParameters.PrimitiveId, MaterialParameters.AbsoluteWorldPosition, VolumetricLightmapBrickTextureUVs);

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
		SubsurfaceColor,
		SubsurfaceProfile,
		GBufferDither,
		ShadingModel);

#if USES_GBUFFER
	GBuffer.SelectiveOutputMask = GetSelectiveOutputMask();
	GBuffer.Velocity = 0;
#endif
	// So that the following code can still use DiffuseColor and SpecularColor.
	GBuffer.SpecularColor = lerp(float3(0.08f*Specular), BaseColor, Metallic); //ComputeF0(Specular, BaseColor, Metallic);


#if POST_PROCESS_SUBSURFACE
	// SubsurfaceProfile applies the BaseColor in a later pass. Any lighting output in the base pass needs
	// to separate specular and diffuse lighting in a checkerboard pattern
	bool bChecker = CheckerFromPixelPos(MaterialParameters.SvPosition.xy);
	if (UseSubsurfaceProfile(GBuffer.ShadingModelID))
	{
		AdjustBaseColorAndSpecularColorForSubsurfaceProfileLighting(BaseColor, GBuffer.SpecularColor, Specular, bChecker);
	}
#endif

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
			
	// Clear Coat Bottom Normal
	if( GBuffer.ShadingModelID == SHADINGMODELID_CLEAR_COAT && CLEAR_COAT_BOTTOM_NORMAL)
	{
		const float2 oct1 = ((float2(GBuffer.CustomData.a, GBuffer.CustomData.z) * 2) - (256.0/255.0)) + UnitVectorToOctahedron(GBuffer.WorldNormal);
		BentNormal = OctahedronToUnitVector(oct1);			
	}
	
	float DiffOcclusion = MaterialAO;
	float SpecOcclusion = MaterialAO;
	//ApplyBentNormal( MaterialParameters, GBuffer.Roughness, /*out*/BentNormal, /*out*/DiffOcclusion, /*out*/SpecOcclusion );
	GBuffer.GBufferAO = AOMultiBounce( Luminance( GBuffer.SpecularColor ), SpecOcclusion ).g;

	half3 DiffuseColor = 0;
	half3 Color = 0;

#if !MATERIAL_SHADINGMODEL_UNLIT
	float3 DiffuseDir = BentNormal;
	float3 DiffuseColorForIndirect = GBuffer.DiffuseColor;

	#if MATERIAL_SHADINGMODEL_SUBSURFACE || MATERIAL_SHADINGMODEL_PREINTEGRATED_SKIN
		if (GBuffer.ShadingModelID == SHADINGMODELID_SUBSURFACE || GBuffer.ShadingModelID == SHADINGMODELID_PREINTEGRATED_SKIN)
		{
			// Add subsurface energy to diffuse
			//@todo - better subsurface handling for these shading models with skylight and precomputed GI
			DiffuseColorForIndirect += SubsurfaceColor;
		}
	#endif

	#if MATERIAL_SHADINGMODEL_CLOTH
		if (GBuffer.ShadingModelID == SHADINGMODELID_CLOTH)
		{
			DiffuseColorForIndirect += SubsurfaceColor * saturate(GetMaterialCustomData0(MaterialParameters));
		}
	#endif

	#if MATERIAL_SHADINGMODEL_HAIR
		if (GBuffer.ShadingModelID == SHADINGMODELID_HAIR)
		{
			FHairTransmittanceData TransmittanceData = InitHairTransmittanceData();
			float3 N = MaterialParameters.WorldNormal;
			float3 V = MaterialParameters.CameraVector;
			float3 L = normalize( V - N * dot(V,N) );
			DiffuseDir = L;
			bool bEvalMultiScatter = true;
			DiffuseColorForIndirect = 2*PI * HairShading( GBuffer, L, V, N, 1, TransmittanceData, 0, 0.2, uint2(0,0), bEvalMultiScatter);
		}
	#endif

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
	
	DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor) * AOMultiBounce( GBuffer.BaseColor, DiffOcclusion );

	#if TRANSLUCENCY_LIGHTING_SURFACE_LIGHTINGVOLUME
		uint GridIndex = 0;
		GridIndex = ComputeLightGridCellIndex((uint2)(MaterialParameters.SvPosition.xy - ResolvedView.ViewRectMin.xy), MaterialParameters.SvPosition.w, EyeIndex);

		#if !(MATERIAL_SINGLE_SHADINGMODEL && MATERIAL_SHADINGMODEL_HAIR)// No IBL for water in deferred: that is skipped because it is done in the water composite pass. It should however be applied when using forward shading in order to get reflection without the water composite pass.
			if (GBuffer.ShadingModelID != SHADINGMODELID_HAIR)
			{
				int SingleCaptureIndex = GetPrimitiveData(MaterialParameters.PrimitiveId).SingleCaptureIndex;

				half3 ReflectionColor = GetImageBasedReflectionLighting(MaterialParameters, GBuffer.Roughness, GBuffer.SpecularColor, IndirectIrradiance, GridIndex, SingleCaptureIndex, EyeIndex)
					* AOMultiBounce(GBuffer.SpecularColor, SpecOcclusion);

				Color += ReflectionColor;
			}
		#endif
    #endif
#endif



	float4 Fogging = float4(0,0,0,1);

	// Volume lighting for lit translucency
#if (MATERIAL_SHADINGMODEL_DEFAULT_LIT || MATERIAL_SHADINGMODEL_SUBSURFACE) && (MATERIALBLENDING_TRANSLUCENT || MATERIALBLENDING_ADDITIVE)
	if (GBuffer.ShadingModelID == SHADINGMODELID_DEFAULT_LIT || GBuffer.ShadingModelID == SHADINGMODELID_SUBSURFACE)
	{
		Color += GetTranslucencyVolumeLighting(MaterialParameters, PixelMaterialInputs, BasePassInterpolants, GBuffer, IndirectIrradiance);
	}
#endif

	half3 Emissive = GetMaterialEmissive(PixelMaterialInputs);

#if !POST_PROCESS_SUBSURFACE
	Color += DiffuseColor;// For skin we need to keep them separate. We also keep them separate for thin translucent. Otherwise just add them together.
#endif
	Color += Emissive;



#if MATERIAL_DOMAIN_POSTPROCESS
	#if MATERIAL_OUTPUT_OPACITY_AS_ALPHA
		Out.MRT[0] = half4(Color, Opacity);
	#else
		Out.MRT[0] = half4(Color, 0);
	#endif
	Out.MRT[0] = RETURN_COLOR(Out.MRT[0]);

#elif MATERIALBLENDING_ALPHAHOLDOUT
	// not implemented for holdout
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb * Opacity, Opacity);
	Out.MRT[0] = RETURN_COLOR(Out.MRT[0]);

#elif MATERIALBLENDING_ALPHACOMPOSITE
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb * Opacity, Opacity);
	Out.MRT[0] = RETURN_COLOR(Out.MRT[0]);

#elif MATERIALBLENDING_TRANSLUCENT
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb, Opacity);
	Out.MRT[0] = RETURN_COLOR(Out.MRT[0]);

#elif MATERIALBLENDING_ADDITIVE
	Out.MRT[0] = half4(Color * Fogging.a * Opacity, 0.0f);
	Out.MRT[0] = RETURN_COLOR(Out.MRT[0]);

#elif MATERIALBLENDING_MODULATE
	// RETURN_COLOR not needed with modulative blending
	half3 FoggedColor = lerp(float3(1, 1, 1), Color, Fogging.aaa * Fogging.aaa);
	Out.MRT[0] = half4(FoggedColor, Opacity);

#else
	FLightAccumulator LightAccumulator = (FLightAccumulator)0;

	Color = Color * Fogging.a + Fogging.rgb;// Apply vertex fog

	#if POST_PROCESS_SUBSURFACE
		// Apply vertex fog to diffuse color
		DiffuseColor = DiffuseColor * Fogging.a + Fogging.rgb;

		if (UseSubsurfaceProfile(GBuffer.ShadingModelID) && 
	        View.bSubsurfacePostprocessEnabled > 0 && View.bCheckerboardSubsurfaceProfileRendering > 0 )
		{
			// Adjust for checkerboard. only apply non-diffuse lighting (including emissive) 
			// to the specular component, otherwise lighting is applied twice
			Color *= !bChecker;
		}
		LightAccumulator_Add(LightAccumulator, Color + DiffuseColor, DiffuseColor, 1.0f, UseSubsurfaceProfile(GBuffer.ShadingModelID));
	#else
		LightAccumulator_Add(LightAccumulator, Color, 0, 1.0f, false);
	#endif

	Out.MRT[0] = RETURN_COLOR(LightAccumulator_GetResult(LightAccumulator));

	#if !USES_GBUFFER
		// Without deferred shading the SSS pass will not be run to reset scene color alpha for opaque / masked to 0
		// Scene color alpha is used by scene captures and planar reflections
		Out.MRT[0].a = 0;
	#endif
#endif


#if USES_GBUFFER
	GBuffer.IndirectIrradiance = IndirectIrradiance;
	// -0.5 .. 0.5, could be optimzed as lower quality noise would be sufficient
	float QuantizationBias = PseudoRandom( MaterialParameters.SvPosition.xy ) - 0.5f;
	EncodeGBuffer(GBuffer, Out.MRT[1], Out.MRT[2], Out.MRT[3], OutGBufferD, OutGBufferE, OutGBufferF, OutVelocity, QuantizationBias);
#endif 

	if(bEditorWeightedZBuffering)
	{
		Out.MRT[0].a = 1;

	#if MATERIALBLENDING_MASKED
		// some material might have a opacity value
		Out.MRT[0].a = GetMaterialMaskInputRaw(PixelMaterialInputs);
	#endif

	#if EDITOR_ALPHA2COVERAGE != 0
		
		if(View.NumSceneColorMSAASamples > 1)// per MSAA sample
		{
			Out.Coverage = In.Coverage & CustomAlpha2Coverage(Out.MRT[0]);
		}
		else// no MSAA is handle like per pixel
		{
			clip(Out.MRT[0].a - GetMaterialOpacityMaskClipValue());
		}

	#else
		// per pixel
		clip(Out.MRT[0].a - GetMaterialOpacityMaskClipValue());
	#endif
	}

#if USES_GBUFFER
	Out.MRT[4] = OutGBufferD;
	Out.MRT[5] = OutGBufferE;
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


