
// Assume View.ViewRect.Size() == FSceneRenderTargets::Get().BufferSize
FIntPoint AOBufferSize = View.ViewRect.Size() / GAODownsampleFactor;										
FIntPoint ScreenGridDimensions = AOBufferSize / GConeTraceDownsampleFactor;
float MaxOcclusionDistance = 
	!Scene->SkyLight->bWantsStaticShadowing ? 
	  Scene->SkyLight->OcclusionMaxDistance 
	: Scene->DefaultMaxDistanceFieldOcclusionDistance;
float GAOConeHalfAngle = Acos(1 - 1.0f / (float)ARRAY_COUNT(SpacedVectors9));
float GlobalMaxSphereQueryRadius = MaxOcclusionDistance / (1 + Tan(GAOConeHalfAngle));
float k = GAOGlobalDFClipmapDistanceExponent = TEXT("r.AOGlobalDFClipmapDistanceExponent") = 2;
float ClipmapExtent = Scene->GlobalDistanceFieldViewDistance * Pow(k, ClipmapIndex - 3); // 1/8, 1/4, 1/2, 1
float CellSize = (ClipmapExtent * 2) / GAOGlobalDFResolution;

static bool ShouldUpdateClipmapThisFrame(int32 ClipmapIndex, int32 GlobalDistanceFieldUpdateIndex)
{
	if(GAOGlobalDistanceFieldStaggeredUpdates==0)
		return true;
	if(ClipmapIndex==0)
		return true;
	if(GlobalDistanceFieldUpdateIndex % 2 == 0 && ClipmapIndex == 1)
		return true;
	if(GlobalDistanceFieldUpdateIndex == 1 && ClipmapIndex == 2)
		return true;
	if(GlobalDistanceFieldUpdateIndex == 3 && ClipmapIndex == 3)
		return true;
	return false;
}



class FAOScreenGridResources {
	FIntPoint ScreenGridDimensions;
	FRWBuffer ScreenGridConeVisibility;
	/* bool bAllocateResourceForGI;
	FRWBuffer ConeDepthVisibilityFunction;
	FRWBuffer StepBentNormal;
	FRWBuffer SurfelIrradiance;
	FRWBuffer HeightfieldIrradiance; */
};

class FVolumeUpdateRegion
{
	FBox Bounds;
	FIntVector CellsSize;
	// EVolumeUpdateType UpdateType = VUT_MeshDistanceFields | VUT_Heightfields;
};
class FGlobalDistanceFieldClipmap
{
	/** World space bounds. */
	FBox Bounds;

	/** Offset applied to UVs so that only new or dirty areas of the volume texture have to be updated. */
	FVector ScrollOffset;

	/** Regions in the volume texture to update. */
	TArray<FVolumeUpdateRegion, TInlineAllocator<3> > UpdateRegions;

	/** Volume texture for this clipmap. */
	TRefCountPtr<IPooledRenderTarget> RenderTarget;
};
class FGlobalDistanceFieldParameterData
{
	FVector4 CenterAndExtent[4];		// = FVector4(Clipmaps[i].Bounds.GetCenter(), Clipmaps[i].Bounds.GetExtent().X)
	FVector4 WorldToUVAddAndMul[4];
	FTextureRHIParamRef Textures[4];	// = Clipmaps[i].RenderTarget->ShaderResourceTexture
	float GlobalDFResolution;			// = GAOGlobalDFResolution
	float MaxDistance;					// = MaxOcclusionDistance / (1 + Tan(GAOConeHalfAngle))
};
class FGlobalDistanceFieldInfo {
	bool bInitialized;
	TArray<FGlobalDistanceFieldClipmap> MostlyStaticClipmaps;
	TArray<FGlobalDistanceFieldClipmap> Clipmaps;
	FGlobalDistanceFieldParameterData ParameterData;
};

class FGlobalDistanceFieldCacheTypeState
{	
	TArray<FVector4> PrimitiveModifiedBounds;
	TRefCountPtr<IPooledRenderTarget> VolumeTexture; // 128*128*128
};
class FGlobalDistanceFieldClipmapState
{
	FIntVector FullUpdateOrigin;
	FIntVector LastPartialUpdateOrigin;
	float CachedMaxOcclusionDistance;
	float CachedGlobalDistanceFieldViewDistance;
	uint32 CacheMostlyStaticSeparately;

	FGlobalDistanceFieldCacheTypeState Cache[2];

