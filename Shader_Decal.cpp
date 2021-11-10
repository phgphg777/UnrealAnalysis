

struct FPixelShaderIn
{
	float4 SvPosition;
	uint Coverage;
	bool bIsFrontFace;
};
struct FPixelShaderOut
{
	float4 MRT[8];	// [0..7], only usable if PIXELSHADEROUTPUT_MRT0, PIXELSHADEROUTPUT_MRT1, ... is 1
	uint Coverage;	// Pixel Shader OutCoverage, only usable if PIXELSHADEROUTPUT_COVERAGE is 1
	float Depth;	// Pixel Shader OutDepth
};

// all values that are output by the forward rendering pass
struct FGBufferData
{
	// normalized
	float3 WorldNormal;
	// 0..1 (derived from BaseColor, Metalness, Specular)
	float3 DiffuseColor;
	// 0..1 (derived from BaseColor, Metalness, Specular)
	float3 SpecularColor;
	// 0..1, white for SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE (apply BaseColor after scattering is more correct and less blurry)
	float3 BaseColor;
	// 0..1
	float Metallic;
	// 0..1
	float Specular;
	// 0..1
	float4 CustomData;
	// Indirect irradiance luma
	float IndirectIrradiance;
	// Static shadow factors for channels assigned by Lightmass
	// Lights using static shadowing will pick up the appropriate channel in their deferred pass
	float4 PrecomputedShadowFactors;
	// 0..1
	float Roughness;
	// 0..1 ambient occlusion  e.g.SSAO, wet surface mask, skylight mask, ...
	float GBufferAO;
	// 0..255 
	uint ShadingModelID;
	// 0..255 
	uint SelectiveOutputMask;
	// 0..1, 2 bits, use HasDistanceFieldRepresentation(GBuffer) or HasDynamicIndirectShadowCasterRepresentation(GBuffer) to extract
	float PerObjectGBufferData;
	// in world units
	float CustomDepth;
	// Custom depth stencil value
	uint CustomStencil;
	// in unreal units (linear), can be used to reconstruct world position,
	// only valid when decoding the GBuffer as the value gets reconstructed from the Z buffer
	float Depth;
	// Velocity for motion blur (only used when WRITES_VELOCITY_TO_GBUFFER is enabled)
	float4 Velocity;

	// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply BaseColor later
	float3 StoredBaseColor;
	// 0..1, only needed by SHADINGMODELID_SUBSURFACE_PROFILE and SHADINGMODELID_EYE which apply Specular later
	float StoredSpecular;
	// 0..1, only needed by SHADINGMODELID_EYE which encodes Iris Distance inside Metallic
	float StoredMetallic;
};




void CalcPixelMaterialInputs(in out FMaterialPixelParameters Parameters, in out FPixelMaterialInputs PixelMaterialInputs)
{
	PixelMaterialInputs.Normal =  float3 (0.00000000,0.00000000,1.00000000);
	float3 MaterialNormal = GetMaterialNormal(Parameters, PixelMaterialInputs);
	MaterialNormal = normalize(MaterialNormal);
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal);
	Parameters.WorldNormal *= Parameters.TwoSidedSign;
	Parameters.ReflectionVector = ReflectionAboutCustomWorldNormal(Parameters, Parameters.WorldNormal, false); 
	Parameters.Particle.MotionBlurFade = 1.0f;

	PixelMaterialInputs.EmissiveColor = Material_VectorExpressions[2].rgb; 		
	PixelMaterialInputs.Opacity = 1.00000000;
	PixelMaterialInputs.OpacityMask = 1.00000000;	
	PixelMaterialInputs.BaseColor =  float3 (0.16145833,0.01131073,0.02256475);
	PixelMaterialInputs.Metallic = 0.00000000; 	
	PixelMaterialInputs.Specular = 0.50000000; 
	PixelMaterialInputs.Roughness = 0.50000000;
	PixelMaterialInputs.Subsurface = 0;
	PixelMaterialInputs.AmbientOcclusion = 1.00000000;
	PixelMaterialInputs.Refraction =  float2 ( float2 (1.00000000,0.00000000).r,Material_ScalarExpressions[0].x);
	PixelMaterialInputs.PixelDepthOffset = 0.00000000;
}


