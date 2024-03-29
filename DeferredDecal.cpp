// Copyright 1998-2018 Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	DeferredDecal.usf: Pixel shader for computing a deferred decal.
=============================================================================*/

// DECAL_BLEND_MODE is set by C++ from EDecalBlendMode e.g. DECALBLENDMODEID_VOLUMETRIC, DECALBLENDMODEID_NORMAL, ...
// DECAL_RENDERTARGET_COUNT is set by C++ e.g. 1: single RT, 2: two MRT, 3: three MRT
// DECAL_RENDERSTAGE is set by C++ from EDecalRenderStage e.g. 0:before base pass, 1:after base pass, 2: before lighting

#include "DecalCommon.ush"

// from SvPosition to decal space (for position), used like this: mul(float4(SvPosition.xyz, 1), SvPositionToDecal);
float4x4 SvPositionToDecal;
// to transform position from decal space to world space, for normals use transpose of inverse
float4x4 DecalToWorld;
// to transform position from world space to decal space, for normals use transpose of inverse
float4x4 WorldToDecal;
// decal orientation in world space, X axis. Used for ObjectOrientation material expression
float3 DecalOrientation;

// DECAL_PRIMITIVE informs material templates which functions to expose when rendering decals.
#define DECAL_PRIMITIVE 1

// x - Fade alpha from screen size fading
// y - Opacity over the lifetime of the decal. 1 -> 0
float2 DecalParams;

#if DECAL_PROJECTION
	#include "/Engine/Generated/Material.ush"
#endif


// from component to clip space (for decal frustum)
float4x4 FrustumComponentToClip;

// decal vertex shader
void MainVS(
	in float4 InPosition : ATTRIBUTE0,
	out float4 OutPosition : SV_POSITION
	)
{
	OutPosition = mul(InPosition, FrustumComponentToClip);
}

float DistanceFunction(float4 SvPosition, float3 Pos)
{
	float3 SwizzlePos = Pos.zyx;

#if DECAL_PROJECTION && DECAL_BLEND_MODE == DECALBLENDMODEID_VOLUMETRIC
	
	FMaterialPixelParameters MaterialParameters = MakeInitializedMaterialPixelParameters();
#if NUM_MATERIAL_TEXCOORDS
	for(int CoordinateIndex = 0;CoordinateIndex < NUM_MATERIAL_TEXCOORDS;CoordinateIndex++)
	{
		MaterialParameters.TexCoords[CoordinateIndex] = SwizzlePos.xy * 0.5f + 0.5f;
	}
#endif

	MaterialParameters.VertexColor = 1;
	MaterialParameters.SvPosition = SvPosition;
	MaterialParameters.ScreenPosition = SvPositionToScreenPosition(SvPosition);
	MaterialParameters.LightVector = SwizzlePos * 0.5f + 0.5f;		// to be compatible with decals but *0.5+0.5 seems wrong
	MaterialParameters.AbsoluteWorldPosition = 
	MaterialParameters.WorldPosition_CamRelative = 
	MaterialParameters.WorldPosition_NoOffsets = 
	MaterialParameters.WorldPosition_NoOffsets_CamRelative = mul(DecalToWorld, float4(Pos, 1)).xyz;
	MaterialParameters.CameraVector = normalize(View.WorldCameraOrigin - MaterialParameters.AbsoluteWorldPosition);

	FPixelMaterialInputs PixelMaterialInputs;
	CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

	// the material needs to output the distance from "Opacity" depending on LightVector (in 0..1 range)
	return GetMaterialMaskInputRaw(PixelMaterialInputs);

#endif

	return 0;
}

// could be exposed to the user
const static float coneSpread = 0.00002f;

// @return t=0..1, -1: no hit
float Raycast(float4 SvPosition, float3 StartPos, float3 EndPos)
{
	const uint steps = 300; 

	float t = 0;

	float RayLength = length(EndPos - StartPos);
	float3 Direction = normalize(EndPos - StartPos);

	LOOP for(uint i = 0; i < steps; ++i)
	{
		float Distance = DistanceFunction(SvPosition, StartPos + t * Direction);

		if(Distance <= t * coneSpread)
		{
			return t / RayLength;
		}

		if(t > RayLength)
		{
		   break;
		}

		t += Distance;
	}

	return -1;
}