	// Used to perform a full update of the clip map when the scene data changes
	FDistanceFieldSceneData* LastUsedSceneDataForFullUpdate = NULL;
};
class FViewInfo : public FSceneView
{
	FSceneViewState* ViewState;
	{
		FAOScreenGridResources* AOScreenGridResources;
		bool bInitializedGlobalDistanceFieldOrigins;
		FGlobalDistanceFieldClipmapState GlobalDistanceFieldClipmapState[4];
		int32 GlobalDistanceFieldUpdateIndex;
	}
	FGlobalDistanceFieldInfo GlobalDistanceFieldInfo;
}

class FScene 
{
	FDistanceFieldSceneData DistanceFieldSceneData;
	float GlobalDistanceFieldViewDistance; // = AWorldSettings::GlobalDistanceFieldViewDistance
}

class FAOParameters
{
	FShaderParameter AOObjectMaxDistance;
	FShaderParameter AOStepScale;
	FShaderParameter AOStepExponentScale;
	FShaderParameter AOMaxViewDistance;
	FShaderParameter AOGlobalMaxOcclusionDistance;

	void Set(const FDistanceFieldAOParameters& Parameters)
	{
		AOObjectMaxDistance = TEXT("r.AOGlobalDFStartDistance"); 	// <- Parameters.ObjectMaxOcclusionDistance
		AOGlobalMaxOcclusionDistance = MaxOcclusionDistance;  		// <- Parameters.GlobalMaxOcclusionDistance
		AOMaxViewDistance = TEXT("r.AOMaxViewDistance"); 			// <- FMath::Min(GAOMaxViewDistance, 65000.0f)
		AOStepExponentScale = TEXT("r.AOStepExponentScale");  		// <- GAOStepExponentScale
		AOStepScale = ;
	}
}	

class FScreenGridParameters
{
	FShaderParameter BaseLevelTexelSize;
	FShaderParameter JitterOffset;
	FShaderParameter ScreenGridConeVisibilitySize;
	FShaderResourceParameter DistanceFieldNormalTexture;
	FShaderResourceParameter DistanceFieldNormalSampler;

	void Set(const FViewInfo& View, FSceneRenderTargetItem& DistanceFieldNormal)
	{
		FIntPoint DownsampledBufferSize = GetBufferSizeForAO();
		FAOScreenGridResources* ScreenGridResources = View.ViewState->AOScreenGridResources;

		BaseLevelTexelSize = 1.0f / FVector2D( DownsampledBufferSize );
		JitterOffset = JitterOffsets[GFrameNumber % 4] * GConeTraceDownsampleFactor;
		ScreenGridConeVisibilitySize = ScreenGridResources->ScreenGridDimensions;
		DistanceFieldNormalTexture = DistanceFieldNormal.ShaderResourceTexture;
		DistanceFieldNormalSampler = TStaticSamplerState<SF_Point,AM_Wrap,AM_Wrap,AM_Wrap>;
	}
}

class FGlobalDistanceFieldParameters
{
	FShaderResourceParameter GlobalDistanceFieldTexture0, GlobalDistanceFieldTexture1, GlobalDistanceFieldTexture2, GlobalDistanceFieldTexture3;
	FShaderResourceParameter GlobalDistanceFieldSampler0, GlobalDistanceFieldSampler1, GlobalDistanceFieldSampler2, GlobalDistanceFieldSampler3;
	FShaderParameter GlobalVolumeCenterAndExtent;
	FShaderParameter GlobalVolumeWorldToUVAddAndMul;
	FShaderParameter GlobalVolumeDimension;
	FShaderParameter GlobalVolumeTexelSize;
	FShaderParameter MaxGlobalDistance;

	void Set(const FGlobalDistanceFieldParameterData& ParameterData)
	{
		GlobalDistanceFieldTexture0 = ParameterData.Textures[0];
		GlobalDistanceFieldTexture1 = ParameterData.Textures[1];
		GlobalDistanceFieldTexture2 = ParameterData.Textures[2];
		GlobalDistanceFieldTexture3 = ParameterData.Textures[3];
		GlobalDistanceFieldSampler0 = GlobalDistanceFieldSampler1 = GlobalDistanceFieldSampler2 = GlobalDistanceFieldSampler3
		 	= TStaticSamplerState<SF_Bilinear, AM_Wrap, AM_Wrap, AM_Wrap>;

		GlobalVolumeCenterAndExtent = ParameterData.CenterAndExtent;
		GlobalVolumeWorldToUVAddAndMul =  ParameterData.WorldToUVAddAndMul;
		GlobalVolumeDimension =  TEXT("r.AOGlobalDFResolution"); 	// = ParameterData.GlobalDFResolution;
		GlobalVolumeTexelSize = 1.0f / ParameterData.TEXT("r.AOGlobalDFResolution");
		MaxGlobalDistance = GlobalMaxSphereQueryRadius; 			// = ParameterData.MaxDistance
	}
}


