
Scene->StaticMeshes.Num == 15;
//Scene->StaticMeshes[0].PrimitiveSceneInfo->Proxy.OwnerName == "Floor";
//Scene->StaticMeshes[1].PrimitiveSceneInfo->Proxy.OwnerName == "Floor";
Scene->StaticMeshes[2].PrimitiveSceneInfo->Proxy.OwnerName == "Table";
//Scene->StaticMeshes[3].PrimitiveSceneInfo->Proxy.OwnerName == "Table";
//Scene->StaticMeshes[4].PrimitiveSceneInfo->Proxy.OwnerName == "BP_Sky_Sphere_C_2";
//Scene->StaticMeshes[5].PrimitiveSceneInfo->Proxy.OwnerName == ;
Scene->StaticMeshes[6].PrimitiveSceneInfo->Proxy.OwnerName == "BP_Sky_Sphere_C_2";
Scene->StaticMeshes[7].PrimitiveSceneInfo->Proxy.OwnerName == "Statue";
Scene->StaticMeshes[8].PrimitiveSceneInfo->Proxy.OwnerName == "Statue";
//Scene->StaticMeshes[9].PrimitiveSceneInfo->Proxy.OwnerName == "Chair";
Scene->StaticMeshes[10].PrimitiveSceneInfo->Proxy.OwnerName == "Chair";
Scene->StaticMeshes[11].PrimitiveSceneInfo->Proxy.OwnerName == "Chair_15";
//Scene->StaticMeshes[12].PrimitiveSceneInfo->Proxy.OwnerName == "Chair_15";
//Scene->StaticMeshes[13].PrimitiveSceneInfo->Proxy.OwnerName == "Floor_14";
Scene->StaticMeshes[14].PrimitiveSceneInfo->Proxy.OwnerName == "Floor_14";


typedef TArray<uint8> FPrimitiveViewMasks;

void FSceneRenderer::ComputeViewVisibility( , 
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess, 
	FViewVisibleCommandsPerView& ViewCommandsPerView, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer, 
	FGlobalDynamicVertexBuffer& DynamicVertexBuffer, 
	FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	FPrimitiveViewMasks HasDynamicMeshElementsMasks;
	FPrimitiveViewMasks HasDynamicEditorMeshElementsMasks;
	FPrimitiveViewMasks HasViewCustomDataMasks;
	HasDynamicMeshElementsMasks.AddZeroed(Scene->Primitives.Num());
	HasDynamicEditorMeshElementsMasks.AddZeroed(Scene->Primitives.Num());
	HasViewCustomDataMasks.AddZeroed(Scene->Primitives.Num());

	uint8 ViewBit = 0x1;
	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex, ViewBit <<= 1)
	{
		FViewInfo& View = Views[ViewIndex];
		FViewCommands& ViewCommands = ViewCommandsPerView[ViewIndex];
		FSceneViewState* ViewState = (FSceneViewState* )View.State;

		View.PrimitiveVisibilityMap.Init(false,Scene->Primitives.Num());
		FrustumCull<, , >(Scene, View);
		OcclusionCull(RHICmdList, Scene, View, DynamicVertexBuffer);

		View.StaticMeshVisibilityMap.Init(false,Scene->StaticMeshes.GetMaxIndex());
		ComputeAndMarkRelevanceForViewParallel(, , , ViewCommands, ViewBit, HasDynamicMeshElementsMasks, HasDynamicEditorMeshElementsMasks, HasViewCustomDataMasks);
	}

	GatherDynamicMeshElements(, , , DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer,
		HasDynamicMeshElementsMasks, HasDynamicEditorMeshElementsMasks, HasViewCustomDataMasks, MeshCollector);

	for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
	{
		FViewInfo& View = Views[ViewIndex];
		FViewCommands& ViewCommands = ViewCommandsPerView[ViewIndex];
		SetupMeshPass(View, BasePassDepthStencilAccess, ViewCommands);
	}
}

