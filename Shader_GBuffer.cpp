#define USES_GBUFFER 					(MATERIALBLENDING_SOLID || MATERIALBLENDING_MASKED)  // BLEND_Opaque or BLEND_Masked
#define SELECTIVE_BASEPASS_OUTPUTS 		TEXT("r.SelectiveBasePassOutputs") 	// default 0
#define GBUFFER_HAS_VELOCITY			TEXT("r.BasePassOutputsVelocity")	// default 0
#define GBUFFER_HAS_TANGENT				TEXT("r.AnisotropicBRDF") && !GBUFFER_HAS_VELOCITY

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


void EncodeGBuffer(
	FGBufferData GBuffer,
	out float4 OutGBufferA,
	out float4 OutGBufferB,
	out float4 OutGBufferC,
	out float4 OutGBufferD,
	out float4 OutGBufferE,
	out float4 OutGBufferF,
	out float4 OutGBufferVelocity,
	float QuantizationBias = 0		// -0.5 to 0.5 random float. Used to bias quantization.
	)
{
	if (GBuffer.ShadingModelID == SHADINGMODELID_UNLIT)
	{
		OutGBufferA = 0;
		SetGBufferForUnlit(OutGBufferB);
		OutGBufferC = 0;
		OutGBufferD = 0;
		OutGBufferE = 0;
		OutGBufferF = 0.5f;
	}
	else
	{
		OutGBufferA.rgb = EncodeNormal( GBuffer.WorldNormal );
		OutGBufferA.a = GBuffer.PerObjectGBufferData;

		OutGBufferB.r = GBuffer.Metallic;
		OutGBufferB.g = GBuffer.Specular;
		OutGBufferB.b = GBuffer.Roughness;
		OutGBufferB.a = EncodeShadingModelIdAndSelectiveOutputMask(GBuffer.ShadingModelID, GBuffer.SelectiveOutputMask);

		OutGBufferC.rgb = EncodeBaseColor( GBuffer.BaseColor );
#if ALLOW_STATIC_LIGHTING // No space for AO. Multiply IndirectIrradiance by AO instead of storing.
		OutGBufferC.a = EncodeIndirectIrradiance(GBuffer.IndirectIrradiance * GBuffer.GBufferAO) + QuantizationBias * (1.0 / 255.0);
#else
		OutGBufferC.a = GBuffer.GBufferAO;
#endif

		OutGBufferD = GBuffer.CustomData;
		OutGBufferE = GBuffer.PrecomputedShadowFactors;

#if GBUFFER_HAS_TANGENT
		OutGBufferF.rgb = EncodeNormal(GBuffer.WorldTangent);
		OutGBufferF.a = GBuffer.Anisotropy * 0.5f + 0.5f;
#else
		OutGBufferF = 0;
#endif
	}

#if WRITES_VELOCITY_TO_GBUFFER
	OutGBufferVelocity = GBuffer.Velocity;
#else
	OutGBufferVelocity = 0;
#endif
}

void FPixelShaderInOut_MainPS(
	FVertexFactoryInterpolantsVSToPS Interpolants,
	FBasePassInterpolantsVSToPS BasePassInterpolants,
	in FPixelShaderIn In,
	inout FPixelShaderOut Out)
{
	float4 OutVelocity = 0;// Velocity
	float4 OutGBufferD = 0;// CustomData
	float4 OutGBufferE = 0;// PreShadowFactor
	float4 OutGBufferF = 0;// Wolrd Space Tangent 
	FGBufferData GBuffer = (FGBufferData)0;
	
	...

#if USES_GBUFFER
	EncodeGBuffer(GBuffer, Out.MRT[1], Out.MRT[2], Out.MRT[3], OutGBufferD, OutGBufferE, OutGBufferF, OutVelocity, QuantizationBias);

	#if GBUFFER_HAS_VELOCITY
	Out.MRT[4] = OutVelocity;
	#elif GBUFFER_HAS_TANGENT
	Out.MRT[4] = OutGBufferF;
	#endif

	Out.MRT[GBUFFER_HAS_VELOCITY || GBUFFER_HAS_TANGENT ? 5 : 4] = OutGBufferD;

	#if GBUFFER_HAS_PRECSHADOWFACTOR
		Out.MRT[GBUFFER_HAS_VELOCITY || GBUFFER_HAS_TANGENT ? 6 : 5] = OutGBufferE;
	#endif
#endif
}





