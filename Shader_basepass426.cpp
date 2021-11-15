#define NUM_TEX_COORD_INTERPOLATORS 1
#define TEX_COORD_SCALE_ANALYSIS 0
#define INSTANCED_STEREO 0
#define LIGHTMAP_VT_ENABLED 0
#define USE_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS NEEDS_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS (FHLSLMaterialTranslator::bNeedsWorldPositionExcludingShaderOffsets(0)) 
#define USES_AO_MATERIAL_MASK (FHLSLMaterialTranslator::bUsesAOMaterialMask(0))
#define USE_WORLDVERTEXNORMAL_CENTER_INTERPOLATION MATERIAL_NORMAL_CURVATURE_TO_ROUGHNESS (UMaterial::bNormalCurvatureToRoughness(0))
#define MATERIAL_TWOSIDED (UMaterial::TwoSided(0))
#define MATERIAL_USES_ANISOTROPY 0
#define SIMPLE_FORWARD_SHADING 0
#define PLATFORM_SUPPORTS_RENDERTARGET_WRITE_MASK 0
#define PLATFORM_SUPPORTS_PER_PIXEL_DBUFFER_MASK 0
#define MATERIAL_SHADINGMODEL_X 0 for every X except UNLIT & DEFAULT_LIT
#define OUTPUT_PIXEL_DEPTH_OFFSET (WANT_PIXEL_DEPTH_OFFSET && ...) (FHLSLMaterialTranslator::bUsesPixelDepthOffset(0))
#define MATERIALBLENDING_MASKED_USING_COVERAGE (FORWARD_SHADING && ...)
#define EARLY_Z_PASS_ONLY_MATERIAL_MASKING 0
#define GBUFFER_HAS_VELOCITY 0
#define WRITES_VELOCITY_TO_GBUFFER (GBUFFER_HAS_VELOCITY && ...)
#define USE_DEVELOPMENT_SHADERS 1
#define EDITOR_PRIMITIVE_MATERIAL (UMaterial::bUsedWithEditorCompositing(0))
#define USE_EDITOR_COMPOSITING (PLATFORM_SUPPORTS_EDITOR_SHADERS && USE_DEVELOPMENT_SHADERS && EDITOR_PRIMITIVE_MATERIAL)
#define NUM_MATERIAL_OUTPUTS_GETBENTNORMAL 0
#define TRANSLUCENCY_LIGHTING_SURFACE_LIGHTINGVOLUME (Material->TranslucencyLightingMode==TLM_Surface) 0
#define TRANSLUCENCY_LIGHTING_SURFACE_FORWARDSHADING (Material->TranslucencyLightingMode==TLM_SurfacePerPixelLighting) 0
#define USES_GBUFFER (UMaterial::BlendMode==BLEND_Opaque || UMaterial::BlendMode==BLEND_Masked)
#define MATERIAL_IS_SKY 0
#define FORCE_FULLY_ROUGH 0
#define TRANSLUCENCY_NEEDS_BASEPASS_FOGGING	(UMaterial::bUseTranslucencyVertexFog && MATERIALBLENDING_ANY_TRANSLUCENT && !MATERIAL_USES_SCENE_COLOR_COPY)
#define NEEDS_BASEPASS_VERTEX_FOGGING (TRANSLUCENCY_NEEDS_BASEPASS_FOGGING && !UMaterial::bComputeFogPerPixel)
#define NEEDS_BASEPASS_PIXEL_FOGGING 0
View.DiffuseOverrideParameter = float4(0,0,0,1);
View.SpecularOverrideParameter = float4(0,0,0,1);
View.OutOfBoundsMask = 0 = ViewFamily->EngineShowFlags.VisualizeOutOfBoundsPixels;
View.UnlitViewmodeMask = 0 = !ViewFamily->EngineShowFlags.Lighting;