void ComputeAndMarkRelevanceForViewParallel( , , ,
	FViewCommands& ViewCommands,
	uint8 ViewBit,
	FPrimitiveViewMasks& OutHasDynamicMeshElementsMasks,
	FPrimitiveViewMasks& OutHasDynamicEditorMeshElementsMasks,
	FPrimitiveViewMasks& HasViewCustomDataMasks
	)
{
	TArray<FRelevancePacket*> Packets;
	uint8* MarkMasks = (uint8*)FMemStack::Get().Alloc(Scene->Primitives.Num() + 31 , 8);
	FSceneSetBitIterator BitIt(View.PrimitiveVisibilityMap);
	
	for(int i=0; i<numPacket; ++i)
	{
		FRelevancePacket* Packet = new FRelevancePacket(
			,,,ViewCommands,,ViewData,
			OutHasDynamicMeshElementsMasks,
			OutHasDynamicEditorMeshElementsMasks,
			MarkMasks, ,);
		Packets.Add(Packet);

		while(!Packet->Input.IsFull() && BitIt)
		{
			Packet->Input.AddPrim(BitIt.GetIndex());
			++BitIt;
		}
		
		if(!BitIt)
			break;
	}

	ParallelFor(Packets.Num(), 
		[&Packets](int32 Index)
		{
			Packets[Index]->ComputeRelevance();
			Packets[Index]->MarkRelevant();
		}
	);

	for (auto Packet : Packets)
		Packet->RenderThreadFinalize();

	{
		// View.StaticMeshVisibilityMap (bit array) <- MarkMasks (byte array)
	}
}

struct ENGINE_API FMaterialRelevance {
	uint16 ShadingModelMask;
	uint8 bOpaque : 1;
	uint8 bMasked : 1;
	uint8 bDistortion : 1;
	uint8 bHairStrands : 1;
	uint8 bSeparateTranslucency : 1; // Translucency After DOF
	uint8 bSeparateTranslucencyModulate : 1;
	uint8 bNormalTranslucency : 1;
	uint8 bUsesSceneColorCopy : 1;
	uint8 bDisableOffscreenRendering : 1; // Blend Modulate
	uint8 bOutputsTranslucentVelocity : 1;
	uint8 bUsesGlobalDistanceField : 1;
	uint8 bUsesWorldPositionOffset : 1;
	uint8 bDecal : 1;
	uint8 bTranslucentSurfaceLighting : 1;
	uint8 bUsesSceneDepth : 1;
	uint8 bUsesSkyMaterial : 1;
	uint8 bUsesSingleLayerWaterMaterial : 1;
	uint8 bHasVolumeMaterialDomain : 1;
	uint8 bUsesCustomDepthStencil : 1;
	uint8 bUsesDistanceCullFade : 1;
	uint8 bDisableDepthTest : 1;
};
struct FPrimitiveViewRelevance : public FMaterialRelevance {
	uint32 bStaticRelevance : 1; 		/** The primitive's static elements are rendered for the view. */
	uint32 bDynamicRelevance : 1;		/** The primitive's dynamic elements are rendered for the view. */
	uint32 bDrawRelevance : 1;			/** The primitive is drawn. */
	uint32 bShadowRelevance : 1;		/** The primitive is casting a shadow. */
	uint32 bVelocityRelevance : 1;		/** The primitive should render velocity. */
	uint32 bRenderCustomDepth : 1;		/** The primitive should render to the custom depth pass. */
	uint32 bRenderInDepthPass : 1;		/** The primitive should render to the depth prepass even if it's not rendered in the main pass. */
	uint32 bRenderInMainPass : 1;		/** The primitive should render to the base pass / normal depth / velocity rendering. */
	uint32 bEditorPrimitiveRelevance : 1;				/** The primitive is drawn only in the editor and composited onto the scene after post processing */
	uint32 bEditorStaticSelectionRelevance : 1;			/** The primitive's static elements are selected and rendered again in the selection outline pass*/
	uint32 bEditorNoDepthTestPrimitiveRelevance : 1;	/** The primitive is drawn only in the editor and composited onto the scene after post processing using no depth testing */
	uint32 bHasSimpleLights : 1;		/** The primitive should have GatherSimpleLights called on the proxy when gathering simple lights. */
	uint32 bUsesLightingChannels : 1;	/** Whether the primitive uses non-default lighting channels. */
	uint32 bTranslucentSelfShadow : 1;	/** Whether the primitive has materials that use volumetric translucent self shadow. */
	uint32 bUseCustomViewData : 1; 		/** Whether the view use custom data. */
	uint32 bInitializedThisFrame : 1;


