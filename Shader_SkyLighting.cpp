


float3 GetSkySHDiffuse(float3 Normal)
{
	float4 NormalVector = float4(Normal, 1);

	float3 Intermediate0, Intermediate1, Intermediate2;
	Intermediate0.x = dot(View.SkyIrradianceEnvironmentMap[0], NormalVector);
	Intermediate0.y = dot(View.SkyIrradianceEnvironmentMap[1], NormalVector);
	Intermediate0.z = dot(View.SkyIrradianceEnvironmentMap[2], NormalVector);

	float4 vB = NormalVector.xyzz * NormalVector.yzzx;
	Intermediate1.x = dot(View.SkyIrradianceEnvironmentMap[3], vB);
	Intermediate1.y = dot(View.SkyIrradianceEnvironmentMap[4], vB);
	Intermediate1.z = dot(View.SkyIrradianceEnvironmentMap[5], vB);

	float vC = NormalVector.x * NormalVector.x - NormalVector.y * NormalVector.y;
	Intermediate2 = View.SkyIrradianceEnvironmentMap[6].xyz * vC;

	return max(0, Intermediate0 + Intermediate1 + Intermediate2);
}

float3 ContrastAndNormalizeMulAdd;
float OcclusionExponent;
float OcclusionCombineMode;

float3 SkyLightDiffuse(FScreenSpaceData ScreenSpaceData, float4 UVAndScreenPos, float3 BentNormal)
{
	float2 UV = UVAndScreenPos.xy;
	float3 Lighting = 0;

	FGBufferData GBuffer = ScreenSpaceData.GBuffer;

	float3 SkyLightingNormal = GBuffer.WorldNormal;
	float DotProductFactor = 1;
	float3 DiffuseIrradiance = 0;
	float SkyVisibility = 1;

#if APPLY_SKY_SHADOWING
	{
		SkyVisibility = length(BentNormal);
		float3 NormalizedBentNormal = BentNormal / (max(SkyVisibility, .00001f));
		float BentNormalWeightFactor = SkyVisibility;
		SkyLightingNormal = lerp(NormalizedBentNormal, GBuffer.WorldNormal, BentNormalWeightFactor);
		DotProductFactor = lerp(dot(NormalizedBentNormal, GBuffer.WorldNormal), 1, BentNormalWeightFactor);
	}

	float ContrastCurve = 1 / (1 + exp(-ContrastAndNormalizeMulAdd.x * (SkyVisibility * 10 - 5)));
	SkyVisibility = saturate(ContrastCurve * ContrastAndNormalizeMulAdd.y + ContrastAndNormalizeMulAdd.z);
	SkyVisibility = pow(SkyVisibility, OcclusionExponent);
	SkyVisibility = lerp(SkyVisibility, 1, OcclusionTintAndMinOcclusion.w);
#endif

	SkyVisibility = (OcclusionCombineMode == 0)
		? min(SkyVisibility, min(GBuffer.GBufferAO, ScreenSpaceData.AmbientOcclusion))
		: SkyVisibility * min(GBuffer.GBufferAO, ScreenSpaceData.AmbientOcclusion);
				
	float3 DiffuseColor = GBuffer.DiffuseColor;

	float3 DiffuseLookup = GetSkySHDiffuse(SkyLightingNormal) * View_SkyLightColor.rgb;

#if !APPLY_SKY_SHADOWING
	Lighting += DiffuseLookup * DiffuseColor;
#else
	Lighting += (DotProductFactor * SkyVisibility) * DiffuseLookup * DiffuseColor;
#endif

	return Lighting;
}


void ReflectionEnvironmentSkyLighting(
	in noperspective float4 UVAndScreenPos : TEXCOORD0,
	in float4 SvPosition : SV_Position,
	out float4 OutColor : SV_Target0)
{
	float2 ScreenUV = UVAndScreenPos.xy;
	FScreenSpaceData ScreenSpaceData = GetScreenSpaceData(ScreenUV);
	uint ShadingModelID = ScreenSpaceData.GBuffer.ShadingModelID;

	OutColor = 0.0f;

	float3 BentNormal = ScreenSpaceData.GBuffer.WorldNormal;
#if APPLY_SKY_SHADOWING
	BentNormal = UpsampleDFAO(UVAndScreenPos);
#endif

	[branch]
	if (ShadingModelID != SHADINGMODELID_UNLIT)
	{
#if ENABLE_DYNAMIC_SKY_LIGHT	
		float3 SkyLighting = SkyLightDiffuse(ScreenSpaceData, UVAndScreenPos, BentNormal);
		OutColor = float4(SkyLighting, 0);
#endif		
	}

	[branch]
	if (ShadingModelID != SHADINGMODELID_UNLIT && ShadingModelID != SHADINGMODELID_HAIR)
	{
		OutColor.xyz += ReflectionEnvironment(ScreenSpaceData, UVAndScreenPos, SvPosition, BentNormal);
	}
}



float MinIndirectDiffuseOcclusion;

/** Upsamples the AO results to full resolution using a bilateral filter. */
void AOUpsamplePS(
	in float4 UVAndScreenPos : TEXCOORD0
	,out float4 OutSceneColor : SV_Target0
	)
{
	float3 BentNormal = UpsampleDFAO(UVAndScreenPos);

#if MODULATE_SCENE_COLOR
	float Visibility = lerp(length(BentNormal), 1.0f, MinIndirectDiffuseOcclusion);
	OutSceneColor = Visibility;
#else
	OutSceneColor = float4(length(BentNormal).xxx, 1.0f);

#endif
}






#define SHADER_PERMUTATION_BOOL(InDefineName) \
	public FShaderPermutationBool { \
	public: \
		static constexpr const TCHAR* DefineName = TEXT(InDefineName); \
	}
class FDynamicSkyLight			: SHADER_PERMUTATION_BOOL("ENABLE_DYNAMIC_SKY_LIGHT");


struct FShaderPermutationBool
{
	static constexpr int32 PermutationCount = 2;
	static constexpr bool IsMultiDimensional = false;

	static int32 ToDimensionValueId(bool E) { return E ? 1 : 0; }
	static bool ToDefineValue(bool E) { return E; }
	static bool FromDimensionValueId(int32 PermutationId) { return PermutationId == 1; }
};


class FDynamicSkyLight : public FShaderPermutationBool 
{ 
public: 
	static constexpr const TCHAR* DefineName = TEXT("ENABLE_DYNAMIC_SKY_LIGHT"); 
};
