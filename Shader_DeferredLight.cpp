FDeferredLightData SetupLightDataForStandardDeferred()
{
	FDeferredLightData LightData;
	LightData.Position = DeferredLightUniforms.Position;
	LightData.InvRadius = DeferredLightUniforms.InvRadius;
	LightData.Color = DeferredLightUniforms.Color;
	LightData.FalloffExponent = DeferredLightUniforms.FalloffExponent;
	LightData.Direction = DeferredLightUniforms.Direction;
	LightData.Tangent = DeferredLightUniforms.Tangent;
	LightData.SpotAngles = DeferredLightUniforms.SpotAngles;
	LightData.SourceRadius = DeferredLightUniforms.SourceRadius;
	LightData.SourceLength = DeferredLightUniforms.SourceLength;
    LightData.SoftSourceRadius = DeferredLightUniforms.SoftSourceRadius;
	LightData.SpecularScale = DeferredLightUniforms.SpecularScale;
	LightData.ContactShadowLength = abs(DeferredLightUniforms.ContactShadowLength);
	LightData.ContactShadowLengthInWS = DeferredLightUniforms.ContactShadowLength < 0.0f;
	LightData.DistanceFadeMAD = DeferredLightUniforms.DistanceFadeMAD;
	LightData.ShadowMapChannelMask = DeferredLightUniforms.ShadowMapChannelMask;
	LightData.ShadowedBits = DeferredLightUniforms.ShadowedBits;
	LightData.RectLightBarnCosAngle = DeferredLightUniforms.RectLightBarnCosAngle;
	LightData.RectLightBarnLength = DeferredLightUniforms.RectLightBarnLength;
	LightData.bInverseSquared = INVERSE_SQUARED_FALLOFF;
	LightData.bRadialLight = LIGHT_SOURCE_SHAPE > 0;
	LightData.bSpotLight = LIGHT_SOURCE_SHAPE > 0;
	LightData.bRectLight = LIGHT_SOURCE_SHAPE == 2;
	LightData.HairTransmittance = InitHairTransmittanceData();
	return LightData;
}

struct FDeferredLightData {
	float3 Position;
	float  InvRadius;
	float3 Color;
	float  FalloffExponent;
	float3 Direction;
	float3 Tangent;
    float SoftSourceRadius;
	float2 SpotAngles;
	float SourceRadius;
	float SourceLength;
	float SpecularScale;
	float ContactShadowLength;
	float2 DistanceFadeMAD;
	float4 ShadowMapChannelMask;
	uint ShadowedBits;/** Whether the light should apply shadowing. */
	float RectLightBarnCosAngle;
	float RectLightBarnLength;
	bool ContactShadowLengthInWS;/** Whether ContactShadowLength is in World Space or in Screen Space. */
	bool bInverseSquared;/** Whether to use inverse squared falloff. */
	bool bRadialLight;/** Whether this is a light with radial attenuation, aka point or spot light. */
	bool bSpotLight;/** Whether this light needs spotlight attenuation. */
	bool bRectLight;
	FHairTransmittanceData HairTransmittance;
};

void RadialVertexMain(
	in uint InVertexId : SV_VertexID,
	in float3 InPosition : ATTRIBUTE0,
	out float4 OutPosition : SV_POSITION,
	out float4 OutScreenPosition : TEXCOORD0)
{
	float3 TranslatedWorldPosition; // WorldPosition - CameraOrigin
	
	if (StencilingConeParameters.x == 0) 
	{
		TranslatedWorldPosition = InPosition * StencilingGeometryPosAndScale.w + StencilingGeometryPosAndScale.xyz;
	}
	else {...}

	OutScreenPosition = OutPosition = mul(float4(TranslatedWorldPosition, 1), View.TranslatedWorldToClip); // Clip space position
}

void DirectionalVertexMain(
	in float2 InPosition : ATTRIBUTE0,
	in float2 InUV       : ATTRIBUTE1,
	out float4 OutPosition : SV_POSITION,
	out float2 OutUV : TEXCOORD0,
	out float3 OutScreenVector : TEXCOORD1)
{	
	DrawRectangle(InPosition, InUV, OutPosition, OutUV);
	OutScreenVector = mul(float4(OutPosition.xy, 1, 0), View.ScreenToTranslatedWorld).xyz; // ray direction from camera w.r.t world coordinates
}


#define USE_ATMOSPHERE_TRANSMITTANCE 0
#define USE_LIGHTING_CHANNELS 1
#define USE_HAIR_LIGHTING 0
#define NON_DIRECTIONAL_DIRECT_LIGHTING 0 // It can be 1 only in Forward shading
#define REFERENCE_QUALITY 0
#define SUPPORT_CONTACT_SHADOWS 0 // Actually, it is 1
#define GBUFFER_HAS_TANGENT 0

