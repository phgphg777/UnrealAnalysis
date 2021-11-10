

----------------------------------------FCullObjectsForViewCS----------------------------------------
RWBuffer<uint> RWObjectIndirectArguments;			// = GAOCulledObjectBuffers.Buffers.ObjectIndirectArguments;	(in FDistanceFieldCulledObjectBufferParameters)
RWStructuredBuffer<float4> RWCulledObjectBounds;	// = GAOCulledObjectBuffers.Buffers.Bounds;		(in FDistanceFieldCulledObjectBufferParameters)
RWStructuredBuffer<float4> RWCulledObjectData;		// = GAOCulledObjectBuffers.Buffers.Data;  		(in FDistanceFieldCulledObjectBufferParameters)

#define UPDATEOBJECTS_THREADGROUP_SIZE 64  	// = UpdateObjectsGroupSize;
#define OBJECT_DATA_STRIDE 17				// == FDistanceFieldObjectBuffers::ObjectDataStride;
#define CULLED_OBJECT_DATA_STRIDE 17		// == FDistanceFieldCulledObjectBuffers::ObjectDataStride;
Buffer<float4> SceneObjectBounds;		// = Scene->DistanceFieldSceneData.ObjectBuffers->Bounds;			(in FDistanceFieldObjectBufferParameters)
Buffer<float4> SceneObjectData;			// = Scene->DistanceFieldSceneData.ObjectBuffers->Data;				(in FDistanceFieldObjectBufferParameters)
uint NumSceneObjects;					// = Scene->DistanceFieldSceneData.NumObjectsInBuffer;				(in FDistanceFieldObjectBufferParameters)
float AOMaxViewDistance;				// = min(GAOMaxViewDistance("r.AOMaxViewDistance",20000), 65000);   (in FAOParameters) 
uint ObjectBoundingGeometryIndexCount;	// = StencilingGeometry::GLowPolyStencilSphereIndexBuffer.NumIndices(96);
groupshared uint NumGroupObjects;
groupshared uint GroupBaseIndex;
groupshared uint GroupObjectIndices[UPDATEOBJECTS_THREADGROUP_SIZE];

[numthreads(UPDATEOBJECTS_THREADGROUP_SIZE, 1, 1)]
void CullObjectsForViewCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint ObjectIndex = DispatchThreadId.x;

	if (DispatchThreadId.x == 0)
	{
		RWObjectIndirectArguments[0] = ObjectBoundingGeometryIndexCount;
	}

	if (GroupThreadId.x == 0)
	{
		NumGroupObjects = 0;
	}

	GroupMemoryBarrierWithGroupSync();

	if (ObjectIndex < NumSceneObjects)
	{
		uint SourceIndex = ObjectIndex;

		float4 ObjectBoundingSphere = SceneObjectBounds[SourceIndex];
		float DistanceToViewSq = dot(View.WorldCameraOrigin - ObjectBoundingSphere.xyz, View.WorldCameraOrigin - ObjectBoundingSphere.xyz);

		if (DistanceToViewSq < Square(AOMaxViewDistance + ObjectBoundingSphere.w)
			&& ViewFrustumIntersectSphere(ObjectBoundingSphere.xyz, ObjectBoundingSphere.w + AOObjectMaxDistance))
		{
            float2 MinMaxDrawDist2 = SceneObjectData[SourceIndex * OBJECT_DATA_STRIDE + 6].zw;

            if ((MinMaxDrawDist2.x < 0.0001 || DistanceToViewSq > MinMaxDrawDist2.x)
                && (MinMaxDrawDist2.y < 0.0001 || DistanceToViewSq < MinMaxDrawDist2.y))
            {
                uint DestIndex;
                InterlockedAdd(NumGroupObjects, 1U, DestIndex);
                GroupObjectIndices[DestIndex] = SourceIndex;
            }
        }
	}

	GroupMemoryBarrierWithGroupSync();

	if (GroupThreadId.x == 0)
	{
		InterlockedAdd(RWObjectIndirectArguments[1], NumGroupObjects, GroupBaseIndex);
	}

	GroupMemoryBarrierWithGroupSync();

	if (GroupThreadId.x < NumGroupObjects)
	{
		uint SourceIndex = GroupObjectIndices[GroupThreadId.x];
		uint DestIndex = GroupBaseIndex + GroupThreadId.x;
		
		RWCulledObjectBounds[DestIndex] = SceneObjectBounds[SourceIndex];		
		for (uint VectorIndex = 0; VectorIndex < CULLED_OBJECT_DATA_STRIDE; VectorIndex++)
		{
			RWCulledObjectData[DestIndex * CULLED_OBJECT_DATA_STRIDE + VectorIndex]
				= SceneObjectData[SourceIndex * OBJECT_DATA_STRIDE + VectorIndex];
		}
	}
}
----------------------------------------FCullObjectsForViewCS----------------------------------------