FGBufferData DecodeGBufferData(
	float4 InGBufferA,
	float4 InGBufferB,
	float4 InGBufferC,
	float4 InGBufferD,
	float4 InGBufferE,
	float4 InGBufferF,
	float4 InGBufferVelocity,
	float CustomNativeDepth,
	uint CustomStencil,
	float SceneDepth,
	bool bGetNormalizedNormal,
	bool bChecker)
{
	FGBufferData GBuffer;

	GBuffer.WorldNormal = DecodeNormal( InGBufferA.xyz );
	if(bGetNormalizedNormal)
	{
		GBuffer.WorldNormal = normalize(GBuffer.WorldNormal);
	}

	GBuffer.PerObjectGBufferData = InGBufferA.a;  
	GBuffer.Metallic	= InGBufferB.r;
	GBuffer.Specular	= InGBufferB.g;
	GBuffer.Roughness	= InGBufferB.b;
	GBuffer.ShadingModelID = DecodeShadingModelId(InGBufferB.a);
	GBuffer.SelectiveOutputMask = DecodeSelectiveOutputMask(InGBufferB.a);

	GBuffer.BaseColor = DecodeBaseColor(InGBufferC.rgb);

#if ALLOW_STATIC_LIGHTING
	GBuffer.GBufferAO = 1;
	GBuffer.IndirectIrradiance = DecodeIndirectIrradiance(InGBufferC.a);
#else
	GBuffer.GBufferAO = InGBufferC.a;
	GBuffer.IndirectIrradiance = 1;
#endif

	GBuffer.CustomData = !(GBuffer.SelectiveOutputMask & SKIP_CUSTOMDATA_MASK) ? InGBufferD : 0;

	GBuffer.PrecomputedShadowFactors = !(GBuffer.SelectiveOutputMask & SKIP_PRECSHADOW_MASK) ? InGBufferE :  ((GBuffer.SelectiveOutputMask & ZERO_PRECSHADOW_MASK) ? 0 :  1);
	GBuffer.CustomDepth = ConvertFromDeviceZ(CustomNativeDepth);
	GBuffer.CustomStencil = CustomStencil;
	GBuffer.Depth = SceneDepth;

	GBuffer.StoredBaseColor = GBuffer.BaseColor;
	GBuffer.StoredMetallic = GBuffer.Metallic;
	GBuffer.StoredSpecular = GBuffer.Specular;

	if( GBuffer.ShadingModelID == SHADINGMODELID_EYE )
	{
		GBuffer.Metallic = 0.0;
#if IRIS_NORMAL
		GBuffer.Specular = 0.25;
#endif
	}

	// derived from BaseColor, Metalness, Specular
	{
		if (UseSubsurfaceProfile(GBuffer.ShadingModelID))
		{
			AdjustBaseColorAndSpecularColorForSubsurfaceProfileLighting(GBuffer.BaseColor, GBuffer.SpecularColor, GBuffer.Specular, bChecker);
		}
		GBuffer.DiffuseColor  = lerp(     GBuffer.BaseColor,                 0, GBuffer.Metallic);
		GBuffer.SpecularColor = lerp(0.08f*GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);

		#if USE_DEVELOPMENT_SHADERS
		{
			// this feature is only needed for development/editor - we can compile it out for a shipping build (see r.CompileShadersForDevelopment cvar help)
			GBuffer.DiffuseColor = GBuffer.DiffuseColor * View.DiffuseOverrideParameter.www + View.DiffuseOverrideParameter.xyz;
			GBuffer.SpecularColor = GBuffer.SpecularColor * View.SpecularOverrideParameter.w + View.SpecularOverrideParameter.xyz;
		}
		#endif //USE_DEVELOPMENT_SHADERS
	}

#if GBUFFER_HAS_TANGENT
	GBuffer.WorldTangent = DecodeNormal(InGBufferF.rgb);
	GBuffer.Anisotropy = InGBufferF.a * 2.0f - 1.0f;

	if(bGetNormalizedNormal)
	{
		GBuffer.WorldTangent = normalize(GBuffer.WorldTangent);
	}
#else
	GBuffer.WorldTangent = 0;
	GBuffer.Anisotropy = 0;
#endif

	GBuffer.Velocity = !(GBuffer.SelectiveOutputMask & SKIP_VELOCITY_MASK) ? InGBufferVelocity : 0;

	return GBuffer;
}