	FPrimitiveViewRelevance() {
		bOpaque = true;
		bRenderInMainPass = true;
		// else false;
	}
};

bool UStaticMeshComponent::HasStaticLighting()
{
	return Mobility == EComponentMobility::Static;
}
bool UStaticMeshComponent::HasValidSettingsForStaticLighting() const
{	
	int32 LightMapWidth = 0;
	int32 LightMapHeight = 0;
	GetLightMapResolution(LightMapWidth, LightMapHeight);

	return HasStaticLighting() && GetStaticMesh() && LightMapWidth>0 && LightMapHeight>0
		&& HasLightmapTextureCoordinates();
}
bool UStaticMeshComponent::HasLightmapTextureCoordinates() const
{ // when does it returns false?
	const UStaticMesh* Mesh = GetStaticMesh();
	return Mesh->LightMapCoordinateIndex  < 
		Mesh->RenderData->LODResources[0].VertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords();
}
FPrimitiveSceneProxy::FPrimitiveSceneProxy(const UPrimitiveComponent* InComponent, FName InResourceName)
:
, bStaticLighting(InComponent->HasStaticLighting())
, bHasValidSettingsForStaticLighting(InComponent->HasValidSettingsForStaticLighting(false))
{
}
FPrimitiveViewRelevance FStaticMeshSceneProxy::GetViewRelevance(const FSceneView* View) const
{   
	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance = IsShown(View) && View->Family->EngineShowFlags.StaticMeshes;
	Result.bRenderCustomDepth = ShouldRenderCustomDepth();
	Result.bRenderInMainPass = ShouldRenderInMainPass();
	Result.bUsesLightingChannels = GetLightingChannelMask() != GetDefaultLightingChannelMask();
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;

	if( bStaticLighting && !bHasValidSettingsForStaticLighting )
	{
		Result.bDynamicRelevance = true;
	}
	else
	{
		Result.bStaticRelevance = true;
		Result.bEditorStaticSelectionRelevance = (IsSelected() || IsHovered());
	}
	Result.bShadowRelevance = IsShadowCast(View);
	Result |= MaterialRelevance;
	Result.bVelocityRelevance = IsMovable() && Result.bOpaque && Result.bRenderInMainPass;

	return Result;
}

struct FRelevancePacket {
	//const float CurrentWorldTime;
	//const float DeltaWorldTime;

	FRHICommandListImmediate& RHICmdList;
	const FScene* Scene;
	const FViewInfo& View;
	const FViewCommands& ViewCommands;
	const FMarkRelevantStaticMeshesForViewData& ViewData;

	int32 NumVisibleDynamicPrimitives;  // per packet
	FPrimitiveViewMasks& OutHasDynamicMeshElementsMasks;  // not per packet, global

	int32 NumVisibleDynamicEditorPrimitives;  // per packet
	FPrimitiveViewMasks& OutHasDynamicEditorMeshElementsMasks;  // not per packet, global

	uint8* MarkMasks;	// For setting View.StaticMeshVisibilityMap

	FRelevancePrimSet<int32> Input;  // per packet
	FRelevancePrimSet<int32> RelevantStaticPrimitives;  // per packet
	FRelevancePrimSet<int32> NotDrawRelevant;
	//FRelevancePrimSet<int32> TranslucentSelfShadowPrimitives;
	FRelevancePrimSet<FPrimitiveSceneInfo*> VisibleDynamicPrimitivesWithSimpleLights;  // per packet
	