----------------------------------------FBuildTileConesCS----------------------------------------
RWBuffer<float4> RWTileConeAxisAndCos;		// = View.State->AOTileIntersectionResources->TileConeAxisAndCos
RWBuffer<float4> RWTileConeDepthRanges;		// = View.State->AOTileIntersectionResources->TileConeDepthRanges

#define THREADGROUP_SIZEX 16	// = GDistanceFieldAOTileSizeX;
#define THREADGROUP_SIZEY 16	// = GDistanceFieldAOTileSizeY;
#define DOWNSAMPLE_FACTOR 2		// = GAODownsampleFactor;
Texture2D DistanceFieldNormalTexture;
SamplerState DistanceFieldNormalSampler;
//uint4 ViewDimensions;			// = View.ViewRect
float2 NumGroups;				// = DivideAndRoundUp(View.ViewRect.Size() / GAODownsampleFactor, GDistanceFieldAOTileSizeX)
groupshared uint IntegerTileMinZ;
groupshared uint IntegerTileMaxZ;
groupshared uint IntegerTileMinZ2;
groupshared uint IntegerTileMaxZ2;

[numthreads(THREADGROUP_SIZEX, THREADGROUP_SIZEY, 1)]
void BuildTileConesMain(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
    uint ThreadIndex = GroupThreadId.y * THREADGROUP_SIZEX + GroupThreadId.x;

	float2 BaseLevelScreenUV = (DispatchThreadId.xy + float2(.5f, .5f)) * DOWNSAMPLE_FACTOR * View.BufferSizeAndInvSize.zw;
	float SceneDepth = Texture2DSampleLevel(DistanceFieldNormalTexture, DistanceFieldNormalSampler, BaseLevelScreenUV, 0).w;

    if (ThreadIndex == 0) 
	{
        IntegerTileMinZ = IntegerTileMinZ2 = 0x7F7FFFFF;     
        IntegerTileMaxZ = IntegerTileMaxZ2 = 0;
    }

    GroupMemoryBarrierWithGroupSync();
    
	if (SceneDepth < AOMaxViewDistance)
	{
		InterlockedMin(IntegerTileMinZ, asuint(SceneDepth));
		InterlockedMax(IntegerTileMaxZ, asuint(SceneDepth));
	}

    GroupMemoryBarrierWithGroupSync();

    float MinTileZ = asfloat(IntegerTileMinZ);
    float MaxTileZ = asfloat(IntegerTileMaxZ);
	float HalfZ = .5f * (MinTileZ + MaxTileZ);

	// Compute a second min and max Z, clipped by HalfZ, so that we get two depth bounds per tile
	// This results in more conservative tile depth bounds and fewer intersections
	if (SceneDepth >= HalfZ && SceneDepth < AOMaxViewDistance)
	{
		InterlockedMin(IntegerTileMinZ2, asuint(SceneDepth));
	}

	if (SceneDepth <= HalfZ)
	{
		InterlockedMax(IntegerTileMaxZ2, asuint(SceneDepth));
	}

	GroupMemoryBarrierWithGroupSync();
	
	float MinTileZ2 = asfloat(IntegerTileMinZ2);
	float MaxTileZ2 = asfloat(IntegerTileMaxZ2);

	if (ThreadIndex == 0)
	{
		float3 TileConeVertex;
		float3 TileConeAxis;
		float TileConeAngleCos;
		float TileConeAngleSin;
		float4 ConeAxisDepthRanges;

		{
			float A = 1 / View.ViewToClip[0][0]; 	// 0.5*width / depth
			float B = 1 / View.ViewToClip[1][1];	// 0.5*height / depth
			float Dx = (A*2) / NumGroups.x; 	//  (width/depth) / NumGroups.x  ==  (width/NumGroups.x) / depth
			float Dy = (B*2) / NumGroups.y;		//  (height/depth) / NumGroups.y  ==  (height/NumGroups.y) / depth

			float3 TileCorner00 = normalize( float3 (
				(GroupId.x + 0) * Dx - A, 	B - (GroupId.y + 0) * Dy, 	1 ) );
			float3 TileCorner10 = normalize( float3(
				(GroupId.x + 1) * Dx - A, 	B - (GroupId.y + 0) * Dy,  	1 ) );
			float3 TileCorner01 = normalize( float3(
				(GroupId.x + 0) * Dx - A,  	B - (GroupId.y + 1) * Dy,  	1 ) );
			float3 TileCorner11 = normalize( float3(
				(GroupId.x + 1) * Dx - A,  	B - (GroupId.y + 1) * Dy,  	1 ) );

			TileConeAxis = normalize(TileCorner00 + TileCorner10 + TileCorner01 + TileCorner11);
			TileConeAngleCos = dot(TileConeAxis, TileCorner00);

			float DistanceToNearPlane = length(TileConeAxis / TileConeAxis.z * View.NearPlane);
			
			ConeAxisDepthRanges = 
				float4(MinTileZ, MaxTileZ2, MinTileZ2m MaxTileZ) * (1.0f/TileConeAxis.z) + DistanceToNearPlane;
		}

		uint TileIndex = GroupId.y * NumGroups.x + GroupId.x;
		RWTileConeAxisAndCos[TileIndex] = float4(TileConeAxis, TileConeAngleCos);
		RWTileConeDepthRanges[TileIndex] = ConeAxisDepthRanges;
	}
}
----------------------------------------FBuildTileConesCS----------------------------------------