#define AO_DOWNSAMPLE_FACTOR 2				// in hlsl,
#define TRACE_DOWNSAMPLE_FACTOR 4  			// in c++,  GConeTraceDownsampleFactor
#define CONE_TRACE_GLOBAL_DISPATCH_SIZEX 8 	// in c++,  GConeTraceGlobalDFTileSize
uint2 ScreenGridConeVisibilitySize; 		// = (128, 96)
float2 BaseLevelTexelSize;					// = 1 / (512, 384)
float2 JitterOffset;						// = (0.25, 0) * 4
float2 Jitter;								// = (0.25, 0)
float2 InvBufferSize;						// = 1 / (1024, 768)
Texture2D DistanceFieldNormalTexture;
SamplerState DistanceFieldNormalSampler;


float2 GetScreenUVFromScreenGrid(uint2 OutputCoordinate)
{
	return ( OutputCoordinate + Jitter + float2(.0625f, .0625f) ) * 4 * 2  * InvBufferSize;
}
float2 GetBaseLevelScreenUVFromScreenGrid(uint2 OutputCoordinate)
{
	return ( OutputCoordinate + Jitter + float2(.125f, .125f) ) * 4 * BaseLevelTexelSize;
}
void GetDownsampledGBuffer(float2 ScreenUV, out float3 OutNormal, out float OutDepth)
{
	float4 TextureValue = Texture2DSampleLevel(DistanceFieldNormalTexture, DistanceFieldNormalSampler, ScreenUV, 0);
	OutNormal = TextureValue.xyz;
	OutDepth = TextureValue.w;
}
void FindBestAxisVectors2(float3 InZAxis, out float3 OutXAxis, out float3 OutYAxis )
{
	float3 UpVector = abs(InZAxis.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
	OutXAxis = normalize( cross( UpVector, InZAxis ) );
	OutYAxis = cross( InZAxis, OutXAxis );

	float a = InZAxis.x; float b = InZAxis.y; float c = InZAxis.z;
	float deltaAngle = radians(1.0);
	float Cos = cos((float)View.FrameNumber * deltaAngle);
	float Sin = sin((float)View.FrameNumber * deltaAngle);
	float3x3 R = {
		Cos+a*a*(1-Cos),	a*b*(1-Cos)-c*Sin, 	a*c*(1-Cos)+b*Sin,
		a*b*(1-Cos)+c*Sin, 	Cos+b*b*(1-Cos),	b*c*(1-Cos)-a*Sin,
		a*c*(1-Cos)-b*Sin,	b*c*(1-Cos)+a*Sin,	Cos+c*c*(1-Cos)
	};
	OutXAxis = mul(R, OutXAxis);
	OutYAxis = mul(R, OutYAxis);
}


[numthreads(CONE_TRACE_GLOBAL_DISPATCH_SIZEX, CONE_TRACE_GLOBAL_DISPATCH_SIZEX, 1)]
void ConeTraceGlobalOcclusionCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,	// 8, 0, 0
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint2 OutputCoordinate = DispatchThreadId.xy;
	
	if (all(OutputCoordinate < ScreenGridConeVisibilitySize))
	{
		float2 BaseLevelScreenUV = GetBaseLevelScreenUVFromScreenGrid(OutputCoordinate);	// 0.06543, 0.0013

		float3 WorldNormal, TangentX, TangentY;
		float SceneDepth;
		GetDownsampledGBuffer(BaseLevelScreenUV, WorldNormal, SceneDepth);
		FindBestAxisVectors2(WorldNormal, TangentX, TangentY);

		// float2 ScreenUV = GetScreenUVFromScreenGrid(OutputCoordinate);		// 0.06494, 0.00065
		float2 ScreenUV = BaseLevelScreenUV;
		float2 ScreenPosition = (ScreenUV.xy - View.ScreenPositionScaleBias.wz) / View.ScreenPositionScaleBias.xy;
		float3 WorldShadingPosition = mul(float4(ScreenPosition * SceneDepth, SceneDepth, 1), View.ScreenToWorld).xyz;

		uint OutputBaseIndex = OutputCoordinate.y * ScreenGridConeVisibilitySize.x + OutputCoordinate.x;

		HemisphereConeTraceAgainstGlobalDistanceField(OutputBaseIndex, WorldShadingPosition, SceneDepth, WorldNormal, TangentX, TangentY);
	}
}