	//FMeshPassMask VisibleDynamicMeshesPassMask;
	//FTranslucenyPrimCount TranslucentPrimCount;
	bool bHasDistortionPrimitives;
	//bool bHasCustomDepthPrimitives;
	//FRelevancePrimSet<FPrimitiveSceneInfo*> LazyUpdatePrimitives;
	//FRelevancePrimSet<FPrimitiveSceneInfo*> DirtyIndirectLightingCacheBufferPrimitives;
	//FRelevancePrimSet<FPrimitiveSceneInfo*> RecachedReflectionCapturePrimitives;

	TArray<FMeshDecalBatch> MeshDecalBatches;
	TArray<FVolumetricMeshBatch> VolumetricMeshBatches;
	FDrawCommandRelevancePacket DrawCommandPacket;

	//FRelevancePrimSet<FPrimitiveLODMask> PrimitivesLODMask; // group both lod mask with primitive index to be able to properly merge them in the view

	//FRelevancePrimSet<FViewCustomData> PrimitivesCustomData; // group both custom data with primitive to be able to properly merge them in the view
	//FMemStackBase& PrimitiveCustomDataMemStack;
	//FPrimitiveViewMasks& OutHasViewCustomDataMasks;

	//uint16 CombinedShadingModelMask;
	bool bUsesGlobalDistanceField;
	bool bUsesLightingChannels;
	bool bTranslucentSurfaceLighting;
	bool bUsesSceneDepth;
	bool bUsesCustomDepthStencil;
	bool bSceneHasSkyMaterial;
	bool bHasSingleLayerWaterMaterial;
	bool bHasTranslucencySeparateModulation;
}

void FRelevancePacket::ComputeRelevance()
{
	for (int32 Index = 0; Index < Input.NumPrims; Index++)
	{
		int32 PrimIdx = Input.Prims[Index];
		FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimIdx];
		FPrimitiveViewRelevance& ViewRelevance = const_cast<FPrimitiveViewRelevance&>(View.PrimitiveViewRelevanceMap[PrimIdx]);
		ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(&View);
		ViewRelevance.bInitializedThisFrame = true;

		if (bStaticRelevance && (bDrawRelevance || bShadowRelevance)) {
			RelevantStaticPrimitives.AddPrim(PrimIdx);
		}
		
		if (!bDrawRelevance) {
			NotDrawRelevant.AddPrim(PrimIdx);
			continue;
		}

		if (bEditorRelevance) {
			++NumVisibleDynamicEditorPrimitives;
			OutHasDynamicEditorMeshElementsMasks[PrimIdx] |= ViewBit;
		}else if(bDynamicRelevance) {
			++NumVisibleDynamicPrimitives;
			OutHasDynamicMeshElementsMasks[PrimIdx] |= ViewBit;
			if (ViewRelevance.bHasSimpleLights) {
				VisibleDynamicPrimitivesWithSimpleLights.AddPrim(PrimitiveSceneInfo);
			}
		}

		//if (ViewRelevance.bUseCustomViewData) {				
		//	OutHasViewCustomDataMasks[PrimIdx] |= ViewBit;
		//}

		if (bTranslucentRelevance && !bEditorRelevance && ViewRelevance.bRenderInMainPass) {
			...
			if (ViewRelevance.bDistortion) {
				bHasDistortionPrimitives = true;
			}
		}

		//CombinedShadingModelMask |= ViewRelevance.ShadingModelMask;
		bUsesGlobalDistanceField |= ViewRelevance.bUsesGlobalDistanceField;
		bUsesLightingChannels |= ViewRelevance.bUsesLightingChannels;
		bTranslucentSurfaceLighting |= ViewRelevance.bTranslucentSurfaceLighting;
		bUsesSceneDepth |= ViewRelevance.bUsesSceneDepth;
		bUsesCustomDepthStencil |= ViewRelevance.bUsesCustomDepthStencil;
		bSceneHasSkyMaterial |= ViewRelevance.bUsesSkyMaterial;
		bHasSingleLayerWaterMaterial |= ViewRelevance.bUsesSingleLayerWaterMaterial;
		bHasTranslucencySeparateModulation |= ViewRelevance.bSeparateTranslucencyModulate;

		//if (ViewRelevance.bRenderCustomDepth) {
		//	bHasCustomDepthPrimitives = true;
		//}

		//if (GUseTranslucencyShadowDepths && ViewRelevance.bTranslucentSelfShadow) {
		//	TranslucentSelfShadowPrimitives.AddPrim(PrimIdx);
		//}

		if (PrimitiveSceneInfo->NeedsUniformBufferUpdate()) {
			LazyUpdatePrimitives.AddPrim(PrimitiveSceneInfo);
		}

		//if (PrimitiveSceneInfo->NeedsIndirectLightingCacheBufferUpdate()) {
		//	DirtyIndirectLightingCacheBufferPrimitives.AddPrim(PrimitiveSceneInfo);
		//}
	}
}