----------------------------------------TObjectCullPS----------------------------------------
struct FObjectCullVertexOutput
{
	nointerpolation float4 PositionAndRadius : TEXCOORD0;
	nointerpolation uint ObjectIndex : TEXCOORD1;
};

StructuredBuffer<float4> CulledObjectBounds;	// = GAOCulledObjectBuffers.Buffers.Bounds;		(in FDistanceFieldCulledObjectBufferParameters)
float ConservativeRadiusScale = sqrt(2);		// = 1.0f / Cos(PI/NumRings);					(NumRings = StencilingGeometry::GLowPolyStencilSphereVertexBuffer.GetNumRings())

void ObjectCullVS(
	float4 InPosition : ATTRIBUTE0,
	uint ObjectIndex : SV_InstanceID,
	out FObjectCullVertexOutput Output,
	out float4 OutPosition : SV_POSITION
	)
{
	float4 ObjectPositionAndRadius = CulledObjectBounds[ObjectIndex];
	float EffectiveRadius = (ObjectPositionAndRadius.w + AOObjectMaxDistance) * ConservativeRadiusScale;
	float3 WorldPosition = InPosition.xyz * EffectiveRadius + ObjectPositionAndRadius.xyz;
	OutPosition = mul(float4(WorldPosition, 1), View.WorldToClip);
	Output.PositionAndRadius = ObjectPositionAndRadius;
	Output.ObjectIndex = ObjectIndex;
} 


RWBuffer<uint> RWNumCulledTilesArray;		// = View.State->AOTileIntersectionResources->NumCulledTilesArray
RWBuffer<uint> RWCulledTileDataArray;		// = View.State->AOTileIntersectionResources->CulledTileDataArray

#define DOWNSAMPLE_FACTOR 2	
#define SCATTER_CULLING_COUNT_PASS 1 or 0
#define CULLED_TILE_DATA_STRIDE 2 			// = CulledTileDataStride 					(in FTileIntersectionParameters)
//StructuredBuffer<float4> CulledObjectData;// = GAOCulledObjectBuffers.Buffers.Data;  	(in FDistanceFieldCulledObjectBufferParameters)
Buffer<uint> CulledTilesStartOffsetArray;	// = View.State->AOTileIntersectionResources->CulledTilesStartOffsetArray (in FTileIntersectionParameters)
Buffer<float4> TileConeAxisAndCos;			// = View.State->AOTileIntersectionResources->TileConeAxisAndCos
Buffer<float4> TileConeDepthRanges;			// = View.State->AOTileIntersectionResources->TileConeDepthRanges
float AOMaxViewDistance;

