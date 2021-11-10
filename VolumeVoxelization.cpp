DrawDynamicMeshPass(View, RHICmdList,
[&View, VolumetricFogDistance, &RHICmdList, &VolumetricFogGridSize, &GridZParams] (FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
{
	const FScene* Scene = View.Family->Scene->GetRenderScene();
	FMeshProcessor MeshProcessor(Scene, Scene->GetFeatureLevel(), &View, DynamicMeshPassContext);
	
	FMeshPassProcessorRenderState PassRenderState;
	{
		PassRenderState.SetBlendState(TStaticBlendState<
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One,
			CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI());
		PassRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
		PassRenderState.SetViewUniformBuffer(Scene->UniformBuffers.VoxelizeVolumeViewUniformBuffer);
	}

	for (int32 MeshBatchIndex = 0; MeshBatchIndex < View.VolumetricMeshBatches.Num(); ++MeshBatchIndex)
	{
		const FMeshBatch& OriginalMesh = *View.VolumetricMeshBatches[MeshBatchIndex].Mesh;
		const FPrimitiveSceneProxy* PrimitiveSceneProxy = View.VolumetricMeshBatches[MeshBatchIndex].Proxy;			
		
		FMaterialRenderProxy* MaterialRenderProxy = OriginalMesh.MaterialRenderProxy;
		const FMaterial& Material = OriginalMesh.MaterialRenderProxy->GetMaterialWithFallback(View.GetFeatureLevel(), MaterialRenderProxy);

		if (Material.GetMaterialDomain() != MD_Volume)
			return;

		const int32 NumVoxelizationPasses = foo(PrimitiveSceneProxy->GetBounds(), View, GridZParams, VolumetricFogGridSize.Z);
		
		assume(!OriginalMesh.VertexFactory->RendersPrimitivesAsCameraFacingSprites());
		FMeshBatch QuadMesh;
		{
			QuadMesh.VertexFactory = GQuadMeshVertexFactory;
			QuadMesh.Elements[0].IndexBuffer = &GQuadMeshIndexBuffer;
			QuadMesh.Elements[0].PrimitiveUniformBuffer = OriginalMesh.Elements[0].PrimitiveUniformBuffer;
			QuadMesh.Elements[0].FirstIndex = 0;
			QuadMesh.Elements[0].NumPrimitives = 2;
			QuadMesh.Elements[0].MinVertexIndex = 0;
			QuadMesh.Elements[0].MaxVertexIndex = 3;
		}

		TMeshProcessorShaders<FVoxelizeVolumeVS, FMeshMaterialShader, FMeshMaterialShader, FVoxelizeVolumePS, FVoxelizeVolumeGS> PassShaders;
		{
			PassShaders.VertexShader = Material.GetShader<TVoxelizeVolumeVS<VMode_Object_Box>>(QuadMesh.VertexFactory->GetType());
			PassShaders.GeometryShader = Material.GetShader<TVoxelizeVolumeGS<VMode_Object_Box>>(QuadMesh.VertexFactory->GetType());
			PassShaders.PixelShader = Material.GetShader<TVoxelizeVolumePS<VMode_Object_Box>>(QuadMesh.VertexFactory->GetType());
		}

		const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader);

		for (int32 PassIndex = 0; PassIndex < NumVoxelizationPasses; PassIndex++)
		{
			FVoxelizeVolumeShaderElementData ShaderElementData(PassIndex);
			ShaderElementData.InitializeMeshMaterialData(&View, PrimitiveSceneProxy, QuadMesh, -1, );

			MeshProcessor.BuildMeshDrawCommands(
				QuadMesh, ~0ull, PrimitiveSceneProxy,
				MaterialRenderProxy, Material,
				PassRenderState, PassShaders, FM_Solid, CM_None,
				SortKey, EMeshPassFeatures::Default,
				ShaderElementData);
		}
	}
}
);




FVertexFactoryIntermediates GetVertexFactoryIntermediates(FVertexFactoryInput Input)
{
	FVertexFactoryIntermediates Intermediates;
	Intermediates.PrimitiveId = Input.PrimitiveId;
	//Intermediates.Color = LocalVF_VertexFetch_ColorComponentsBuffer[Input.VertexId & LocalVF_VertexFetch_Parameters[0]] .bgra ;
	float TangentSign;
	Intermediates.TangentToLocal = CalcTangentToLocal(Input, TangentSign);
	Intermediates.TangentToWorld = CalcTangentToWorld(Intermediates, Intermediates.TangentToLocal);
	Intermediates.TangentToWorldSign = TangentSign * GetPrimitiveData(Intermediates.PrimitiveId).InvNonUniformScaleAndDeterminantSign.w;
	return Intermediates;
}