// like explained here http://iquilezles.org/www/articles/derivative/derivative.htm
float3 GradientNotNormalized(float4 SvPosition, float3 OSPos, float t)
{
	float eps = t * coneSpread;

	float2 e = float2(eps, 0);

    return float3(DistanceFunction(SvPosition, OSPos + e.xyy) - DistanceFunction(SvPosition, OSPos - e.xyy),
                  DistanceFunction(SvPosition, OSPos + e.yxy) - DistanceFunction(SvPosition, OSPos - e.yxy),
                  DistanceFunction(SvPosition, OSPos + e.yyx) - DistanceFunction(SvPosition, OSPos - e.yyx));
}


float4 SvPositionToScreenPosition2(float4 SvPosition)
{
	// assumed SvPosition.w = 1

	float4 Ret;

	Ret.x = ((SvPosition.x - View.ViewRectMin.x) * View.ViewSizeAndInvSize.z) * 2 - 1;
	Ret.y = ((SvPosition.y - View.ViewRectMin.y) * View.ViewSizeAndInvSize.w) * -2 + 1;
	Ret.z = ConvertFromDeviceZ(SvPosition.z);
	Ret.w = 1;

	Ret.xy *= Ret.z;

	// so .w has the SceneDepth, some mobile code wants that
//	Ret *= Ret.z;

	return Ret;
}

// decal pixel shader
#if DECAL_PROJECTION

// EARLYDEPTHSTENCIL for volumetric decals could work here, but only if we are ouside of the decal