void FPixelShaderInOut_MainPS(inout FPixelShaderIn In, inout FPixelShaderOut Out)
{
	ResolvedView = ResolveView();

	float2 ScreenUV = SvPositionToBufferUV(In.SvPosition);
	
	// make SvPosition appear to be rasterized with the depth from the depth buffer
	In.SvPosition.z = LookupDeviceZ(ScreenUV);

	float3 OSPosition;
	float3 WSPosition;
	float HitT;

	{
		float4 DecalVectorHom = mul(float4(In.SvPosition.xyz,1), SvPositionToDecal);
		OSPosition = DecalVectorHom.xyz / DecalVectorHom.w;

		// clip content outside the decal
		// not needed if we stencil out the decal but we do that only on large (screen space) ones
		clip(OSPosition.xyz + 1.0f);
		clip(1.0f - OSPosition.xyz);

		// todo: TranslatedWorld would be better for quality
		WSPosition = SvPositionToWorld(In.SvPosition);
	}

	float3 CameraVector = normalize(View.WorldCameraOrigin - WSPosition);

	// can be optimized
	float3 DecalVector = OSPosition * 0.5f + 0.5f;

	// Swizzle so that DecalVector.xy are perpendicular to the projection direction and DecalVector.z is distance along the projection direction
	float3 SwizzlePos = DecalVector.zyx;

	// By default, map textures using the vectors perpendicular to the projection direction
	float2 DecalUVs = SwizzlePos.xy;

	FMaterialPixelParameters MaterialParameters = MakeInitializedMaterialPixelParameters();
	{
		MaterialParameters.TwoSidedSign = 1;
		MaterialParameters.VertexColor = 1;
		MaterialParameters.CameraVector = CameraVector;
		MaterialParameters.SvPosition = In.SvPosition;
		MaterialParameters.ScreenPosition = SvPositionToScreenPosition(In.SvPosition);
		MaterialParameters.LightVector = SwizzlePos;
		MaterialParameters.AbsoluteWorldPosition = 
			MaterialParameters.WorldPosition_CamRelative = 
			MaterialParameters.WorldPosition_NoOffsets = 
			MaterialParameters.WorldPosition_NoOffsets_CamRelative = WSPosition;
	}
	FPixelMaterialInputs PixelMaterialInputs;
	CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

	float DecalFading = saturate(4 - 4 * abs(SwizzlePos.z * 2 - 1)) * DecalParams.x * DecalParams.y;
	float Opacity = GetMaterialOpacity(PixelMaterialInputs) * DecalFading;

	{
		Out.MRT[0].xyz = GetMaterialEmissive(PixelMaterialInputs);
		//Out.MRT[1].rgb = EncodeNormal( MaterialParameters.WorldNormal );
		Out.MRT[2].rgb = float3(
			GetMaterialMetallic(PixelMaterialInputs), 
			GetMaterialSpecular(PixelMaterialInputs), 
			GetMaterialRoughness(PixelMaterialInputs) );
		Out.MRT[3].rgb = EncodeBaseColor( GetMaterialBaseColor(PixelMaterialInputs) );
		#if DECAL_BLEND_MODE == DECALBLENDMODEID_STAIN
		//Out.MRT[3].rgb *= Opacity;
		#endif

		Out.MRT[0].a = Opacity;	// Emissive
		//Out.MRT[1].a = Opacity;	// Normal
		Out.MRT[2].a = Opacity;	// Metallic, Specular, Roughness
		Out.MRT[3].a = Opacity;	// BaseColor
	}
}


void MainPS
	(
		in float4 SvPosition : SV_Position		// after all interpolators
		, in bool bIsFrontFace : SV_IsFrontFace
		, out float4 OutTarget0 : SV_Target0
		//, out float4 OutTarget0 : SV_Target1
		, out float4 OutTarget2 : SV_Target2
		, out float4 OutTarget3 : SV_Target3
	)
{
	// ---------------------------------------------------------------------------------

	FPixelShaderIn PixelShaderIn = (FPixelShaderIn) 0;
	FPixelShaderOut PixelShaderOut = (FPixelShaderOut) 0;

	PixelShaderIn.SvPosition = SvPosition;
	PixelShaderIn.bIsFrontFace = bIsFrontFace;

	FPixelShaderInOut_MainPS(PixelShaderIn, PixelShaderOut);

	OutTarget0 = PixelShaderOut.MRT[0];
	//OutTarget1 = PixelShaderOut.MRT[1];
	OutTarget2 = PixelShaderOut.MRT[2];
	OutTarget3 = PixelShaderOut.MRT[3];
}