/* Output:
PrimitivesLODMask  // per packet
PrimitivesCustomData  // per packet
DrawCommandPacket  // per packet
VolumetricMeshBatches  // per packet
MeshDecalBatches  // per packet
MarkMasks  // not per packet, global
View.NumVisibleStaticMeshElements  // not per packet, global
View.StaticMeshBatchVisibility  // not per packet, global
*/
void FRelevancePacket::MarkRelevant()
{
	for (int32 i = 0; i < RelevantStaticPrimitives.NumPrims; ++i)
	{
		int32 PrimitiveIndex = RelevantStaticPrimitives.Prims[i];
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveIndex];

		// FLODMask LODToRender = ComputeLODForMeshes(PrimitiveSceneInfo->StaticMeshRelevances, View, Bounds.BoxSphereBounds.Origin, Bounds.BoxSphereBounds.SphereRadius, ViewData.ForcedLODLevel, MeshScreenSizeSquared, CurFirstLODIdx, ViewData.LODScale);
		// PrimitivesLODMask.AddPrim(FRelevancePacket::FPrimitiveLODMask(PrimitiveIndex, LODToRender));

		// void* UserViewCustomData = nullptr;
		// if (OutHasViewCustomDataMasks[PrimitiveIndex] != 0) // Has a relevance for this view
		// {
		// 	UserViewCustomData = PrimitiveSceneInfo->Proxy->InitViewCustomData(View, View.LODDistanceFactor, PrimitiveCustomDataMemStack, true, false, &LODToRender, MeshScreenSizeSquared);
		// 	if (UserViewCustomData != nullptr)
		// 	{
		// 		PrimitivesCustomData.AddPrim(FViewCustomData(PrimitiveSceneInfo, UserViewCustomData));
		// 	}
		// }

		const int32 NumStaticMeshes = PrimitiveSceneInfo->StaticMeshRelevances.Num();
		for(int32 MeshIndex = 0; MeshIndex < NumStaticMeshes;MeshIndex++)
		{
			const FStaticMeshBatchRelevance& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
			const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];

			uint8 MarkMask = 0;
			// bool bNeedsBatchVisibility = false;

			const bool bCanCache = !bIsPrimitiveDistanceCullFading && !bIsMeshDitheringLOD;

			if (ViewRelevance.bDrawRelevance)
			{
				if ((StaticMeshRelevance.bUseForMaterial || StaticMeshRelevance.bUseAsOccluder)
					&& (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderInDepthPass || ViewRelevance.bRenderCustomDepth))
				{
					if (StaticMeshRelevance.bUseForDepthPass && bDrawDepthOnly)
					{
						DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::DepthPass);
					}

					if (StaticMeshRelevance.bUseForMaterial && (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth))
					{
						++NumVisibleStaticMeshElements;
						MarkMask |= EMarkMaskBits::StaticMeshVisibilityMapMask;
						
						DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::BasePass);

						if(StaticMeshRelevance.bUseSkyMaterial)
						{
							DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::SkyPass);
						}
					}
				}

				if (StaticMeshRelevance.bUseForMaterial
					&& ViewRelevance.HasTranslucency()
					&& !ViewRelevance.bEditorPrimitiveRelevance
					&& ViewRelevance.bRenderInMainPass)
				{
					// 
				}

				if (ViewRelevance.bEditorStaticSelectionRelevance)
				{
					DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::EditorSelection);
				}

				if (ViewRelevance.bHasVolumeMaterialDomain)
				{
					VolumetricMeshBatches.Add( {&StaticMesh, PrimitiveSceneInfo->Proxy} )
				}

				if (ViewRelevance.bRenderInMainPass && ViewRelevance.bDecal)
				{
					MeshDecalBatches.Add( {&StaticMesh, PrimitiveSceneInfo->Proxy, PrimitiveSceneInfo->Proxy->GetTranslucencySortPriority()} );
				}
			}

			if (MarkMask)
			{
				MarkMasks[StaticMeshRelevance.Id] = MarkMask; // StaticMeshRelevance.Id == StaticMesh.Id
			}

			if (StaticMeshRelevance.bRequiresPerElementVisibility)
			{
				WriteView.StaticMeshBatchVisibility[StaticMesh.BatchVisibilityId] = StaticMesh.VertexFactory->GetStaticBatchElementVisibility(View, &StaticMesh, UserViewCustomData);
			}
		}
	}

	FPlatformAtomics::InterlockedAdd(
		&WriteView.NumVisibleStaticMeshElements, NumVisibleStaticMeshElements);
}