void ObjectCullPS(
	FObjectCullVertexOutput Input, 
	in float4 SVPos : SV_POSITION,
	out float4 OutColor : SV_Target0)
{
	OutColor = 0;
	
	uint2 TilePosition = (uint2)SVPos.xy;
	uint TileIndex = TilePosition.y * NumGroups.x + TilePosition.x;

#if SCATTER_CULLING_COUNT_PASS
	InterlockedAdd(RWNumCulledTilesArray[Input.ObjectIndex], 1);
#else
	uint CulledTileIndex;
	InterlockedAdd(RWNumCulledTilesArray[Input.ObjectIndex], 1, CulledTileIndex);

	uint CulledTileDataStart = CulledTilesStartOffsetArray[Input.ObjectIndex];

	RWCulledTileDataArray[(CulledTileDataStart + CulledTileIndex) * CULLED_TILE_DATA_STRIDE + 0] = TileIndex;
	RWCulledTileDataArray[(CulledTileDataStart + CulledTileIndex) * CULLED_TILE_DATA_STRIDE + 1] = Input.ObjectIndex;
#endif
}
----------------------------------------TObjectCullPS----------------------------------------


----------------------------------------FComputeCulledTilesStartOffsetCS----------------------------------------
RWBuffer<uint> RWCulledTilesStartOffsetArray;	// = View.State->AOTileIntersectionResources->CulledTilesStartOffsetArray 	(in FTileIntersectionParameters)
RWBuffer<uint> RWCulledTileDataArray; 			// = View.State->AOTileIntersectionResources->CulledTileDataArray 			(in FTileIntersectionParameters)
RWBuffer<uint> RWObjectTilesIndirectArguments;	// = View.State->AOTileIntersectionResources->ObjectTilesIndirectArguments 	(in FTileIntersectionParameters)

#define CULLED_TILE_DATA_STRIDE 2					// = CulledTileDataStride 				(in FTileIntersectionParameters)
#define CULLED_TILE_SIZEX 16						// = GDistanceFieldAOTileSizeX			(in FTileIntersectionParameters)
#define TRACE_DOWNSAMPLE_FACTOR 4 					// = GConeTraceDownsampleFactor 		(in FTileIntersectionParameters)
#define CONE_TRACE_OBJECTS_THREADGROUP_SIZE 64		// = ConeTraceObjectsThreadGroupSize 	(in FTileIntersectionParameters)
#define COMPUTE_START_OFFSET_GROUP_SIZE 64			// = ComputeStartOffsetGroupSize
#define CONE_TILE_SIZEX (16/4)						// (CULLED_TILE_SIZEX / TRACE_DOWNSAMPLE_FACTOR) 
#define CONE_TRACE_TILES_PER_THREADGROUP (64/(4*4))	// (CONE_TRACE_OBJECTS_THREADGROUP_SIZE / (CONE_TILE_SIZEX * CONE_TILE_SIZEX))
Buffer<uint> ObjectIndirectArguments;	// = GAOCulledObjectBuffers.Buffers.ObjectIndirectArguments;(in FDistanceFieldCulledObjectBufferParameters)
Buffer<uint> NumCulledTilesArray;

