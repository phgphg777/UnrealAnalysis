
// needs to before Common.usf
#define SHADOW_DEPTH_SHADER 1
#define USE_STENCIL_LOD_DITHER	0

#include "LPVListEntry.ush"
#include "Common.ush"
#include "/Engine/Generated/Material.ush"
#include "/Engine/Generated/VertexFactory.ush"
#include "ShadowDepthCommon.ush"

#define PassStruct ShadowDepthPass
#define SceneTexturesStruct ShadowDepthPass.SceneTextures 

#define ONEPASS_POINTLIGHT_SHADOW 0
#define COMPILER_SUPPORTS_EMPTY_STRUCTS 0
#define INTERPOLATE_VF_ATTRIBUTES 0
#define INTERPOLATE_POSITION 0

struct FShadowDepthVSToPS
{
#if PERSPECTIVE_CORRECT_DEPTH
	float ShadowDepth : TEXCOORD6;
	float DepthBias : TEXCOORD8;
#else
	float Dummy : TEXCOORD6;
#endif
};


void SetShadowDepthOutputs(
	float4x4 WorldToClipMatrix, 
	float4x4 WorldToShadowMatrix, 
	float4 WorldPosition, 
	float3 WorldVertexNormal, 
	out float4 OutPosition, 
	out float ShadowDepth
#if PERSPECTIVE_CORRECT_DEPTH
	, out float OutDepthBias
#endif
)
{
	OutPosition = mul(WorldPosition, WorldToClipMatrix);

	// Clamp the vertex to the near plane if it is in front of the near plane
	// This has problems if some vertices of a triangle get clamped and others do not, also causes artifacts with non-ortho projections
	if (PassStruct.bClampToNearPlane > 0 && OutPosition.z < 0)
	{
		OutPosition.z = 0.000001f;
		OutPosition.w = 1.0f;
	}

	const float NoL = abs(dot(
		float3(WorldToShadowMatrix[0].z, WorldToShadowMatrix[1].z, WorldToShadowMatrix[2].z),
		WorldVertexNormal));

	const float MaxSlopeDepthBias = PassStruct.ShadowParams.z;
	const float Slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL*NoL)) / NoL : MaxSlopeDepthBias, 0, MaxSlopeDepthBias);
	
	const float SlopeDepthBias = PassStruct.ShadowParams.y;
	const float SlopeBias = SlopeDepthBias * Slope;

	const float ConstantDepthBias = PassStruct.ShadowParams.x;
	const float DepthBias = SlopeBias + ConstantDepthBias;

#if PERSPECTIVE_CORRECT_DEPTH
	ShadowDepth = OutPosition.z;
	OutDepthBias = DepthBias;
#else
	// Output linear, normalized depth
	const float InvMaxSubjectDepth = PassStruct.ShadowParams.w;
	ShadowDepth = OutPosition.z * InvMaxSubjectDepth + DepthBias;
	OutPosition.z = ShadowDepth * OutPosition.w;
#endif
}


void PositionOnlyMain(
	in FPositionAndNormalOnlyVertexFactoryInput Input,
	out FShadowDepthVSToPS OutParameters,
	out float4 OutPosition : SV_POSITION
	)
{
	ResolvedView = ResolveView();
	float4 WorldPos = VertexFactoryGetWorldPosition(Input);
	float3 WorldNormal = VertexFactoryGetWorldNormal(Input);
	float Dummy;

	SetShadowDepthOutputs(
		PassStruct.ProjectionMatrix, 
		PassStruct.ViewMatrix, 
		WorldPos, 
		WorldNormal, 
		OutPosition,
#if PERSPECTIVE_CORRECT_DEPTH
		OutParameters.ShadowDepth,
		OutParameters.DepthBias
#else
		Dummy
#endif
	);

#if !PERSPECTIVE_CORRECT_DEPTH
	OutParameters.Dummy = 0;
#endif
}

void Main(
	FVertexFactoryInput Input,
	out FShadowDepthVSToPS OutParameters,
	OPTIONAL_VertexID
	out float4 OutPosition : SV_POSITION)
{
	ResolvedView = ResolveView();
	
	FVertexFactoryIntermediates VFIntermediates = GetVertexFactoryIntermediates(Input);
	float4 WorldPos = VertexFactoryGetWorldPosition(Input, VFIntermediates);
	float3x3 TangentToLocal = VertexFactoryGetTangentToLocal(Input, VFIntermediates);
	FMaterialVertexParameters VertexParameters = GetMaterialVertexParameters(Input, VFIntermediates, WorldPos.xyz, TangentToLocal);
	WorldPos.xyz += GetMaterialWorldPositionOffset(VertexParameters);
	const float3 WorldNormal = VertexFactoryGetWorldNormal(Input, VFIntermediates);	
	float Dummy;

	SetShadowDepthOutputs(
		PassStruct.ProjectionMatrix,
		PassStruct.ViewMatrix,
		WorldPos, 
		WorldNormal,
		OutPosition, 
#if PERSPECTIVE_CORRECT_DEPTH
		OutParameters.ShadowDepth,
		OutParameters.DepthBias
#else
		Dummy
#endif
	);

#if !PERSPECTIVE_CORRECT_DEPTH
	OutParameters.Dummy = 0;
#endif
}