class FSceneRenderer{	
	FMeshElementCollector MeshCollector;
}

class FMeshElementCollector {
	TChunkedArray<FMeshBatch> MeshBatchStorage;
	
	FSceneView* Views[NumViews];
	FSimpleElementCollector* SimpleElementCollectors[NumViews];/** PDIs */

	TArray<FMeshBatchAndRelevance>* MeshBatches[NumViews];/** Meshes to render */
	TArray<FPrimitiveUniformShaderParameters>* DynamicPrimitiveShaderDataPerView[NumViews];/** Tracks dynamic primitive data for upload to GPU Scene for every view, when enabled. */
	int32 NumMeshBatchElementsPerView[NumViews];

	const FPrimitiveSceneProxy* PrimitiveSceneProxy;/** Current primitive being gathered. */
	uint16 MeshIdInPrimitivePerView[NumViews];/** Current Mesh Id In a Primitive per view */

	//TArray<FMaterialRenderProxy*> TemporaryProxies;/** Material proxies that will be deleted at the end of the frame. */
	//TArray<FOneFrameResource*> OneFrameResources;/** Resources that will be deleted at the end of the frame. */

	/** Dynamic buffer pools. */
	FGlobalDynamicIndexBuffer* DynamicIndexBuffer;
	FGlobalDynamicVertexBuffer* DynamicVertexBuffer;
	FGlobalDynamicReadBuffer* DynamicReadBuffer;

	ERHIFeatureLevel::Type FeatureLevel;
	
	const bool bUseAsyncTasks = false;

	TArray<TFunction<void()>*> ParallelTasks;/** Tasks to wait for at the end of gathering dynamic mesh elements. */
};

class ENGINE_API FSimpleElementCollector : public FPrimitiveDrawInterface {/** Primitive draw interface implementation used to store primitives requested to be drawn when gathering dynamic mesh elements. */
	FHitProxyId HitProxyId;
	uint16 PrimitiveMeshId;
	bool bIsMobileHDR;
	TArray<FDynamicPrimitiveResource*,SceneRenderingAllocator> DynamicResources;/** The dynamic resources which have been registered with this drawer. */
public:
	FBatchedElements BatchedElements;
	FBatchedElements TopBatchedElements;
};
class FViewInfo : public FSceneView {
	TArray<FMeshBatchAndRelevance> DynamicMeshElements;/** Gathered in initviews from all the primitives with dynamic view relevance, used in each mesh pass. */
	TArray<uint32, SceneRenderingAllocator> DynamicMeshEndIndices;/* [PrimitiveIndex] = end index index in DynamicMeshElements[], to support GetDynamicMeshElementRange(). Contains valid values only for visible primitives with bDynamicRelevance. */
	TArray<FMeshPassMask, SceneRenderingAllocator> DynamicMeshElementsPassRelevance;/* Mesh pass relevance for gathered dynamic mesh elements. */
	
	FSimpleElementCollector SimpleElementCollector;
	