[numthreads(COMPUTE_START_OFFSET_GROUP_SIZE, 1, 1)]
void ComputeCulledTilesStartOffsetCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
	uint3 GroupThreadId : SV_GroupThreadID)
{
	uint ObjectIndex = DispatchThreadId.x;
	uint NumCulledObjects = ObjectIndirectArguments[1];

	if (ObjectIndex < NumCulledObjects)
	{
		uint NumIntersectingTiles = NumCulledTilesArray[ObjectIndex];
		uint NumConeTraceThreadGroups = DivideAndRoundUp(NumIntersectingTiles, CONE_TRACE_TILES_PER_THREADGROUP);

		uint StartOffsetThreadGroups;
		InterlockedAdd(RWObjectTilesIndirectArguments[0], NumConeTraceThreadGroups, StartOffsetThreadGroups);
		RWCulledTilesStartOffsetArray[ObjectIndex] = StartOffsetThreadGroups * CONE_TRACE_TILES_PER_THREADGROUP;

		// Pad remaining entries with INVALID_TILE_INDEX so we can skip computing them in the cone tracing pass
		for (uint PaddingTileIndex = NumIntersectingTiles; PaddingTileIndex < NumConeTraceThreadGroups * CONE_TRACE_TILES_PER_THREADGROUP; PaddingTileIndex++)
		{
			RWCulledTileDataArray[(StartOffset + PaddingTileIndex) * CULLED_TILE_DATA_STRIDE + 0] = INVALID_TILE_INDEX;
			RWCulledTileDataArray[(StartOffset + PaddingTileIndex) * CULLED_TILE_DATA_STRIDE + 1] = ObjectIndex;
		}
	}

	if (DispatchThreadId.x == 0)
	{
		RWObjectTilesIndirectArguments[1] = 1;
		RWObjectTilesIndirectArguments[2] = 1;
	}
}
----------------------------------------FComputeCulledTilesStartOffsetCS----------------------------------------


----------------------------------------TConeTraceScreenGridObjectOcclusionCS----------------------------------------
[numthreads(CONE_TRACE_GLOBAL_DISPATCH_SIZEX, CONE_TRACE_GLOBAL_DISPATCH_SIZEX, 1)]
void ConeTraceGlobalOcclusionCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint2 OutputCoordinate = DispatchThreadId.xy;
	uint OutputBaseIndex = OutputCoordinate.y * ScreenGridConeVisibilitySize.x + OutputCoordinate.x;

	if (all(OutputCoordinate < ScreenGridConeVisibilitySize))
	{
		float2 BaseLevelScreenUV = GetBaseLevelScreenUVFromScreenGrid(OutputCoordinate);
		float3 WorldNormal, TangentX, TangentY;
		float SceneDepth;
		GetDownsampledGBuffer(BaseLevelScreenUV, WorldNormal, SceneDepth);
		FindBestAxisVectors2(WorldNormal, TangentX, TangentY);

		float2 ScreenPosition = (BaseLevelScreenUV.xy - View.ScreenPositionScaleBias.wz) / View.ScreenPositionScaleBias.xy;
		float3 WorldShadingPosition = mul(float4(ScreenPosition * SceneDepth, SceneDepth, 1), View.ScreenToWorld).xyz;

		HemisphereConeTraceAgainstGlobalDistanceField(OutputBaseIndex, WorldShadingPosition, /*SceneDepth*/, WorldNormal, TangentX, TangentY);
	}
}
----------------------------------------TConeTraceScreenGridObjectOcclusionCS----------------------------------------