#include "/Engine/Generated/Material.ush"
//MaterialTemplete.ush////////////////////////////////////////////////////////////////////////////////////
struct FPixelMaterialInputs
{
	float3  EmissiveColor;
	float  Opacity;
	float  OpacityMask;
	float3  BaseColor;
	float  Metallic;
	float  Specular;
	float  Roughness;
	float  Anisotropy;
	float3  Normal;
	float3  Tangent;
	float4  Subsurface;
	float  AmbientOcclusion;
	float2  Refraction;
	float  PixelDepthOffset;
	uint ShadingModel;
};
struct FMaterialPixelParameters
{
	float2 TexCoords[NUM_TEX_COORD_INTERPOLATORS];
	half4 VertexColor;/** Interpolated vertex color, in linear color space. */
	half3 WorldNormal;/** Normalized world space normal. */
	half3 WorldTangent;/** Normalized world space tangent. */
	half3 ReflectionVector;/** Normalized world space reflected camera vector. */
	half3 CameraVector;/** Normalized world space camera vector, which is the vector from the point being shaded to the camera position. */
	half3 LightVector;/** World space light vector, only valid when rendering a light function. */
	/**
	 * Like SV_Position (.xy is pixel position at pixel center, z:DeviceZ, .w:SceneDepth) using shader generated value SV_POSITION
	 * Note: this is not relative to the current viewport.  RelativePixelPosition = MaterialParameters.SvPosition.xy - View.ViewRectMin.xy;
	 */
	float4 SvPosition;
	float4 ScreenPosition;/** Post projection position reconstructed from SvPosition, before the divide by W. left..top -1..1, bottom..top -1..1  within the viewport, W is the SceneDepth */
	half UnMirrored;
	half TwoSidedSign;
	half3x3 TangentToWorld;/** Orthonormal rotation-only transform from tangent space to world space. The transpose(TangentToWorld) is WorldToTangent, and TangentToWorld[2] is WorldVertexNormal */
	float3 AbsoluteWorldPosition;/** Interpolated worldspace position of this pixel */
	float3 WorldPosition_CamRelative;/** Interpolated worldspace position of this pixel, centered around the camera */
	half3 LightingPositionOffset;/** Offset applied to the lighting position for translucency, used to break up aliasing artifacts. */
#if LIGHTMAP_UV_ACCESS
	float2	LightmapUVs;
#endif
	uint PrimitiveId;// Index into View.PrimitiveSceneData
};

void CalcPixelMaterialInputs(in out FMaterialPixelParameters Parameters, in out FPixelMaterialInputs PixelMaterialInputs)
{
	%s // Initial calculations (required for Normal)
	%s // The Normal is a special case as it might have its own expressions and also be used to calculate other inputs, so perform the assignment here

	float3 MaterialNormal = GetMaterialNormal(Parameters, PixelMaterialInputs);// Note that here MaterialNormal can be in world space or tangent space
#if MATERIAL_TANGENTSPACENORMAL
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal = normalize(MaterialNormal));
	Parameters.WorldNormal *= Parameters.TwoSidedSign;// flip the normal for backfaces being rendered with a two-sided material
#else
	Parameters.WorldNormal = normalize(MaterialNormal);
#endif

	Parameters.ReflectionVector = ReflectionAboutCustomWorldNormal(Parameters, Parameters.WorldNormal, false);
	
	%s // Now the rest of the inputs

	Parameters.WorldTangent = 0;
}