// is called in MainPS() from PixelShaderOutputCommon.usf
void FPixelShaderInOut_MainPS(inout FPixelShaderIn In, inout FPixelShaderOut Out)
{
	ResolvedView = ResolveView();

	float2 ScreenUV = SvPositionToBufferUV(In.SvPosition);
	
	// make SvPosition appear to be rasterized with the depth from the depth buffer
	In.SvPosition.z = LookupDeviceZ(ScreenUV);

	const bool bVolumetric = DECAL_BLEND_MODE == DECALBLENDMODEID_VOLUMETRIC;

	float3 OSPosition;
	float3 WSPosition;
	float HitT;

	if(bVolumetric)
	{
		// LineBoxIntersect want's to clip a long ray so we provide one, this could be done differently as it limites the draw distance and reduces precision 
		float SceneW = 100000000;

		float4 SvPositionInDistance = float4(In.SvPosition.xy, ConvertToDeviceZ(SceneW), 1);

		float3 WSStartRay = View.WorldCameraOrigin;
		float3 WSEndRay = SvPositionToWorld(SvPositionInDistance);

		// in object/decal space
		float4 OSStartRayHom = mul(float4(WSStartRay,1), WorldToDecal);
		float4 OSEndRayHom = mul(float4(WSEndRay, 1), WorldToDecal);

		float3 OSStartRay = OSStartRayHom.xyz / OSStartRayHom.w;
		float3 OSEndRay = OSEndRayHom.xyz / OSEndRayHom.w;

		float2 OSBoxMinMax = LineBoxIntersect(OSStartRay, OSEndRay, -1, 1);

		float3 OSBoxStartRay = OSStartRay + OSBoxMinMax.x * (OSEndRay - OSStartRay);
		float3 OSBoxEndRay = OSStartRay + OSBoxMinMax.y * (OSEndRay - OSStartRay);

		HitT = Raycast(In.SvPosition, OSBoxStartRay, OSBoxEndRay);

		clip(HitT);

		OSPosition = OSBoxStartRay + HitT * (OSBoxEndRay - OSBoxStartRay);
		WSPosition = mul(float4(OSPosition, 1), DecalToWorld).xyz;

		#if OUTPUT_PIXEL_DEPTH_OFFSET
			float4 CSHitPos = mul(float4(WSPosition, 1), View.WorldToClip);
			OutDepth = CSHitPos.z / CSHitPos.w;
		#endif
	}
	else
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
#if NUM_MATERIAL_TEXCOORDS
	for(int CoordinateIndex = 0;CoordinateIndex < NUM_MATERIAL_TEXCOORDS;CoordinateIndex++)
	{
		MaterialParameters.TexCoords[CoordinateIndex] = DecalUVs;
	}
#endif
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
	FPixelMaterialInputs PixelMaterialInputs;
	CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

	// wizardbug for decal
	FScreenSpaceData ScreenSpaceData = GetScreenSpaceData(ScreenUV);


	float TargetSurfacePriority = ScreenSpaceData.GBuffer.SBCustomData.z;

	clip(TargetSurfacePriority <= 0.0f ? -1 : 1);

	float DecalPriority = saturate( GetMaterialCustomData1(MaterialParameters) );
	clip( DecalPriority  <= TargetSurfacePriority ? 1 : -1);
	// wizardbug for decal


	float DecalFading = saturate(4 - 4 * abs(SwizzlePos.z * 2 - 1)) * DecalParams.x * DecalParams.y;

	#if DECAL_BLEND_MODE == DECALBLENDMODEID_AO
	{
		float AmbientOcclusion = lerp(1, GetMaterialAmbientOcclusion(PixelMaterialInputs), DecalFading);
		Out.MRT[0] = float4(AmbientOcclusion, AmbientOcclusion, AmbientOcclusion, AmbientOcclusion);
		return;
	}
	#endif

	if(bVolumetric)
	{
		// not normalized
		float3 OSNormal = GradientNotNormalized(In.SvPosition, OSPosition, HitT);
			
		// To transform the normals use tranpose(Inverse(DecalToWorld)) = transpose(WorldToDecal)
		// But we want to only rotate the normals (we don't want to non-uniformaly scale them).
		// We assume the matrix is only a scale and rotation, and we remove non-uniform scale:
		float3 lengthSqr = { length2(DecalToWorld._m00_m01_m02),
			length2(DecalToWorld._m10_m11_m12),
			length2(DecalToWorld._m20_m21_m22) };

		float3 scale = rsqrt(lengthSqr);

		// Pre-multiply by the inverse of the non-uniform scale in DecalToWorld
		float4 ScaledNormal = float4(OSNormal.xyz * scale.xyz, 0.f); 
		float3 WSNormal = mul(ScaledNormal, DecalToWorld).xyz;

		// need to normalized

		MaterialParameters.WorldNormal = normalize(WSNormal);
	}

	

	// Store the results in local variables and reuse instead of calling the functions multiple times.
	half3 BaseColor = GetMaterialBaseColor(PixelMaterialInputs);
	half  Metallic = GetMaterialMetallic(PixelMaterialInputs);
	half  Specular = GetMaterialSpecular(PixelMaterialInputs);

	float3 Color = 1;

	#if DECAL_BLEND_MODE == DECALBLENDMODEID_NORMAL
		// -1..1 range to 0..1
		Color = MaterialParameters.WorldNormal * 0.5f + 0.5f;
	#else
		Color = GetMaterialEmissive(PixelMaterialInputs);
	
		#if (ES2_PROFILE || ES3_1_PROFILE) // only for mobile
			Color+= BaseColor;
		#endif
	#endif
	
	float Opacity = GetMaterialOpacity(PixelMaterialInputs) * DecalFading;

	FGBufferData GBufferData;

	GBufferData.WorldNormal = MaterialParameters.WorldNormal;
	GBufferData.BaseColor = BaseColor;

	GBufferData.Metallic = Metallic;
	GBufferData.Specular = Specular;
	GBufferData.Roughness = GetMaterialRoughness(PixelMaterialInputs);
	GBufferData.CustomData = 0;
	GBufferData.IndirectIrradiance = 0;
	GBufferData.PrecomputedShadowFactors = 1;
	GBufferData.GBufferAO = 1;
	GBufferData.ShadingModelID = SHADINGMODELID_DEFAULT_LIT;
	GBufferData.SelectiveOutputMask = 0;
	GBufferData.PerObjectGBufferData = 1; 

	DecalCommonOutput(In, Out, Color, Opacity, GBufferData);
}


#define PIXELSHADEROUTPUT_MRT0 (DECAL_RENDERTARGET_COUNT > 0)
#define PIXELSHADEROUTPUT_MRT1 (DECAL_RENDERTARGET_COUNT > 1 && (BIND_RENDERTARGET1 || COMPILER_METAL))
#define PIXELSHADEROUTPUT_MRT2 (DECAL_RENDERTARGET_COUNT > 2)
#define PIXELSHADEROUTPUT_MRT3 (DECAL_RENDERTARGET_COUNT > 3)
#define PIXELSHADEROUTPUT_MRT4 (DECAL_RENDERTARGET_COUNT > 4)
// all PIXELSHADEROUTPUT_ and "void FPixelShaderInOut_MainPS()" need to be setup before this include
// this include generates the wrapper code to call MainPS()
#include "PixelShaderOutputCommon.ush"


#endif // DECAL_PROJECTION