----------------------------------------TConeTraceScreenGridObjectOcclusionCS----------------------------------------
[numthreads(CONE_TRACE_OBJECTS_THREADGROUP_SIZE, 1, 1)]
void ConeTraceObjectOcclusionCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint CulledTileDataBaseIndex = GroupId.x * CONE_TRACE_TILES_PER_THREADGROUP;
	uint SubCulledTileIndex = GroupThreadId.x / (CONE_TILE_SIZEX * CONE_TILE_SIZEX);
	uint CulledTileIndex = CulledTileDataBaseIndex + SubCulledTileIndex;
	
	uint TileIndex = CulledTileDataArray[CulledTileIndex * CULLED_TILE_DATA_STRIDE + 0];
	uint2 TileCoordinate = uint2(TileIndex % TileListGroupSize.x, TileIndex / TileListGroupSize.x);
	
	uint PixelIndex = GroupThreadId.x % (CONE_TILE_SIZEX * CONE_TILE_SIZEX);
	uint2 PixelCoordinate = uint2(PixelIndex % CONE_TILE_SIZEX, PixelIndex / CONE_TILE_SIZEX);
	
	uint2 OutputCoordinate = TileCoordinate * CONE_TILE_SIZEX + PixelCoordinate;
	uint OutputBaseIndex = OutputCoordinate.y * ScreenGridConeVisibilitySize.x + OutputCoordinate.x;

	uint ObjectIndex = CulledTileDataArray[CulledTileIndex * CULLED_TILE_DATA_STRIDE + 1];

	if (TileIndex != INVALID_TILE_INDEX && all(OutputCoordinate < ScreenGridConeVisibilitySize))
	{
		float2 BaseLevelScreenUV = GetBaseLevelScreenUVFromScreenGrid(OutputCoordinate);
		float3 WorldNormal, TangentX, TangentY;
		float SceneDepth;
		GetDownsampledGBuffer(BaseLevelScreenUV, WorldNormal, SceneDepth);
		FindBestAxisVectors2(WorldNormal, TangentX, TangentY);

		float2 ScreenPosition = (BaseLevelScreenUV.xy - View.ScreenPositionScaleBias.wz) / View.ScreenPositionScaleBias.xy;
		float3 WorldShadingPosition = mul(float4(ScreenPosition * SceneDepth, SceneDepth, 1), View.ScreenToWorld).xyz;

		HemisphereConeTraceAgainstTileCulledObjects(ObjectIndex, OutputBaseIndex, WorldShadingPosition, /*SceneDepth*/, WorldNormal, TangentX, TangentY);
	}
}
----------------------------------------TConeTraceScreenGridObjectOcclusionCS----------------------------------------


----------------------------------------FCombineConeVisibilityCS----------------------------------------
RWTexture2D<float4> RWDistanceFieldBentNormal;

#define COMBINE_CONES_SIZEX 8			// = GCombineConesSizeX
Buffer<uint> ScreenGridConeVisibility;
uint2 ConeBufferMax;					// = (View.ViewRect / GAODownsampleFactor / GConeTraceDownsampleFactor) - 1
float2 DFNormalBufferUVMax;				// = (View.ViewRect / GAODownsampleFactor - 0.5f) / (BufferSize / GAODownsampleFactor)
float BentNormalNormalizeFactor;		// = 1 / Avr(SpacedVectors9[]).Size()

[numthreads(COMBINE_CONES_SIZEX, COMBINE_CONES_SIZEX, 1)]
void CombineConeVisibilityCS(
	uint3 GroupId : SV_GroupID,
	uint3 DispatchThreadId : SV_DispatchThreadID,
    uint3 GroupThreadId : SV_GroupThreadID) 
{
	uint2 InputCoordinate = min(DispatchThreadId.xy, ConeBufferMax);
	uint InputBaseIndex = InputCoordinate.y * ScreenGridConeVisibilitySize.x + InputCoordinate.x;
	float2 BaseLevelScreenUV = GetBaseLevelScreenUVFromScreenGrid(InputCoordinate);
	BaseLevelScreenUV = min(BaseLevelScreenUV, DFNormalBufferUVMax);

	uint2 OutputCoordinate = DispatchThreadId.xy;
	
	float3 WorldNormal, TangentX, TangentY;
	float SceneDepth;
	GetDownsampledGBuffer(BaseLevelScreenUV, WorldNormal, SceneDepth);
	FindBestAxisVectors2(WorldNormal, TangentX, TangentY);

	float3 UnoccludedDirection = 0;
	for (uint ConeIndex = 0; ConeIndex < NUM_CONE_DIRECTIONS; ConeIndex++)
	{
		float ConeVisibility = asfloat(ScreenGridConeVisibility[ConeIndex * ScreenGridConeVisibilitySize.x * ScreenGridConeVisibilitySize.y + InputBaseIndex]);
		float3 ConeDirection = AOSamples2.SampleDirections[ConeIndex].xyz;
		float3 RotatedConeDirection = ConeDirection.x * TangentX + ConeDirection.y * TangentY + ConeDirection.z * WorldNormal;
		
		UnoccludedDirection += ConeVisibility * RotatedConeDirection;
	}

	float3 BentNormal = (UnoccludedDirection / (float)NUM_CONE_DIRECTIONS) * BentNormalNormalizeFactor;

	RWDistanceFieldBentNormal[OutputCoordinate] = float4(BentNormal, SceneDepth);
}
----------------------------------------FCombineConeVisibilityCS----------------------------------------