float4 GlobalVolumeCenterAndExtent[4];

float ComputeDistanceFromBoxToPointInside(float3 BoxCenter, float3 BoxExtent, float3 InPoint)
{
	float3 DistancesToMin = max(InPoint - BoxCenter + BoxExtent, 0);
	float3 DistancesToMax = max(BoxCenter + BoxExtent - InPoint, 0);
	float3 ClosestDistances = min(DistancesToMin, DistancesToMax);
	return min(ClosestDistances.x, min(ClosestDistances.y, ClosestDistances.z));
}
void HemisphereConeTraceAgainstGlobalDistanceField(uint OutputBaseIndex, float3 WorldShadingPosition, float SceneDepth, float3 WorldNormal, float3 TangentX, float3 TangentY)
{
	float Border = AOGlobalMaxOcclusionDistance;
	int ClipmapIndex = -1;

	for(uint i=0; i<4; ++i)
	{
		if(Border < ComputeDistanceFromBoxToPointInside(GlobalVolumeCenterAndExtent[i].xyz, GlobalVolumeCenterAndExtent[i].www, WorldShadingPosition))
		{
			ClipmapIndex = i;
			break;
		}
	}

	if(ClipmapIndex >= 0)
	{
		HemisphereConeTraceAgainstGlobalDistanceFieldClipmap((uint)ClipmapIndex, OutputBaseIndex, WorldShadingPosition, SceneDepth, WorldNormal, TangentX, TangentY)	
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
float MaxOcclusionDistance = !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance : Scene->DefaultMaxDistanceFieldOcclusionDistance;
float GAOConeHalfAngle = Acos(1 - 1.0f / (float)ARRAY_COUNT(SpacedVectors9));
float AOLargestSampleOffset = AOObjectMaxDistance / (1 + Tan(GAOConeHalfAngle));	// 65.9896  ==  100 / 1.51539
float GlobalMaxSphereQueryRadius = MaxOcclusionDistance / (1 + Tan(GAOConeHalfAngle));
FAOParameters::AOStepScale = AOLargestSampleOffset / Pow(2.0f, GAOStepExponentScale * (GAONumConeSteps - 1));
FGlobalDistanceFieldParameters::MaxGlobalDistance = GlobalMaxSphereQueryRadius;
TConeTraceScreenGridGlobalOcclusionCS::TanConeHalfAngle = Tan(GAOConeHalfAngle);

float AOStepExponentScale = GAOStepExponentScale;
float AOStepScale = AOObjectMaxDistance 
					/ (1 + Tan(GAOConeHalfAngle)) / Pow(2.0f, GAOStepExponentScale * (GAONumConeSteps - 1));
//////////////////////////////////////////////////////////////////////////////////////////////////////

float AOStepScale;			// 2.91636
float AOStepExponentScale;	// 0.5
float GetStepOffset(float StepIndex)
{
	// Original heuristic
	//return AOStepScale * exp2(AOStepExponentScale * StepIndex);

	float temp = AOStepExponentScale * StepIndex;
	return AOStepScale * (temp * temp + 1);
}
float3 ComputeGlobalUV(float3 WorldPosition, uint ClipmapIndex)
{
	//return ((WorldPosition - GlobalVolumeCenterAndExtent[ClipmapIndex].xyz + GlobalVolumeScollOffset[ClipmapIndex].xyz) / (GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2) + .5f);
	float4 WorldToUVAddAndMul = GlobalVolumeWorldToUVAddAndMul[ClipmapIndex];
	return WorldPosition * WorldToUVAddAndMul.www + WorldToUVAddAndMul.xyz;
}
float4 SampleGlobalDistanceField(int ClipmapIndex, float3 UV)
{
	ClipmapIndex == 0 ? 
		return Texture3DSampleLevel(GlobalDistanceFieldTexture0, SharedGlobalDistanceFieldSampler0, UV, 0)
	: ClipmapIndex == 1 ?
		return Texture3DSampleLevel(GlobalDistanceFieldTexture1, SharedGlobalDistanceFieldSampler1, UV, 0)
	: ClipmapIndex == 2 ?
		return Texture3DSampleLevel(GlobalDistanceFieldTexture2, SharedGlobalDistanceFieldSampler2, UV, 0)
	: 	return Texture3DSampleLevel(GlobalDistanceFieldTexture3, SharedGlobalDistanceFieldSampler3, UV, 0)
}

#define CONE_TRACE_OBJECTS 1 	// in c++
#define NUM_CONE_STEPS 10		// in hlsl,  == GAONumConeSteps,  Number of AO sample positions along each cone
#define NUM_CONE_DIRECTIONS 9	// in hlsl,  == NumConeSampleDirections,  Number of cone traced directions

void HemisphereConeTraceAgainstGlobalDistanceFieldClipmap(
	uniform uint ClipmapIndex,
	uint OutputBaseIndex,
	float3 WorldShadingPosition,
	float SceneDepth,
	float3 WorldNormal,
	float3 TangentX,
	float3 TangentY)
{
	float MinStepSize = GlobalVolumeCenterAndExtent[ClipmapIndex].w * 2 / 300.0f;
	float InvAOGlobalMaxOcclusionDistance = 1.0f / AOGlobalMaxOcclusionDistance;
	float InitialOffset = GetStepOffset(NUM_CONE_STEPS);

	float ConeTraceLeakFill = 1.0f;
	
#if	!CONE_TRACE_OBJECTS
	// Without the object cone trace there is a lot of AO leaking. Constant initial offset looks worse, but helps with leaking.
	InitialOffset = GlobalVolumeCenterAndExtent[ClipmapIndex].w * GlobalVolumeTexelSize * 2.0f;

	// Cover AO leaking by comparing initial step SDF value against initial step size.
	{
		float3 WorldSamplePosition = WorldShadingPosition + WorldNormal * InitialOffset;
		float3 StepVolumeUV = ComputeGlobalUV(WorldSamplePosition, ClipmapIndex);
		float DistanceToOccluder = SampleGlobalDistanceField(ClipmapIndex, StepVolumeUV).x;
		ConeTraceLeakFill = saturate(Pow2(DistanceToOccluder / InitialOffset)) * 0.4f + 0.6f;
	}
#endif

	for (uint ConeIndex = 0; ConeIndex < NUM_CONE_DIRECTIONS; ConeIndex++)
	{
		float3 ConeDirection = AOSamples2.SampleDirections[ConeIndex].xyz;
		float3 RotatedConeDirection = ConeDirection.x * TangentX + ConeDirection.y * TangentY + ConeDirection.z * WorldNormal;

		float MinVisibility = 1;
		float WorldStepOffset = InitialOffset;

		for (uint StepIndex = 0; StepIndex < NUM_CONE_STEPS && WorldStepOffset < AOGlobalMaxOcclusionDistance; StepIndex++)
		{
			float3 WorldSamplePosition = WorldShadingPosition + RotatedConeDirection * WorldStepOffset;
			float3 StepVolumeUV = ComputeGlobalUV(WorldSamplePosition, ClipmapIndex);
			float DistanceToOccluder = SampleGlobalDistanceField(ClipmapIndex, StepVolumeUV).x;
			float Visibility = saturate(DistanceToOccluder / (WorldStepOffset * TanConeHalfAngle));			
			
			MinVisibility = min(MinVisibility, Visibility);
			WorldStepOffset += max(DistanceToOccluder, MinStepSize);
		}

		MinVisibility *= ConeTraceLeakFill;

		InterlockedMin(RWScreenGridConeVisibility[ConeIndex * ScreenGridConeVisibilitySize.x * ScreenGridConeVisibilitySize.y + OutputBaseIndex], asuint(MinVisibility));
	}
}


#define CONE_TRACE_OBJECTS_THREADGROUP_SIZE 64  // = ConeTraceObjectsThreadGroupSize
[numthreads(CONE_TRACE_OBJECTS_THREADGROUP_SIZE, 1, 1)]
void ConeTraceObjectOcclusionCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint PixelIndex = GroupThreadId.x % (CONE_TILE_SIZEX * CONE_TILE_SIZEX);
	uint SubCulledTileIndex = GroupThreadId.x / (CONE_TILE_SIZEX * CONE_TILE_SIZEX);

	uint CulledTileDataBaseIndex = GroupId.x * CONE_TRACE_TILES_PER_THREADGROUP;
	uint CulledTileIndex = CulledTileDataBaseIndex + SubCulledTileIndex;
	uint TileIndex = CulledTileDataArray[CulledTileIndex * CULLED_TILE_DATA_STRIDE + 0];
	uint ObjectIndex = CulledTileDataArray[CulledTileDataBaseIndex * CULLED_TILE_DATA_STRIDE + 1];
	uint2 TileCoordinate = uint2(TileIndex % TileListGroupSize.x, TileIndex / TileListGroupSize.x);
	uint2 PixelCoordinate = uint2(PixelIndex % CONE_TILE_SIZEX, PixelIndex / CONE_TILE_SIZEX);
	uint2 OutputCoordinate = TileCoordinate * CONE_TILE_SIZEX + PixelCoordinate;

	if (TileIndex != INVALID_TILE_INDEX && all(OutputCoordinate < ScreenGridConeVisibilitySize))
	{
		float2 BaseLevelScreenUV = GetBaseLevelScreenUVFromScreenGrid(OutputCoordinate);
		uint OutputBaseIndex = OutputCoordinate.y * ScreenGridConeVisibilitySize.x + OutputCoordinate.x;

		float3 WorldNormal, TangentX, TangentY;
		float SceneDepth;
		GetDownsampledGBuffer(BaseLevelScreenUV, WorldNormal, SceneDepth);
		FindBestAxisVectors2(WorldNormal, TangentX, TangentY);

		float2 ScreenPosition = (BaseLevelScreenUV.xy - View.ScreenPositionScaleBias.wz) / View.ScreenPositionScaleBias.xy;
		float3 WorldShadingPosition = mul(float4(ScreenPosition * SceneDepth, SceneDepth, 1), View.ScreenToWorld).xyz;

		HemisphereConeTraceAgainstTileCulledObjects(ObjectIndex, OutputBaseIndex, WorldShadingPosition, SceneDepth, WorldNormal, TangentX, TangentY);
	}
}

Buffer<float4> TileConeDepthRanges;
float TanConeHalfAngle;
RWBuffer<uint> RWScreenGridConeVisibility;
RWBuffer<float> RWConeDepthVisibilityFunction;

MaterialFloat ComputeDistanceFromBoxToPoint(MaterialFloat3 Mins, MaterialFloat3 Maxs, MaterialFloat3 InPoint)
{
	MaterialFloat3 DistancesToMin = InPoint < Mins ? abs(InPoint - Mins) : 0;
	MaterialFloat3 DistancesToMax = InPoint > Maxs ? abs(InPoint - Maxs) : 0;

	//@todo - this is actually incorrect, it gives manhattan distance
	MaterialFloat Distance = dot(DistancesToMin, 1);
	Distance += dot(DistancesToMax, 1);
	return Distance;
}
void HemisphereConeTraceAgainstTileCulledObjects(
	uint ObjectIndex, 
	uint OutputBaseIndex, 
	float3 WorldShadingPosition, 
	float SceneDepth, 
	float3 WorldNormal, 
	float3 TangentX, 
	float3 TangentY)
{
	float MaxWorldStepOffset = GetStepOffset(NUM_CONE_STEPS);
	float InvMaxOcclusionDistance = 1.0f / AOObjectMaxDistance;
#if USE_GLOBAL_DISTANCE_FIELD
	InvMaxOcclusionDistance = 1.0f / AOGlobalMaxOcclusionDistance;
#endif

	float4 ObjectPositionAndRadius = CulledObjectBounds[ObjectIndex];
	float ObjectDistanceSq = dot(ObjectPositionAndRadius.xyz - WorldShadingPosition, ObjectPositionAndRadius.xyz - WorldShadingPosition);

	// Skip tracing objects with a small projected angle 
	if (ObjectPositionAndRadius.w * ObjectPositionAndRadius.w / ObjectDistanceSq > Square(.25f))
	{
		float3 LocalPositionExtent = CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 3].xyz;
		float4x4 WorldToVolume = transpose(float4x4(
			CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 0],
			CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 1],
			CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 2],
			float4(0.0f, 0.0f, 0.0f, 1.0f) 
		));

		float4 temp4 = CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 4];
		bool bGeneratedAsTwoSided = temp4.w < 0;
		float4 UVScaleAndVolumeScale = float4(temp4.xyz, abs(temp4.w));

		float3 VolumeShadingPosition = mul(float4(WorldShadingPosition, 1), WorldToVolume).xyz;
		float BoxDistance = ComputeDistanceFromBoxToPoint(-LocalPositionExtent, LocalPositionExtent, VolumeShadingPosition) * UVScaleAndVolumeScale.w;

		if (BoxDistance < AOObjectMaxDistance)
		{
			float4 UVAddAndSelfShadowBias = CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 5];
			float2 DistanceFieldMAD = CulledObjectData[ObjectIndex * CULLED_OBJECT_DATA_STRIDE + 6].xy;

			float ObjectOccluderRadius = length(LocalPositionExtent) * .5f * UVScaleAndVolumeScale.w;
			float SelfShadowScale = 1.0f / max(UVAddAndSelfShadowBias.w, .0001f);

			for (uint ConeIndex = 0; ConeIndex < NUM_CONE_DIRECTIONS; ConeIndex++)
			{
				float3 ConeDirection = AOSamples2.SampleDirections[ConeIndex].xyz;
				float3 RotatedConeDirection = ConeDirection.x * TangentX + ConeDirection.y * TangentY + ConeDirection.z * WorldNormal;
				float3 ScaledLocalConeDirection = mul(RotatedConeDirection, (float3x3)WorldToVolume).xyz;
		
				float MinVisibility = 1;
				float WorldStepOffset = GetStepOffset(0);

				#if USE_GLOBAL_DISTANCE_FIELD
					WorldStepOffset += 2;
				#endif

				float CurrentStepOffset = WorldStepOffset;

				for (uint StepIndex = 0; StepIndex < NUM_CONE_STEPS && WorldStepOffset < MaxWorldStepOffset; StepIndex++)
				{
					float3 StepSamplePosition = VolumeShadingPosition + ScaledLocalConeDirection * WorldStepOffset;
					float3 ClampedSamplePosition = fastClamp(StepSamplePosition, -LocalPositionExtent, LocalPositionExtent);
					float DistanceToClamped = lengthFast(StepSamplePosition - ClampedSamplePosition);

					float3 StepVolumeUV = DistanceFieldVolumePositionToUV(ClampedSamplePosition, UVScaleAndVolumeScale.xyz, UVAddAndSelfShadowBias.xyz);
					float DistanceToOccluder = (SampleMeshDistanceField(StepVolumeUV, DistanceFieldMAD).x + DistanceToClamped) * UVScaleAndVolumeScale.w;

					float SphereRadius = WorldStepOffset * TanConeHalfAngle;
					float InvSphereRadius = rcpFast(SphereRadius);

					// Derive visibility from 1d intersection
					float Visibility = saturate(DistanceToOccluder * InvSphereRadius);

					// Don't allow small objects to fully occlude a cone step
					float SmallObjectVisibility = 1 - saturate(ObjectOccluderRadius * InvSphereRadius);

					// Don't allow occlusion within an object's self shadow distance
					float SelfShadowVisibility = 1 - saturate(WorldStepOffset * SelfShadowScale);
					
					Visibility = max(Visibility, max(SmallObjectVisibility, SelfShadowVisibility));

					float OccluderDistanceFraction = (WorldStepOffset + DistanceToOccluder) * InvMaxOcclusionDistance;

					// Fade out occlusion based on distance to occluder to avoid a discontinuity at the max AO distance
					float DistanceFadeout = saturate(OccluderDistanceFraction * OccluderDistanceFraction * .6f);
					Visibility = max(Visibility, DistanceFadeout);
					MinVisibility = min(Visibility, MinVisibility);
					
					float MinStepScale = .6f;

					#if USE_GLOBAL_DISTANCE_FIELD
						MinStepScale = 2;
					#endif

					float NextStepOffset = GetStepOffset(StepIndex + 1);
					float MinStepSize = MinStepScale * (NextStepOffset - CurrentStepOffset);
					CurrentStepOffset = NextStepOffset;
					WorldStepOffset += max(DistanceToOccluder, MinStepSize);
				}

				InterlockedMin(RWScreenGridConeVisibility[ConeIndex * ScreenGridConeVisibilitySize.x * ScreenGridConeVisibilitySize.y + OutputBaseIndex], asuint(MinVisibility));
			}
		}
	}
}