void CalcMaterialParametersEx(
	in out FMaterialPixelParameters Parameters,
	in out FPixelMaterialInputs PixelMaterialInputs,
	float4 SvPosition,
	float4 ScreenPosition,
	FIsFrontFace bIsFrontFace,
	float3 TranslatedWorldPosition,
	float3 TranslatedWorldPositionExcludingShaderOffsets)
{
	Parameters.WorldPosition_CamRelative = TranslatedWorldPosition.xyz;
	Parameters.AbsoluteWorldPosition = TranslatedWorldPosition.xyz - ResolvedView.PreViewTranslation.xyz;
	Parameters.SvPosition = SvPosition;
	Parameters.ScreenPosition = ScreenPosition;
	Parameters.CameraVector = -WorldRayDirection();
	Parameters.LightVector = 0;
	Parameters.TwoSidedSign = 1.0f;
	// Now that we have all the pixel-related parameters setup, calculate the Material Input/Attributes and Normal
	CalcPixelMaterialInputs(Parameters, PixelMaterialInputs);
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "/Engine/Generated/VertexFactory.ush"
//LocalVertexFactory.ush//////////////////////////////////////////////////////////////////////////////////
FMaterialPixelParameters GetMaterialPixelParameters(FVertexFactoryInterpolantsVSToPS Interpolants, float4 SvPosition)
{
	FMaterialPixelParameters Parameters = (FMaterialPixelParameters)0;
	for( int CoordinateIndex = 0; CoordinateIndex < NUM_TEX_COORD_INTERPOLATORS; CoordinateIndex++ )
	{
		Parameters.TexCoords[CoordinateIndex] = GetUV(Interpolants, CoordinateIndex);
	}
	Parameters.UnMirrored = TangentToWorld2.w;
	Parameters.VertexColor = GetColor(Interpolants);
	Parameters.TangentToWorld = AssembleTangentToWorld( GetTangentToWorld0(Interpolants).xyz, GetTangentToWorld2(Interpolants) );
#if LIGHTMAP_UV_ACCESS && NEEDS_LIGHTMAP_COORDINATE
		Parameters.LightmapUVs = Interpolants.LightMapCoordinate.xy;
#endif
	Parameters.TwoSidedSign = 1;
	Parameters.PrimitiveId = GetPrimitiveId(Interpolants);
	return Parameters;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
//DeferredShadingCommon.ush///////////////////////////////////////////////////////////////////////////////
struct FGBufferData
{
	float3 WorldNormal;// normalized
	float3 WorldTangent;// normalized, only valid if HAS_ANISOTROPY_MASK in SelectiveOutputMask	
	float3 DiffuseColor;// 0..1 (derived from BaseColor, Metalness, Specular)
	float3 SpecularColor;// 0..1 (derived from BaseColor, Metalness, Specular)
	float3 BaseColor;// 0..1, white for SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE (apply BaseColor after scattering is more correct and less blurry)
	float Metallic;// 0..1
	float Specular;// 0..1
	float4 CustomData;// 0..1
	float IndirectIrradiance;// Indirect irradiance luma
	float4 PrecomputedShadowFactors;// Static shadow factors for channels assigned by Lightmass. Lights using static shadowing will pick up the appropriate channel in their deferred pass
	float Roughness;// 0..1
	float Anisotropy;// -1..1, only valid if only valid if HAS_ANISOTROPY_MASK in SelectiveOutputMask
	float GBufferAO;// 0..1 ambient occlusion  e.g.SSAO, wet surface mask, skylight mask, ...
	uint ShadingModelID;// 0..255 
	uint SelectiveOutputMask;// 0..255 
	float PerObjectGBufferData;// 0..1, 2 bits, use CastContactShadow(GBuffer) or HasDynamicIndirectShadowCasterRepresentation(GBuffer) to extract
	float CustomDepth;// in world units
	uint CustomStencil;// Custom depth stencil value
	float Depth;// in unreal units (linear), can be used to reconstruct world position, only valid when decoding the GBuffer as the value gets reconstructed from the Z buffer
	float4 Velocity;// Velocity for motion blur (only used when WRITES_VELOCITY_TO_GBUFFER is enabled)
	float3 StoredBaseColor;// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply BaseColor later
	float StoredSpecular;// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply Specular later
	float StoredMetallic;// 0..1, only needed by SHADINGMODELID_EYE which encodes Iris Distance inside Metallic
};

void EncodeGBuffer(
	FGBufferData GBuffer,
	out float4 OutGBufferA,
	out float4 OutGBufferB,
	out float4 OutGBufferC,
	out float4 OutGBufferD,
	out float4 OutGBufferE,
	out float4 OutGBufferVelocity,
	float QuantizationBias = 0		// -0.5 to 0.5 random float. Used to bias quantization.
	)
{
	if (GBuffer.ShadingModelID == SHADINGMODELID_UNLIT)
	{
		OutGBufferA = OutGBufferC = OutGBufferD = OutGBufferE = OutGBufferVelocity = 0;
		SetGBufferForUnlit(OutGBufferB);
		return;
	}

	OutGBufferA.rgb = EncodeNormal( GBuffer.WorldNormal );
	OutGBufferA.a = GBuffer.PerObjectGBufferData;
	OutGBufferB.rgb = float3(GBuffer.Metallic, GBuffer.Specular, GBuffer.Roughness);
	OutGBufferB.a = EncodeShadingModelIdAndSelectiveOutputMask(GBuffer.ShadingModelID, GBuffer.SelectiveOutputMask);
	OutGBufferC.rgb = EncodeBaseColor( GBuffer.BaseColor );
#if ALLOW_STATIC_LIGHTING
	OutGBufferC.a = EncodeIndirectIrradiance(GBuffer.IndirectIrradiance * GBuffer.GBufferAO) + QuantizationBias * (1.0 / 255.0);// No space for AO. Multiply IndirectIrradiance by AO instead of storing.
#else
	OutGBufferC.a = GBuffer.GBufferAO;
#endif
	OutGBufferD = GBuffer.CustomData;
	OutGBufferE = GBuffer.PrecomputedShadowFactors;
	OutGBufferVelocity = 0;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////
//ShadingModelsMaterial.ush///////////////////////////////////////////////////////////////////////////////
void SetGBufferForShadingModel(
	in out FGBufferData GBuffer, 
	in const FMaterialPixelParameters MaterialParameters,
	const float Opacity,
	const half3 BaseColor,
	const half  Metallic,
	const half  Specular,
	const float Roughness,
	const float Anisotropy,
	const float3 SubsurfaceColor,
	const float SubsurfaceProfile,
	const float Dither,
	const uint ShadingModel)
{
	GBuffer.WorldNormal = MaterialParameters.WorldNormal;
	GBuffer.WorldTangent = MaterialParameters.WorldTangent;
	GBuffer.BaseColor = BaseColor;
	GBuffer.Metallic = Metallic;
	GBuffer.Specular = Specular;
	GBuffer.Roughness = Roughness;
	GBuffer.Anisotropy = Anisotropy;
	GBuffer.ShadingModelID = ShadingModel;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////

void FPixelShaderInOut_MainPS(
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	in FPixelShaderIn In,
	inout FPixelShaderOut Out)
{
	ResolvedView = ResolveView();
	float4 OutVelocity = 0;// Velocity
	float4 OutGBufferD = 0;// CustomData
	float4 OutGBufferE = 0;// PreShadowFactor
	float4 ScreenPosition = SvPositionToResolvedScreenPosition(In.SvPosition);
	float3 TranslatedWorldPosition = SvPositionToResolvedTranslatedWorld(In.SvPosition);
	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(Interpolants, In.SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;
	CalcMaterialParametersEx(MaterialParameters, PixelMaterialInputs, In.SvPosition, ScreenPosition, In.bIsFrontFace, TranslatedWorldPosition, TranslatedWorldPosition);
	GetMaterialCoverageAndClipping(MaterialParameters, PixelMaterialInputs);

	half3 BaseColor = GetMaterialBaseColor(PixelMaterialInputs);
	half  Metallic = GetMaterialMetallic(PixelMaterialInputs);
	half  Specular = GetMaterialSpecular(PixelMaterialInputs);
	float MaterialAO = GetMaterialAmbientOcclusion(PixelMaterialInputs);
	float Roughness = GetMaterialRoughness(PixelMaterialInputs);
	float Anisotropy = GetMaterialAnisotropy(PixelMaterialInputs);
	uint ShadingModel = GetMaterialShadingModel(PixelMaterialInputs);
	half Opacity = GetMaterialOpacity(PixelMaterialInputs);
	float SubsurfaceProfile = 0;// 0..1, SubsurfaceProfileId = int(x * 255)
	float3 SubsurfaceColor = 0;// If we don't use this shading model the color should be black (don't generate shader code for unused data, don't do indirectlighting cache lighting with this color).
	float DBufferOpacity = 1.0f;
#if MATERIALDECALRESPONSEMASK && !MATERIALBLENDING_ANY_TRANSLUCENT
	if (GetPrimitiveData(MaterialParameters.PrimitiveId).DecalReceiverMask > 0 && View.ShowDecalsMask > 0)
	{
		uint DBufferMask = 0x07;
		if (DBufferMask)
		{
			float2 NDC = MaterialParameters.ScreenPosition.xy / MaterialParameters.ScreenPosition.w;
			float2 ScreenUV = NDC * ResolvedView.ScreenPositionScaleBias.xy + ResolvedView.ScreenPositionScaleBias.wz;
			FDBufferData DBufferData = GetDBufferData(ScreenUV, DBufferMask);
			ApplyDBufferData(DBufferData, MaterialParameters.WorldNormal, SubsurfaceColor, Roughness, BaseColor, Metallic, Specular);
			DBufferOpacity = (DBufferData.ColorOpacity + DBufferData.NormalOpacity + DBufferData.RoughnessOpacity) * (1.0f / 3.0f);
		}
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
	SetGBufferForShadingModel( // Use GBuffer.ShadingModelID after SetGBufferForShadingModel(..) because the ShadingModel input might not be the same as the output
		GBuffer,
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
		ShadingModel );
#if USES_GBUFFER	
	GBuffer.SelectiveOutputMask = GetSelectiveOutputMask();
	GBuffer.Velocity = 0;
#endif
	GBuffer.SpecularColor = ComputeF0(Specular, BaseColor, Metallic);
	GBuffer.DiffuseColor = BaseColor * (1.0 - Metallic);
	if (View.RenderingReflectionCaptureMask) 
	{
		EnvBRDFApproxFullyRough(GBuffer.DiffuseColor, GBuffer.SpecularColor);// When rendering reflection captures, GBuffer.Roughness is already forced to 1 using RoughnessOverrideParameter in GetMaterialRoughness.
	}

	float DiffOcclusion = MaterialAO;
	float SpecOcclusion = MaterialAO;
	GBuffer.GBufferAO = AOMultiBounce( Luminance( GBuffer.SpecularColor ), SpecOcclusion ).g;

	half3 Color = 0;
	float IndirectIrradiance = 0;

#if !MATERIAL_SHADINGMODEL_UNLIT
	float3 DiffuseDir = MaterialParameters.WorldNormal;
	float3 DiffuseColorForIndirect = GBuffer.DiffuseColor;
	float3 DiffuseIndirectLighting;
	float3 SubsurfaceIndirectLighting;
	GetPrecomputedIndirectLightingAndSkyLight(MaterialParameters, Interpolants, BasePassInterpolants, LightmapVTPageTableResult, 
		GBuffer, DiffuseDir, VolumetricLightmapBrickTextureUVs, DiffuseIndirectLighting, SubsurfaceIndirectLighting, IndirectIrradiance);
	Color += (DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor) * AOMultiBounce( GBuffer.BaseColor, DiffOcclusion );
#endif
	Color += GetMaterialEmissive(PixelMaterialInputs);
#if MATERIAL_SHADINGMODEL_DEFAULT_LIT && (MATERIALBLENDING_TRANSLUCENT || MATERIALBLENDING_ADDITIVE) // Volume lighting for lit translucency
	if (GBuffer.ShadingModelID == SHADINGMODELID_DEFAULT_LIT)
	{
		Color += GetTranslucencyVolumeLighting(MaterialParameters, PixelMaterialInputs, BasePassInterpolants, GBuffer, IndirectIrradiance);
	}
#endif
	
#if NEEDS_BASEPASS_VERTEX_FOGGING
	float4 HeightFogging = BasePassInterpolants.VertexFog;
#else
	float4 HeightFogging = float4(0,0,0,1);
#endif
	float4 Fogging = HeightFogging;
#if MATERIALBLENDING_ANY_TRANSLUCENT
	if (FogStruct.ApplyVolumetricFog > 0) 
	{
		float3 VolumeUV = ComputeVolumeUV(MaterialParameters.AbsoluteWorldPosition, ResolvedView.WorldToClip);
		Fogging = CombineVolumetricFog(HeightFogging, VolumeUV, EyeIndex);
	}
#endif

#if MATERIALBLENDING_ALPHAHOLDOUT
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb * Opacity, Opacity);
#elif MATERIALBLENDING_ALPHACOMPOSITE
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb * Opacity, Opacity);
#elif MATERIALBLENDING_TRANSLUCENT
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb, Opacity);
#elif MATERIALBLENDING_ADDITIVE
	Out.MRT[0] = half4(Color * Fogging.a * Opacity, 0.0f);
#elif MATERIALBLENDING_MODULATE
	Out.MRT[0] = half4(lerp(float3(1, 1, 1), Color, Fogging.aaa * Fogging.aaa), Opacity);
#else
	FLightAccumulator LightAccumulator = (FLightAccumulator)0;
	Color = Color * Fogging.a + Fogging.rgb;
	LightAccumulator_Add(LightAccumulator, Color, 0, 1.0f, false);
	Out.MRT[0] = LightAccumulator_GetResult(LightAccumulator);
#endif

#if USES_GBUFFER
	GBuffer.IndirectIrradiance = IndirectIrradiance;
	float QuantizationBias = PseudoRandom( MaterialParameters.SvPosition.xy ) - 0.5f; // -0.5 .. 0.5, could be optimzed as lower quality noise would be sufficient
	EncodeGBuffer(GBuffer, Out.MRT[1], Out.MRT[2], Out.MRT[3], OutGBufferD, OutGBufferE, OutVelocity, QuantizationBias);
	Out.MRT[4] = OutGBufferD;
	#if GBUFFER_HAS_PRECSHADOWFACTOR
		Out.MRT[5] = OutGBufferE;
	#endif
#endif

#if !MATERIALBLENDING_MODULATE && USE_PREEXPOSURE
	const float ViewPreExposure = View.PreExposure;
	// We need to multiply pre-exposure by all components including A, otherwise the ratio of diffuse to specular lighting will get messed up in the SSS pass.
	// RGB: Full color (Diffuse + Specular)
	// A:   Diffuse Intensity, but only if we are not blending
	#if MATERIALBLENDING_ALPHAHOLDOUT || MATERIALBLENDING_ALPHACOMPOSITE || MATERIALBLENDING_TRANSLUCENT || MATERIALBLENDING_ADDITIVE
		Out.MRT[0].rgb  *= ViewPreExposure;
	#else
		Out.MRT[0].rgba *= ViewPreExposure;
	#endif
#endif
}