void DeferredLightPixelMain(
#if LIGHT_SOURCE_SHAPE > 0
	float4 ScreenPosition : TEXCOORD0,
#else
	float2 UV				: TEXCOORD0,
	float3 ScreenVector		: TEXCOORD1,
#endif
	float4 SVPos			: SV_POSITION,
	out float4 OutColor		: SV_Target0 )
// ( ScreenPosition.x / ScreenPosition.w + 1) * 0.5 * ViewRect.Width == SVPos.x
// (-ScreenPosition.y / ScreenPosition.w + 1) * 0.5 * ViewRect.Height == SVPos.y
{
	float2 ScreenUV;
	float3 WorldPosition;
	float3 CameraVector;

#if LIGHT_SOURCE_SHAPE > 0
	ScreenUV = ScreenPosition.xy / ScreenPosition.w * View.ScreenPositionScaleBias.xy + View.ScreenPositionScaleBias.wz;
	float SceneDepth = CalcSceneDepth(UV);
	WorldPosition = mul(float4(ScreenPosition.xy / ScreenPosition.w * SceneDepth, SceneDepth, 1), View.ScreenToWorld).xyz;
	CameraVector = normalize(Out.WorldPosition - View.WorldCameraOrigin);
#else
	ScreenUV = UV;
	float SceneDepth = CalcSceneDepth(ScreenUV);
	WorldPosition = ScreenVector * SceneDepth + View.WorldCameraOrigin;
	CameraVector = normalize(ScreenVector);
#endif	

	OutColor = 0;
	FScreenSpaceData SSData = GetScreenSpaceData(ScreenUV);
	
	if( SSData.GBuffer.ShadingModelID > 0 
		&& (GetLightingChannelMask(ScreenUV) & DeferredLightUniforms.LightingChannelMask) )
	{
		FDeferredLightData LightData = SetupLightDataForStandardDeferred();
		float Dither = InterleavedGradientNoise(SVPos.xy, View.StateFrameIndexMod8 ); // ??
		FRectTexture RectTexture = { DeferredLightUniforms.SourceTexture, };
		float4 ShadowAttenuation = Square(Texture2DSampleLevel(LightAttenuationTexture, LightAttenuationTextureSampler, ScreenUV, 0));
		float IESAttenuation = ComputeLightProfileMultiplier(...);

		const float4 Radiance = GetDynamicLighting(
			WorldPosition, 
			CameraVector, 
			SSData.GBuffer, 
			SSData.AmbientOcclusion, , 
			LightData, 
			ShadowAttenuation, 
			Dither, , 
			RectTexture, );
		
		OutColor = Radiance * IESAttenuation * GetExposure();
	}
}

void LightAccumulator_Add(
	inout FLightAccumulator In, 
	float3 TotalLight,
	float3 ScatterableLight, 
	float3 CommonMultiplier, 
	bool bUseSubsurfaceProfile)
{
	In.TotalLight += TotalLight * CommonMultiplier;
	if (bUseSubsurfaceProfile && View.bCheckerboardSubsurfaceProfileRendering == 0)
	{
		In.ScatterableLightLuma += Luminance(ScatterableLight * CommonMultiplier);
	}
}

float GetLocalLightAttenuation(
	float3 WorldPosition, 
	FDeferredLightData LightData, 
	float3 ToLight, 
	float3 L)
{
	if(!LightData.bRadialLight)
		return 1.f;

	float RelativeDistanceSqr = saturate(dot(ToLight, ToLight) * Square(LightData.InvRadius)); // (r/R)^2

	float LightMask = LightData.bInverseSquared
		? Square(1 - Square(RelativeDistanceSqr)) // (1-(r/R)^4)^2
		: pow(1 - RelativeDistanceSqr, LightData.FalloffExponent); // (1-(r/R)^2)^exponent

	if (LightData.bSpotLight)
	{
		// [(dot(L, NegativeSpotDir) - CosOuterCone) / (CosInnerCone - CosOuterCone)]^2
		LightMask *= Square( saturate( (dot(L, LightData.Direction) - LightData.SpotAngles.x) * LightData.SpotAngles.y ) );
	}

	if(LightData.bRectLight && dot(LightData.Direction, L) < 0)
	{
		LightMask = 0;
	}

	return LightMask;
}