FGBufferData GetGBufferData(float2 UV, bool bGetNormalizedNormal = true)
{
	float4 GBufferA = Texture2DSampleLevel(SceneTexturesStruct.GBufferATexture, SceneTexturesStruct_GBufferATextureSampler, UV, 0);
	float4 GBufferB = Texture2DSampleLevel(SceneTexturesStruct.GBufferBTexture, SceneTexturesStruct_GBufferBTextureSampler, UV, 0);
	float4 GBufferC = Texture2DSampleLevel(SceneTexturesStruct.GBufferCTexture, SceneTexturesStruct_GBufferCTextureSampler, UV, 0);
	float4 GBufferD = Texture2DSampleLevel(SceneTexturesStruct.GBufferDTexture, SceneTexturesStruct_GBufferDTextureSampler, UV, 0);
	float CustomNativeDepth = Texture2DSampleLevel(SceneTexturesStruct.CustomDepthTexture, SceneTexturesStruct_CustomDepthTextureSampler, UV, 0).r;

	int2 IntUV = (int2)trunc(UV * View.BufferSizeAndInvSize.xy);
	uint CustomStencil = SceneTexturesStruct.CustomStencilTexture.Load(int3(IntUV, 0)) STENCIL_COMPONENT_SWIZZLE;

	#if ALLOW_STATIC_LIGHTING
		float4 GBufferE = Texture2DSampleLevel(SceneTexturesStruct.GBufferETexture, SceneTexturesStruct_GBufferETextureSampler, UV, 0);
	#else
		float4 GBufferE = 1;
	#endif

	float4 GBufferF = Texture2DSampleLevel(SceneTexturesStruct.GBufferFTexture, SceneTexturesStruct_GBufferFTextureSampler, UV, 0);

	#if WRITES_VELOCITY_TO_GBUFFER
		float4 GBufferVelocity = Texture2DSampleLevel(SceneTexturesStruct.GBufferVelocityTexture, SceneTexturesStruct_GBufferVelocityTextureSampler, UV, 0);
	#else
		float4 GBufferVelocity = 0;
	#endif

	float SceneDepth = CalcSceneDepth(UV);
	
	return DecodeGBufferData(GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, GBufferF, GBufferVelocity, CustomNativeDepth, CustomStencil, SceneDepth, bGetNormalizedNormal, CheckerFromSceneColorUV(UV));
}

FGBufferData GetGBufferDataFromSceneTextures(float2 UV, bool bGetNormalizedNormal = true)
{
	float4 GBufferA = GBufferATexture.SampleLevel(GBufferATextureSampler, UV, 0);
	float4 GBufferB = GBufferBTexture.SampleLevel(GBufferBTextureSampler, UV, 0);
	float4 GBufferC = GBufferCTexture.SampleLevel(GBufferCTextureSampler, UV, 0);
	float4 GBufferD = GBufferDTexture.SampleLevel(GBufferDTextureSampler, UV, 0);
	float4 GBufferE = GBufferETexture.SampleLevel(GBufferETextureSampler, UV, 0);
	float4 GBufferF = GBufferFTexture.SampleLevel(GBufferFTextureSampler, UV, 0);
	float4 GBufferVelocity = GBufferVelocityTexture.SampleLevel(GBufferVelocityTextureSampler, UV, 0);

	uint CustomStencil = 0;
	float CustomNativeDepth = 0;

	float DeviceZ = SampleDeviceZFromSceneTextures(UV);

	float SceneDepth = ConvertFromDeviceZ(DeviceZ);

	return DecodeGBufferData(GBufferA, GBufferB, GBufferC, GBufferD, GBufferE, GBufferF, GBufferVelocity, CustomNativeDepth, CustomStencil, SceneDepth, bGetNormalizedNormal, CheckerFromSceneColorUV(UV));
}