FVertexFactoryInterpolantsVSToPS VertexFactoryGetInterpolantsVSToPS(FVertexFactoryInput Input, FVertexFactoryIntermediates Intermediates, FMaterialVertexParameters VertexParameters)
{
	FVertexFactoryInterpolantsVSToPS Interpolants = (FVertexFactoryInterpolantsVSToPS)0;
	SetTangents(Interpolants, Intermediates.TangentToWorld[0], Intermediates.TangentToWorld[2], Intermediates.TangentToWorldSign);
	//SetColor(Interpolants, Intermediates.Color);
	SetPrimitiveId(Interpolants, Intermediates.PrimitiveId);
	return Interpolants;
}

struct FVertexFactoryIntermediates
{
	float3x3  TangentToLocal;
	float3x3  TangentToWorld;
	float  TangentToWorldSign;
	float4  Color;
	uint PrimitiveId;
};

struct FVertexFactoryInput
{
	float4 Position : ATTRIBUTE0;
	uint PrimitiveId : ATTRIBUTE13;
	uint VertexId : SV_VertexID;
};

struct FVertexFactoryInterpolantsVSToPS
{
	float4 TangentToWorld0 : TEXCOORD10_centroid; 
	float4 TangentToWorld2 : TEXCOORD11_centroid;
	nointerpolation uint PrimitiveId : PRIMITIVE_ID;
};

struct FVoxelizeVolumePrimitiveVSToGS
{
	FVertexFactoryInterpolantsVSToPS FactoryInterpolants;
	float2 VertexQuadCoordinate : ATTRIBUTE0;
};

struct FVoxelizeVolumePrimitiveGSToPS
{
	FVertexFactoryInterpolantsVSToPS FactoryInterpolants;
	float4 OutPosition : SV_POSITION;
	uint SliceIndex : SV_RenderTargetArrayIndex;
};

void VoxelizeVS(
	FVertexFactoryInput Input,
	uint VertexId : SV_VertexID,
	out FVoxelizeVolumePrimitiveVSToGS Output )
{
	ResolvedView = ResolveView();

	FVertexFactoryIntermediates VFIntermediates = GetVertexFactoryIntermediates(Input);
	float4 TranslatedWorldPosition = VertexFactoryGetWorldPosition(Input, VFIntermediates);
	//FMaterialVertexParameters VertexParameters = GetMaterialVertexParameters(Input, VFIntermediates, TranslatedWorldPosition.xyz);

	Output.FactoryInterpolants = VertexFactoryGetInterpolantsVSToPS(Input, VFIntermediates, VertexParameters);
	Output.VertexQuadCoordinate = float2(VertexId & 1, (VertexId >> 1) & 1);
}

float4 GetViewSpaceAABW(uint PrimitiveId)  // AABW: Axis Aligned Bounding Window
{
	float3 LocalObjectBoundsMin = GetPrimitiveData(PrimitiveId).LocalObjectBoundsMin;
	float3 LocalObjectBoundsMax = GetPrimitiveData(PrimitiveId).LocalObjectBoundsMax;
	float4x4 LocalToWorld = GetPrimitiveData(PrimitiveId).LocalToWorld;

	float2 ViewSpaceAABWMin = 10000000;
	float2 ViewSpaceAABWMax = -10000000;
	for (int BoxVertex = 0; BoxVertex < 8; BoxVertex++)
	{
		float3 BoxVertexMask = float3((BoxVertex >> 0) & 1, (BoxVertex >> 1) & 1, (BoxVertex >> 2) & 1);
		float3 BoxVertexLocalPosition = LocalObjectBoundsMin + (LocalObjectBoundsMax - LocalObjectBoundsMin) * BoxVertexMask;
		float3 BoxVertexPosition = mul(float4(BoxVertexLocalPosition, 1), LocalToWorld).xyz;
		float3 ViewSpaceBoxVertex = mul(float4(BoxVertexPosition + ResolvedView.PreViewTranslation, 1), ResolvedView.TranslatedWorldToView).xyz;
		ViewSpaceAABWMin = min(ViewSpaceAABWMin, ViewSpaceBoxVertex.xy);
		ViewSpaceAABWMax = max(ViewSpaceAABWMax, ViewSpaceBoxVertex.xy);
	}

	return float4(ViewSpaceAABWMin, ViewSpaceAABWMax);
}