	TArray<FPrimitiveUniformShaderParameters> DynamicPrimitiveShaderData;/** Tracks dynamic primitive data for upload to GPU Scene, when enabled. */
}

void FSceneRenderer::GatherDynamicMeshElements(
	TArray<FViewInfo>& InViews, 
	const FScene* InScene, 
	const FSceneViewFamily& InViewFamily, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
	FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
	FGlobalDynamicReadBuffer& DynamicReadBuffer,
	const FPrimitiveViewMasks& HasDynamicMeshElementsMasks, 
	const FPrimitiveViewMasks& HasDynamicEditorMeshElementsMasks, 
	const FPrimitiveViewMasks& HasViewCustomDataMasks,
	FMeshElementCollector& Collector)
{
	Collector.ClearViewMeshArrays();

	for (int32 i = 0; i < InViews.Num(); i++)
	{
		Collector.DynamicIndexBuffer = &DynamicIndexBuffer;
		Collector.DynamicVertexBuffer = &DynamicVertexBuffer;
		Collector.DynamicReadBuffer = &DynamicReadBuffer;

		Collector.Views.Add( & InViews[i] );
		Collector.MeshIdInPrimitivePerView.Add(0);
		Collector.NumMeshBatchElementsPerView.Add(0);
		Collector.SimpleElementCollectors.Add( & InViews[i].SimpleElementCollector );
		Collector.MeshBatches.Add( & InViews[i].DynamicMeshElements );
		Collector.DynamicPrimitiveShaderDataPerView.Add( & InViews[i].DynamicPrimitiveShaderData );
	}

	for (int32 PrimitiveIndex = 0; PrimitiveIndex < NumPrimitives; ++PrimitiveIndex)
	{
		uint8 ViewMask = HasDynamicMeshElementsMasks[PrimitiveIndex];

		if(ViewMask == 0)
			continue;

		FPrimitiveSceneInfo* PrimitiveSceneInfo = InScene->Primitives[PrimitiveIndex];
		Collector.SetPrimitive(PrimitiveSceneInfo->Proxy, PrimitiveSceneInfo->DefaultDynamicHitProxyId);

		// SetDynamicMeshElementViewCustomData(InViews, HasViewCustomDataMasks, PrimitiveSceneInfo);

		if (PrimitiveIndex > 0)
		{
			for (int32 ViewIndex = 0; ViewIndex < ViewCount; ViewIndex++)
			{
				InViews[ViewIndex].DynamicMeshEndIndices[PrimitiveIndex - 1] = Collector.GetMeshBatchCount(ViewIndex);
			}
		}

		PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(InViewFamily.Views, InViewFamily, ViewMaskFinal, Collector);

		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ViewIndex++)
		{
			InViews[ViewIndex].DynamicMeshEndIndices[PrimitiveIndex] = Collector.GetMeshBatchCount(ViewIndex);
		}

		// Compute DynamicMeshElementsMeshPassRelevance for this primitive.
		for (int32 ViewIndex = 0; ViewIndex < ViewCount; ViewIndex++)
		{
			if (ViewMask & (1 << ViewIndex))
			{
				FViewInfo& View = InViews[ViewIndex];
				const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveIndex];

				const int32 LastNumDynamicMeshElements = View.DynamicMeshElementsPassRelevance.Num();
				View.DynamicMeshElementsPassRelevance.SetNum(View.DynamicMeshElements.Num());

				for (int32 ElementIndex = LastNumDynamicMeshElements; ElementIndex < View.DynamicMeshElements.Num(); ++ElementIndex)
				{
					const FMeshBatchAndRelevance& MeshBatch = View.DynamicMeshElements[ElementIndex];
					FMeshPassMask& PassRelevance = View.DynamicMeshElementsPassRelevance[ElementIndex];

					ComputeDynamicMeshRelevance(, , ViewRelevance, MeshBatch, View, PassRelevance, , );
				}
			}
		}
	}

	if (GIsEditor)
	{
		...
	}

	MeshCollector.ProcessTasks();
}