float4 GetDynamicLighting(
	float3 WorldPosition, 
	float3 CameraVector, 
	FGBufferData GBuffer, 
	float AmbientOcclusion, , 
	FDeferredLightData LightData, 
	float4 ShadowAttenuation, 
	float Dither, , 
	FRectTexture SourceTexture, )
{
	FLightAccumulator LightAccumulator = (FLightAccumulator) 0;

	float3 V = -CameraVector;
	float3 N = GBuffer.WorldNormal;
	float3 ToLight = LightData.bRadialLight ? LightData.Position - WorldPosition : LightData.Direction;
	float3 L = normalize(ToLight);
	float IntrinsicFalloff = GetLocalLightAttenuation(WorldPosition, LightData, ToLight, L);
	
	if( IntrinsicFalloff > 0 )
	{
		FShadowTerms Shadow;
		Shadow.SurfaceShadow = AmbientOcclusion;
		Shadow.TransmissionShadow = 1;
		Shadow.TransmissionThickness = 1;
		Shadow.HairTransmittance.Transmittance = 1;
		Shadow.HairTransmittance.OpaqueVisibility = 1;
		GetShadowTerms(GBuffer, LightData, WorldPosition, L, ShadowAttenuation, Dither, Shadow);
		
		if( Shadow.SurfaceShadow + Shadow.TransmissionShadow > 0 )
		{
			bool bUseSubsurfaceProfile = GBuffer.ShadingModelID == SHADINGMODELID_SUBSURFACE_PROFILE || GBuffer.ShadingModelID == SHADINGMODELID_EYE;
			
			FDirectLighting Lighting = !LightData.bRectLight ?
				IntegrateBxDF(GBuffer, N, V, ToLight, , LightData.bInverseSquared) :
				IntegrateBxDF(GBuffer, N, V, GetRect(ToLight, LightData), , SourceTexture);
			
			LightAccumulator_Add( 
				LightAccumulator, 
				Lighting.Diffuse + Lighting.Specular * LightData.SpecularScale, 
				Lighting.Diffuse, 
				LightData.Color * IntrinsicFalloff * Shadow.SurfaceShadow, 
				bUseSubsurfaceProfile );
			LightAccumulator_Add( 
				LightAccumulator, 
				Lighting.Transmission, 
				Lighting.Transmission, 
				LightData.Color * IntrinsicFalloff * Shadow.TransmissionShadow, 
				bUseSubsurfaceProfile );
		}
	}

	return float4(LightAccumulator.TotalLight, LightAccumulator.ScatterableLightLuma);
}

// in brief:
float3 GetDynamicLighting()
{
	float IntrinsicFalloff = GetLocalLightAttenuation(WorldPosition, LightData, ToLight, L);
	FShadowTerms Shadow;
	GetShadowTerms(GBuffer, LightData, WorldPosition, L, ShadowAttenuation, Dither, Shadow);

	FDirectLighting Albedo = !LightData.bRectLight ?
		IntegrateBxDF(GBuffer, N, V, ToLight, , LightData.bInverseSquared) :
		IntegrateBxDF(GBuffer, N, V, GetRect(ToLight, LightData), , SourceTexture);

	return LightData.Color * (Albedo.Diffuse + Albedo.Specular) * IntrinsicFalloff * Shadow.SurfaceShadow;
}

FDirectLighting IntegrateBxDF(FGBufferData GBuffer, half3 N, half3 V, 
	float3 ToLight, , bool bInverseSquared)
{
	float3 L;
	float Irradiance;
	{
		float DistSqr = dot(ToLight, ToLight);
		L = ToLight / sqrt( DistSqr );
		Irradiance = saturate(dot(N, L)) * (bInverseSquared ? rcp( DistSqr + 1 ) : 1);
	}

	FAreaLight AreaLight = (FAreaLight)0;
	AreaLight.LineCosSubtended = 1;
	AreaLight.FalloffColor = 1;

	return DefaultLitBxDF(GBuffer, N, V, L, Irradiance, AreaLight, );
}

FDirectLighting IntegrateBxDF(FGBufferData GBuffer, half3 N, half3 V, 
	FRect Rect, , FRectTexture SourceTexture )
{
	float Irradiance;
	float3 L = RectIrradianceLambert( N, Rect, Irradiance );
	
	FAreaLight AreaLight = (AreaLight)0;
	AreaLight.LineCosSubtended = 1;
	AreaLight.FalloffColor = SampleSourceTexture( L, Rect, SourceTexture );
	AreaLight.Rect = Rect;
	AreaLight.Texture = SourceTexture;
	AreaLight.bIsRect = true;

	return DefaultLitBxDF(GBuffer, N, V, L, Irradiance, AreaLight, );
}

// GBuffer.DiffuseColor  = lerp(BaseColor, 0, Metallic);
// GBuffer.SpecularColor = lerp(float3(0.08f*Specular), BaseColor, Metallic); 
FDirectLighting DefaultLitBxDF(FGBufferData GBuffer, half3 N, half3 V, 
	half3 L, float Irradiance, FAreaLight AreaLight, )
{	
	BxDFContext Context;
	Init( Context, N, V, L );
	Context.NoV = saturate( abs( Context.NoV ) + 1e-5 );

	FDirectLighting Albedo = (FDirectLighting) 0;
	Albedo.Diffuse  = AreaLight.FalloffColor * Irradiance * Diffuse_Lambert( GBuffer.DiffuseColor ); 
	Albedo.Specular = AreaLight.bIsRect ? 
		RectGGXApproxLTC( GBuffer.Roughness, GBuffer.SpecularColor, N, V, AreaLight.Rect, AreaLight.Texture ) : 
		Irradiance * SpecularGGX( GBuffer.Roughness, GBuffer.SpecularColor, Context, NoL, AreaLight ) ;
	return Albedo;
}