// #define MAX_SLICES_PER_VOXELIZATION_PASS N
[maxvertexcount(N * 3)]
void VoxelizeGS(
	triangle FVoxelizeVolumePrimitiveVSToGS Inputs[3], 
	inout TriangleStream<FVoxelizeVolumePrimitiveGSToPS> OutStream )
{
	ResolvedView = ResolveView();
	uint PrimitiveId = Inputs[0].FactoryInterpolants.PrimitiveId;

	int GridSizeZ = VolumetricFog.GridSize.z;
	float4 BoundingSphere = GetPrimitiveData(PrimitiveId).ObjectWorldPositionAndRadius;
	float3 ViewSpacePrimitiveVolumeOrigin = mul(float4(BoundingSphere.xyz, 1), ResolvedView.WorldToView).xyz;	

	float QuadDepth = ViewSpacePrimitiveVolumeOrigin.z;
	int FurthestSliceIndexUnclamped = ComputeZSliceFromDepth(QuadDepth + BoundingSphere.w);
	int ClosestSliceIndexUnclamped = ComputeZSliceFromDepth(QuadDepth - BoundingSphere.w);

	// Clamp to valid range, start at first slice for the current pass
	int FirstSlice = max(ClosestSliceIndexUnclamped, 0) + N * VoxelizationPassIndex;
	// Clamp to valid range, end at the last slice for the current pass
	int LastSlice = min(min(FurthestSliceIndexUnclamped, GridSizeZ - 1), FirstSlice + N - 1);
	
	if (ClosestSliceIndexUnclamped < GridSizeZ
		&& FirstSlice <= FurthestSliceIndexUnclamped
		&& FurthestSliceIndexUnclamped >= 0)
	{
		float4 ViewSpaceAABW = GetViewSpaceAABW(PrimitiveId);
		float2 AABWVertices[3];	
		for (uint i = 0; i < 3; i++)
		{
			float2 CornerMask = Inputs[i].VertexQuadCoordinate;
			AABWVertices[i] = ViewSpaceAABW.xy + (ViewSpaceAABW.zw - ViewSpaceAABW.xy) * CornerMask;
		}
	
		for (int SliceIndex = FirstSlice; SliceIndex <= LastSlice; SliceIndex++)
		{
			float SliceDepth = ComputeDepthFromZSlice(SliceIndex + VoxelizeVolumePass.FrameJitterOffset0.z);

			if ( QuadDepth - BoundingSphere.w < SliceDepth < QuadDepth + BoundingSphere.w )
			{
				for (uint i = 0; i < 3; i++)
				{
					FVoxelizeVolumePrimitiveGSToPS Output;
					Output.FactoryInterpolants = Inputs[i].FactoryInterpolants;
					Output.SliceIndex = SliceIndex;
					Output.OutPosition = float4(AABWVertices[i], SliceDepth, 1) * VoxelizeVolumePass.ViewToVolumeClip;
					OutStream.Append(Output);
				}
				OutStream.RestartStrip();
			}
		}
	}
}


float ComputeVolumeShapeMasking(float3 WorldPosition, uint PrimitiveId, FVertexFactoryInterpolantsVSToPS FactoryInterpolants)
{
	float3 LocalObjectBoundsMin = GetPrimitiveData(PrimitiveId).LocalObjectBoundsMin;
	float3 LocalObjectBoundsMax = GetPrimitiveData(PrimitiveId).LocalObjectBoundsMax;
	float3 LocalPosition = mul(float4(WorldPosition, 1), GetPrimitiveData(PrimitiveId).WorldToLocal).xyz;

	return LocalObjectBoundsMin < LocalPosition < LocalObjectBoundsMax ? 1 : 0;
}

void VoxelizePS(
	FVoxelizeVolumePrimitiveGSToPS Interpolants,
	in float4 SvPosition : SV_Position,
	out float4 OutVBufferA : SV_Target0,
	out float4 OutVBufferB : SV_Target1
	)
{
	ResolvedView = ResolveView();
	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(Interpolants.FactoryInterpolants, SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;
	CalcMaterialParameters(MaterialParameters, PixelMaterialInputs, SvPosition, true);
	float3 EmissiveColor = clamp(GetMaterialEmissiveRaw(PixelMaterialInputs), 0.0f, 65000.0f);
	float3 Albedo = GetMaterialBaseColor(PixelMaterialInputs);
	float Extinction = clamp(GetMaterialOpacityRaw(PixelMaterialInputs), 0.0f, 65000.0f);

	float FadeStart = .6f;
	float SliceFadeAlpha = 1 - saturate((SvPosition.w / VolumetricFog.MaxDistance - FadeStart) / (1 - FadeStart));
	float Scale = 0.01f * pow(SliceFadeAlpha, 3);

	// Would be faster to branch around the whole material evaluation, but that would cause divergent flow control for gradient operations
	Scale *= ComputeVolumeShapeMasking(MaterialParameters.AbsoluteWorldPosition, MaterialParameters.PrimitiveId, Interpolants.FactoryInterpolants);

	OutVBufferA = float4(Albedo * Extinction * Scale, Extinction * Scale);
	OutVBufferB = float4(EmissiveColor * Scale, 0);
}