void ComputeDynamicMeshRelevance(, , 
	const FPrimitiveViewRelevance& ViewRelevance, 
	const FMeshBatchAndRelevance& MeshBatch, 
	FViewInfo& View, 
	FMeshPassMask& PassMask, , )
{
	const int32 NumElements = MeshBatch.Mesh->Elements.Num();

	if (ViewRelevance.bDrawRelevance && (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth || ViewRelevance.bRenderInDepthPass))
	{
		PassMask.Set(EMeshPass::DepthPass);
		View.NumVisibleDynamicMeshElements[EMeshPass::DepthPass] += NumElements;

		if (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth)
		{
			PassMask.Set(EMeshPass::BasePass);
			View.NumVisibleDynamicMeshElements[EMeshPass::BasePass] += NumElements;
			
			if (ViewRelevance.bRenderCustomDepth)
			{
				PassMask.Set(EMeshPass::CustomDepth);
				View.NumVisibleDynamicMeshElements[EMeshPass::CustomDepth] += NumElements;
			}

			if (View.bAllowTranslucentPrimitivesInHitProxy)
			{
				PassMask.Set(EMeshPass::HitProxy);
				View.NumVisibleDynamicMeshElements[EMeshPass::HitProxy] += NumElements;
			}else{
				PassMask.Set(EMeshPass::HitProxyOpaqueOnly);
				View.NumVisibleDynamicMeshElements[EMeshPass::HitProxyOpaqueOnly] += NumElements;
			}
		}
	}

	if (ViewRelevance.HasTranslucency() && !ViewRelevance.bEditorPrimitiveRelevance && ViewRelevance.bRenderInMainPass)
	{
		if (View.Family->AllowTranslucencyAfterDOF())
		{
			if (ViewRelevance.bNormalTranslucency)
			{
				PassMask.Set(EMeshPass::TranslucencyStandard);
				View.NumVisibleDynamicMeshElements[EMeshPass::TranslucencyStandard] += NumElements;
			}

			if (ViewRelevance.bSeparateTranslucency)
			{
				PassMask.Set(EMeshPass::TranslucencyAfterDOF);
				View.NumVisibleDynamicMeshElements[EMeshPass::TranslucencyAfterDOF] += NumElements;
			}

			if (ViewRelevance.bSeparateTranslucencyModulate)
			{
				PassMask.Set(EMeshPass::TranslucencyAfterDOFModulate);
				View.NumVisibleDynamicMeshElements[EMeshPass::TranslucencyAfterDOFModulate] += NumElements;
			}
		}else{
			PassMask.Set(EMeshPass::TranslucencyAll);
			View.NumVisibleDynamicMeshElements[EMeshPass::TranslucencyAll] += NumElements;
		}

		if (ViewRelevance.bDistortion)
		{
			PassMask.Set(EMeshPass::Distortion);
			View.NumVisibleDynamicMeshElements[EMeshPass::Distortion] += NumElements;
		}
	}

	if (ViewRelevance.bDrawRelevance)
	{
		PassMask.Set(EMeshPass::EditorSelection);
		View.NumVisibleDynamicMeshElements[EMeshPass::EditorSelection] += NumElements;
	}

	if (ViewRelevance.bHasVolumeMaterialDomain)
	{
		View.VolumetricMeshBatches.AddUninitialized(1);
		FVolumetricMeshBatch& BatchAndProxy = View.VolumetricMeshBatches.Last();
		BatchAndProxy.Mesh = MeshBatch.Mesh;
		BatchAndProxy.Proxy = MeshBatch.PrimitiveSceneProxy;
	}

	if (ViewRelevance.bRenderInMainPass && ViewRelevance.bDecal)
	{
		View.MeshDecalBatches.AddUninitialized(1);
		FMeshDecalBatch& BatchAndProxy = View.MeshDecalBatches.Last();
		BatchAndProxy.Mesh = MeshBatch.Mesh;
		BatchAndProxy.Proxy = MeshBatch.PrimitiveSceneProxy;
		BatchAndProxy.SortKey = MeshBatch.PrimitiveSceneProxy->GetTranslucencySortPriority();
	}
}