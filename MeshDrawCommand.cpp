/* Cashed command for static:
Cashed -> 
FDrawCommandRelevancePacket::AddCommandsForMesh() ->
FRelevancePacket::RenderThreadFinalize() ->
FParallelMeshDrawCommandPass::DispatchPassSetup()
*/

FSceneRenderer::ComputeViewVisibility() -> FSceneRenderer::SetupMeshPass() ->
FParallelMeshDrawCommandPass::DispatchPassSetup() -> 
TGraphTask<FMeshDrawCommandPassSetupTask>::CreateTask() -> FMeshDrawCommandPassSetupTask::AnyThreadTask() ->
GenerateDynamicMeshDrawCommands() -> FMeshPassProcessor::AddMeshBatch()


class FMeshDrawCommand
{	
	FMeshDrawShaderBindings ShaderBindings;/** Resource bindings */
	FVertexInputStreamArray VertexStreams;/** Resource bindings */
	FRHIIndexBuffer* IndexBuffer;/** Resource bindings */

	FGraphicsMinimalPipelineStateId CachedPipelineId;/** PSO */

	uint32 FirstIndex;/** Draw command parameters */
	uint32 NumPrimitives;/** Draw command parameters */
	uint32 NumInstances;/** Draw command parameters */

	union {
		struct {
			uint32 BaseVertexIndex;
			uint32 NumVertices;
		} VertexParams;
		
		struct {
			FRHIVertexBuffer* Buffer;
			uint32 Offset;
		} IndirectArgs;
	};

	int8 PrimitiveIdStreamIndex; // <- MeshBatch.VertexFactory->GetPrimitiveIdStreamIndex(InputStreamType) <- FMeshPassProcessor::BuildMeshDrawCommands() <- GenerateDynamicMeshDrawCommands()
	uint8 StencilRef;/** Non-pipeline state */
};

class FVisibleMeshDrawCommand
{
	// Mesh Draw Command stored separately to avoid fetching its data during sorting
	const FMeshDrawCommand* MeshDrawCommand;

	FMeshDrawCommandSortKey SortKey; // <-FBasePassMeshProcessor::Process()
	int32 DrawPrimitiveId; // <- GetDrawCommandPrimitiveId() <- FMeshPassProcessor::BuildMeshDrawCommands() <- GenerateDynamicMeshDrawCommands()
	int32 ScenePrimitiveId; // <- GetDrawCommandPrimitiveId() <- FMeshPassProcessor::BuildMeshDrawCommands() <- GenerateDynamicMeshDrawCommands()

	int32 PrimitiveIdBufferOffset; // <- BuildMeshDrawCommandPrimitiveIdBuffer()
	int32 StateBucketId; // -1 <- FDynamicPassMeshDrawListContext::FinalizeCommand() <- FMeshPassProcessor::BuildMeshDrawCommands()
	
	ERasterizerFillMode MeshFillMode;
	ERasterizerCullMode MeshCullMode;
};

class FCachedMeshDrawCommandInfo {
	FMeshDrawCommandSortKey SortKey;
	int32 CommandIndex; // Stores the index into FScene::CachedDrawLists of the corresponding FMeshDrawCommand, or -1 if not stored there
	int32 StateBucketId; // Stores the index into FScene::CachedMeshDrawCommandStateBuckets of the corresponding FMeshDrawCommand, or -1 if not stored there
	EMeshPass::Type MeshPass;
	ERasterizerFillMode MeshFillMode;
	ERasterizerCullMode MeshCullMode;
};

class FPrimitiveSceneInfo : public FDeferredCleanupInterface {
	
	TArray<FStaticMeshBatch> StaticMeshes; /** The primitive's static meshes. */
	TArray<FStaticMeshBatchRelevance> StaticMeshRelevances; /** The primitive's static mesh relevances. Must be in sync with StaticMeshes. */
	TArray<FCachedMeshDrawCommandInfo> StaticMeshCommandInfos; // Set by FPrimitiveSceneInfo::CacheMeshDrawCommands()
}

template<class T>
struct FRelevancePrimSet {
	static const int MaxOutputPrims = 127;
	int32 NumPrims;
	T Prims[MaxOutputPrims];
}
struct FRelevancePacket {
	const FViewCommands& ViewCommands;
	FRelevancePrimSet<int32> Input;
	FRelevancePrimSet<int32> RelevantStaticPrimitives;
	FDrawCommandRelevancePacket DrawCommandPacket;
}

/* Output:
PrimitivesLODMask
PrimitivesCustomData
$$ DrawCommandPacket $$
VolumetricMeshBatches
MeshDecalBatches
MarkMasks
View.StaticMeshBatchVisibility
View.NumVisibleStaticMeshElements
*/
void FRelevancePacket::MarkRelevant()
{
	for (int32 StaticPrimIndex = 0, Num = RelevantStaticPrimitives.NumPrims; StaticPrimIndex < Num; ++StaticPrimIndex)
	{
		int32 PrimitiveIndex = RelevantStaticPrimitives.Prims[StaticPrimIndex];
		const FPrimitiveSceneInfo* PrimitiveSceneInfo = Scene->Primitives[PrimitiveIndex];
		const FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveIndex];

		const int32 NumStaticMeshes = PrimitiveSceneInfo->StaticMeshRelevances.Num();
		for(int32 MeshIndex = 0; MeshIndex < NumStaticMeshes;MeshIndex++)
		{
			const FStaticMeshBatchRelevance& StaticMeshRelevance = PrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
			const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];

			if (ViewRelevance.bDrawRelevance)
			{
				if(...)
				{
					if(StaticMeshRelevance.bUseForDepthPass && bDrawDepthOnly)
					{
						DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::DepthPass);
					}

					if (StaticMeshRelevance.bUseForMaterial && (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth))
					{
						DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::BasePass);

						if(StaticMeshRelevance.bUseSkyMaterial)
						{
							DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::SkyPass);
						}

						...
						
						if (StaticMeshRelevance.bSelectable)
						{
							if (View.bAllowTranslucentPrimitivesInHitProxy)
							{
								DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::HitProxy);
							}
							else
							{
								DrawCommandPacket.AddCommandsForMesh(, , , , , , EMeshPass::HitProxyOpaqueOnly);
							}
						}

						++NumVisibleStaticMeshElements;
					}

					bNeedsBatchVisibility = true;
				}
			}
		}
	}
}


class FStaticMeshBatchRelevance {
	FMeshPassMask CommandInfosMask; /* Every bit corresponds to one MeshPass. If bit is set, then FPrimitiveSceneInfo::CachedMeshDrawCommandInfos contains this mesh pass. */
	uint16 CommandInfosBase; /** Starting offset into continuous array of command infos for this mesh in FPrimitiveSceneInfo::StaticMeshCommandInfos. */
	int32 GetStaticMeshCommandInfoIndex(EMeshPass::Type MeshPass) /** Computes index of cached mesh draw command in FPrimitiveSceneInfo::StaticMeshCommandInfos, for a given mesh pass. */
	{
		if (!CommandInfosMask.Get(MeshPass))
			return -1;

		int32 offset = 0;
		for (int32 MeshPassIndex = 0; MeshPassIndex < MeshPass; ++MeshPassIndex)
		{
			offset += (int32) CommandInfosMask.Get( (EMeshPass::Type)MeshPassIndex );
		}
		return CommandInfosBase + offset;
	}
}
struct FDrawCommandRelevancePacket {
	TArray<FVisibleMeshDrawCommand>  VisibleCachedDrawCommands [EMeshPass::Num];
	TArray<const FStaticMeshBatch*>  DynamicBuildRequests [EMeshPass::Num];
	int32 NumDynamicBuildRequestElements [EMeshPass::Num];
	bool bUseCachedMeshDrawCommands;
}

using FStateBucketMap = Experimental::TRobinHoodHashMap<FMeshDrawCommand, FMeshDrawCommandCount, MeshDrawCommandKeyFuncs>;

class FCachedPassMeshDrawList {	
	TSparseArray<FMeshDrawCommand> MeshDrawCommands; /** Indices held by FStaticMeshBatch::CachedMeshDrawCommands must be stable */
	int32 LowestFreeIndexSearchStart;
};
class FScene : public FSceneInterface {
	/** Instancing state buckets.  These are stored on the scene as they are precomputed at FPrimitiveSceneInfo::AddToScene time. */
	//FCriticalSection CachedMeshDrawCommandLock [EMeshPass::Num];
	FStateBucketMap CachedMeshDrawCommandStateBuckets [EMeshPass::Num];
	FCachedPassMeshDrawList CachedDrawLists [EMeshPass::Num];
}

void FDrawCommandRelevancePacket::AddCommandsForMesh(
	int32 PrimitiveIndex, 
	const FPrimitiveSceneInfo* InPrimitiveSceneInfo,
	const FStaticMeshBatchRelevance& StaticMeshRelevance, 
	const FStaticMeshBatch& StaticMesh, 
	const FScene* Scene, 
	bool bCanCache, 
	EMeshPass::Type PassType)
{
	if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands)
		&& StaticMeshRelevance.bSupportsCachingMeshDrawCommands
		&& bCanCache)
	{
		const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(PassType);

		if (StaticMeshCommandInfoIndex >= 0)
		{
			const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = 
				InPrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];

			const FMeshDrawCommand* MeshDrawCommand = 
				CachedMeshDrawCommand.StateBucketId >= 0 ? 
				&Scene->CachedMeshDrawCommandStateBuckets[PassType] .GetByElementId(CachedMeshDrawCommand.StateBucketId).Key : 
				&Scene->CachedDrawLists[PassType] 					.MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

			VisibleCachedDrawCommands[(uint32)PassType].Add( 
				FVisibleMeshDrawCommand(
					MeshDrawCommand,
					PrimitiveIndex,
					PrimitiveIndex,
					CachedMeshDrawCommand.StateBucketId,
					CachedMeshDrawCommand.MeshFillMode,
					CachedMeshDrawCommand.MeshCullMode,
					CachedMeshDrawCommand.SortKey
				) 
			);
		}
	}
	else
	{
		NumDynamicBuildRequestElements[PassType] += StaticMeshRelevance.NumElements;
		DynamicBuildRequests[PassType].Add(&StaticMesh);
	}
}


typedef TArray<FVisibleMeshDrawCommand> FMeshCommandOneFrameArray;

class FViewCommands {
	FMeshCommandOneFrameArray MeshCommands [EMeshPass::Num];
	TArray<const FStaticMeshBatch*> DynamicMeshCommandBuildRequests [EMeshPass::Num];
	int32 NumDynamicMeshCommandBuildRequestElements [EMeshPass::Num];
};
typedef TArray<FViewCommands> FViewVisibleCommandsPerView;

bool FDeferredShadingSceneRenderer::InitViews()
{
	FViewVisibleCommandsPerView ViewCommandsPerView;
	ViewCommandsPerView.SetNum(Views.Num());

	ComputeViewVisibility(, , ViewCommandsPerView, , , )
	{
		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			FViewCommands& ViewCommands = ViewCommandsPerView[ViewIndex];
			ComputeAndMarkRelevanceForViewParallel(, , , ViewCommands, , , , )
			{
				TArray<FRelevancePacket*> Packets;
				...

				ParallelFor(Packets.Num(), 
					[&Packets](int32 Index)
					{
						Packets[Index]->ComputeRelevance();
						Packets[Index]->MarkRelevant();
					},
					!WillExecuteInParallel
				);

				for (auto Packet : Packets)
				{
					Packet->RenderThreadFinalize();
					{
						// Copy FRelevancePacket::DrawCommandPacket to InitViews()::ViewCommands..
					}
				}
			}
		}

		GatherDynamicMeshElements(Views, Scene, ViewFamily, DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer,
			HasDynamicMeshElementsMasks, HasDynamicEditorMeshElementsMasks, HasViewCustomDataMasks, MeshCollector);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
		{
			FViewCommands& ViewCommands = ViewCommandsPerView[ViewIndex];
			SetupMeshPass(Views[ViewIndex], BasePassDepthStencilAccess, ViewCommands);
		}
	}
}


FMeshPassProcessor* CreateBasePassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
	DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);
	
	SetupBasePassState(Scene->DefaultBasePassDepthStencilAccess, false, DrawRenderState);

	const FBasePassMeshProcessor::EFlags Flags = FBasePassMeshProcessor::EFlags::CanUseDepthStencil;

	return new(FMemStack::Get()) FBasePassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext, Flags);
}
FMeshPassProcessor* CreateSkyPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	FMeshPassProcessorRenderState DrawRenderState(Scene->UniformBuffers.ViewUniformBuffer, Scene->UniformBuffers.OpaqueBasePassUniformBuffer);
	DrawRenderState.SetInstancedViewUniformBuffer(Scene->UniformBuffers.InstancedViewUniformBuffer);

	FExclusiveDepthStencil::Type BasePassDepthStencilAccess_NoDepthWrite = FExclusiveDepthStencil::Type(Scene->DefaultBasePassDepthStencilAccess & ~FExclusiveDepthStencil::DepthWrite);
	SetupBasePassState(BasePassDepthStencilAccess_NoDepthWrite, false, DrawRenderState);

	return new(FMemStack::Get()) FSkyPassMeshProcessor(Scene, InViewIfDynamicMeshCommand, DrawRenderState, InDrawListContext);
}


enum class EMeshPassFlags {
	None = 0,
	CachedMeshCommands = 1 << 0,
	MainView = 1 << 1
};
typedef FMeshPassProcessor* (*PassProcessorCreateFunction) (const FScene* , const FSceneView* , FMeshPassDrawListContext* );
class FPassProcessorManager {
	RENDERER_API static PassProcessorCreateFunction JumpTable[(uint32)EShadingPath::Num][EMeshPass::Num];
	RENDERER_API static EMeshPassFlags Flags[(uint32)EShadingPath::Num][EMeshPass::Num];
};
FRegisterPassProcessorCreateFunction RegisterBasePass(&CreateBasePassProcessor, EShadingPath::Deferred, EMeshPass::BasePass, EMeshPassFlags::CachedMeshCommands | EMeshPassFlags::MainView);
FRegisterPassProcessorCreateFunction RegisterSkyPass(&CreateSkyPassProcessor, EShadingPath::Deferred, EMeshPass::SkyPass, EMeshPassFlags::MainView);

EMeshPassFlags FPassProcessorManager::Flags[1][EMeshPass::Num] = 
{
	CachedMeshCommands | MainView,	// DepthPass
	CachedMeshCommands | MainView,	// BasePass
	MainView,						// SkyPass
	MainView,						// SingleLayerWaterPass
	CachedMeshCommands,				// CSMShadowDepth
	MainView,						// Distortion
	CachedMeshCommands | MainView, 	// Velocity
	CachedMeshCommands | MainView,	// TranslucentVelocity
	MainView,						// TranslucencyStandard, 
	MainView,						// TranslucencyAfterDOF, TranslucencyAfterDOFModulate, TranslucencyAll
	MainView,						// LightmapDensity, DebugViewMode, CustomDepth
	None,							// MobileBasePassCSM
	None,							// MobileInverseOpacity
	CachedMeshCommands 				// VirtualTexture
	CachedMeshCommands | MainView,	// HitProxy
	CachedMeshCommands | MainView,	// HitProxyOpaqueOnly
	MainView						// EditorSelection
}


void FSceneRenderer::SetupMeshPass(FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewCommands& ViewCommands)
{
	for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; PassIndex++)
	{
		const EMeshPass::Type PassType = (EMeshPass::Type)PassIndex;

		if (FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::MainView)
		{
			PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
			FMeshPassProcessor* MeshPassProcessor = CreateFunction(Scene, &View, nullptr);

			FParallelMeshDrawCommandPass& Pass = View.ParallelMeshDrawCommandPasses[PassIndex];

			Pass.DispatchPassSetup(Scene, View, PassType, BasePassDepthStencilAccess, MeshPassProcessor,
				View.DynamicMeshElements,
				&View.DynamicMeshElementsPassRelevance,
				View.NumVisibleDynamicMeshElements[PassType],
				ViewCommands.DynamicMeshCommandBuildRequests[PassType],
				ViewCommands.NumDynamicMeshCommandBuildRequestElements[PassType],
				ViewCommands.MeshCommands[PassIndex]);
		}
	}
}


class FMeshDrawCommandPassSetupTaskContext {
	EMeshPass::Type PassType;
	FMeshPassProcessor* MeshPassProcessor;

	TArray<const FStaticMeshBatch*> DynamicMeshCommandBuildRequests;
	int32 NumDynamicMeshCommandBuildRequestElements;
	FMeshCommandOneFrameArray MeshDrawCommands;

	const TArray<FMeshBatchAndRelevance>* DynamicMeshElements;
	const TArray<FMeshPassMask>* DynamicMeshElementsPassRelevance;
	int32 NumDynamicMeshElements;
}
class FParallelMeshDrawCommandPass {
	FMeshDrawCommandPassSetupTaskContext TaskContext;
	//FPrimitiveIdVertexBufferPoolEntry PrimitiveIdVertexBufferPoolEntry;
	//FGraphEventRef TaskEventRef;
	//FString PassNameForStats;
	//mutable bool bPrimitiveIdBufferDataOwnedByRHIThread;
	int32 MaxNumDraws;
};

void FParallelMeshDrawCommandPass::DispatchPassSetup(
	FScene* Scene,
	const FViewInfo& View,
	EMeshPass::Type PassType,
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess,
	FMeshPassProcessor* MeshPassProcessor,
	const TArray<FMeshBatchAndRelevance>& DynamicMeshElements,
	const TArray<FMeshPassMask>* DynamicMeshElementsPassRelevance,
	int32 NumDynamicMeshElements,
	TArray<const FStaticMeshBatch*>& InOutDynamicMeshCommandBuildRequests,
	int32 NumDynamicMeshCommandBuildRequestElements,
	FMeshCommandOneFrameArray& InOutMeshDrawCommands)
{
	MaxNumDraws = InOutMeshDrawCommands.Num() + NumDynamicMeshElements + NumDynamicMeshCommandBuildRequestElements;

	TaskContext.MeshPassProcessor = MeshPassProcessor;
	TaskContext.DynamicMeshElements = &DynamicMeshElements;
	TaskContext.DynamicMeshElementsPassRelevance = DynamicMeshElementsPassRelevance;
	TaskContext.View = &View;
	TaskContext.PassType = PassType;
	TaskContext.bReverseCulling = View.bReverseCulling;
	TaskContext.bRenderSceneTwoSided = View.bRenderSceneTwoSided;
	TaskContext.BasePassDepthStencilAccess = BasePassDepthStencilAccess;
	TaskContext.DefaultBasePassDepthStencilAccess = Scene->DefaultBasePassDepthStencilAccess;
	TaskContext.NumDynamicMeshElements = NumDynamicMeshElements;
	TaskContext.NumDynamicMeshCommandBuildRequestElements = NumDynamicMeshCommandBuildRequestElements;

	// Setup translucency sort key update pass based on view.
	TaskContext.TranslucentSortPolicy = View.TranslucentSortPolicy;
	switch (PassType)
	{
		case EMeshPass::TranslucencyStandard:			TaskContext.TranslucencyPass = ETranslucencyPass::TPT_StandardTranslucency; break;
		case EMeshPass::TranslucencyAfterDOF: 			TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDOF; break;
		case EMeshPass::TranslucencyAfterDOFModulate:	TaskContext.TranslucencyPass = ETranslucencyPass::TPT_TranslucencyAfterDOFModulate; break;
		case EMeshPass::TranslucencyAll: 				TaskContext.TranslucencyPass = ETranslucencyPass::TPT_AllTranslucency; break;
		case EMeshPass::MobileInverseOpacity: 			TaskContext.TranslucencyPass = ETranslucencyPass::TPT_StandardTranslucency; break;
		default:										TaskContext.TranslucencyPass = ETranslucencyPass::TPT_MAX;
	}

	// Copy InitViews()::ViewCommands to FMeshDrawCommandPassSetupTaskContext::MeshDrawCommands..
	FMemory::Memswap(&TaskContext.MeshDrawCommands, &InOutMeshDrawCommands, sizeof(InOutMeshDrawCommands));
	FMemory::Memswap(&TaskContext.DynamicMeshCommandBuildRequests, &InOutDynamicMeshCommandBuildRequests, sizeof(InOutDynamicMeshCommandBuildRequests));

	if (MaxNumDraws > 0)
	{
		// Preallocate resources on rendering thread based on MaxNumDraws.
		bPrimitiveIdBufferDataOwnedByRHIThread = false;
		TaskContext.PrimitiveIdBufferDataSize = TaskContext.InstanceFactor * MaxNumDraws * sizeof(int32);
		TaskContext.PrimitiveIdBufferData = FMemory::Malloc(TaskContext.PrimitiveIdBufferDataSize);
		PrimitiveIdVertexBufferPoolEntry = GPrimitiveIdVertexBufferPool.Allocate(TaskContext.PrimitiveIdBufferDataSize);
		TaskContext.MeshDrawCommands.Reserve(MaxNumDraws);
		TaskContext.TempVisibleMeshDrawCommands.Reserve(MaxNumDraws);

		const bool bExecuteInParallel = FApp::ShouldUseThreadingForPerformance()
			&& CVarMeshDrawCommandsParallelPassSetup.GetValueOnRenderThread() > 0
			&& GRenderingThread; // Rendering thread is required to safely use rendering resources in parallel.

		if (bExecuteInParallel)
		{
			FGraphEventArray DependentGraphEvents;
			DependentGraphEvents.Add(TGraphTask<FMeshDrawCommandPassSetupTask>::CreateTask(nullptr, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(TaskContext));
			TaskEventRef = TGraphTask<FMeshDrawCommandInitResourcesTask>::CreateTask(&DependentGraphEvents, ENamedThreads::GetRenderThread()).ConstructAndDispatchWhenReady(TaskContext);
		}
		else
		{
			QUICK_SCOPE_CYCLE_COUNTER(STAT_MeshPassSetupImmediate);
			FMeshDrawCommandPassSetupTask Task(TaskContext);
			Task.AnyThreadTask();
			FMeshDrawCommandInitResourcesTask DependentTask(TaskContext);
			DependentTask.AnyThreadTask();
		}
	}
}


void FMeshDrawCommandPassSetupTask::AnyThreadTask()
{
	GenerateDynamicMeshDrawCommands( , , , ,
		*Context.DynamicMeshElements,				// View.DynamicMeshElements
		Context.DynamicMeshElementsPassRelevance,	// View.DynamicMeshElementsPassRelevance
		Context.NumDynamicMeshElements,				// View.NumVisibleDynamicMeshElements[PassType]
		
		Context.DynamicMeshCommandBuildRequests,  			// ViewCommands.DynamicMeshCommandBuildRequests[PassType]
		Context.NumDynamicMeshCommandBuildRequestElements, 	// ViewCommands.NumDynamicMeshCommandBuildRequestElements[PassType]
		Context.MeshDrawCommands,  							// ViewCommands.MeshCommands[PassType]
		
		Context.MeshDrawCommandStorage,
		Context.MinimalPipelineStatePassSet,
		Context.NeedsShaderInitialisation
	);

	ApplyViewOverridesToMeshDrawCommands();
	Context.MeshDrawCommands.Sort(FCompareFMeshDrawCommands());
	BuildMeshDrawCommandPrimitiveIdBuffer();
}

class FDynamicMeshDrawCommandStorage { /** Storage for Mesh Draw Commands built every frame. */
	TChunkedArray<FMeshDrawCommand> MeshDrawCommands; // Using TChunkedArray to support growing without moving FMeshDrawCommand, since FVisibleMeshDrawCommand stores a pointer to these
};

class FMeshDrawCommandPassSetupTaskContext {
	FDynamicMeshDrawCommandStorage MeshDrawCommandStorage;
	FGraphicsMinimalPipelineStateSet MinimalPipelineStatePassSet;
	bool NeedsShaderInitialisation;
}

class FDynamicPassMeshDrawListContext : public FMeshPassDrawListContext {
	FDynamicMeshDrawCommandStorage& DrawListStorage;
	FMeshCommandOneFrameArray& DrawList;
	FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet;
	bool& NeedsShaderInitialisation;
}

/* Output:
VisibleCommands
MeshDrawCommandStorage
MinimalPipelineStatePassSet
NeedsShaderInitialisation
*/
void GenerateDynamicMeshDrawCommands( , , , ,
	const TArray<FMeshBatchAndRelevance>& DynamicMeshElements,
	const TArray<FMeshPassMask>* DynamicMeshElementsPassRelevance,
	//int32 MaxNumDynamicMeshElements,

	const TArray<const FStaticMeshBatch*>& DynamicMeshCommandBuildRequests,
	//int32 MaxNumBuildRequestElements,
	FMeshCommandOneFrameArray& VisibleCommands,
	
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	FGraphicsMinimalPipelineStateSet& MinimalPipelineStatePassSet,
	bool& NeedsShaderInitialisation)
{
	FDynamicPassMeshDrawListContext DynamicPassMeshDrawListContext(
		MeshDrawCommandStorage,
		VisibleCommands,
		MinimalPipelineStatePassSet,
		NeedsShaderInitialisation
	);
	PassMeshProcessor->SetDrawListContext(&DynamicPassMeshDrawListContext);

	for (int32 MeshIndex = 0; MeshIndex < DynamicMeshElements.Num(); MeshIndex++)
	{
		if ( (*DynamicMeshElementsPassRelevance)[MeshIndex].Get(PassType) ) 
		{
			const FMeshBatchAndRelevance& MeshAndRelevance = DynamicMeshElements[MeshIndex];
			const uint64 BatchElementMask = ~0ull;

			PassMeshProcessor->AddMeshBatch(*MeshAndRelevance.Mesh, BatchElementMask, MeshAndRelevance.PrimitiveSceneProxy);
		}
	}

	for (int32 MeshIndex = 0; MeshIndex < DynamicMeshCommandBuildRequests.Num(); MeshIndex++)
	{
		const FStaticMeshBatch* StaticMeshBatch = DynamicMeshCommandBuildRequests[MeshIndex];
		const uint64 BatchElementMask = !StaticMeshBatch->bRequiresPerElementVisibility ? ~0ull : View.StaticMeshBatchVisibility[StaticMeshBatch->BatchVisibilityId];

		PassMeshProcessor->AddMeshBatch(*StaticMeshBatch, BatchElementMask, StaticMeshBatch->PrimitiveSceneInfo->Proxy, StaticMeshBatch->Id);
	}
}


/* Cached DrawCommand VS Dynamic DrawCommand */
void FPrimitiveSceneInfo::CacheMeshDrawCommands(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos)
{
	PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
	FMeshPassProcessor* PassMeshProcessor = CreateFunction(Scene, /*ViewIfDynamicMeshCommand*/nullptr, &CachedPassMeshDrawListContext);
	
	PassMeshProcessor->AddMeshBatch(Mesh, uint64(-1), SceneInfo->Proxy);
}
void FSceneRenderer::SetupMeshPass(FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, FViewCommands& ViewCommands)
{
	PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
	FMeshPassProcessor* MeshPassProcessor = CreateFunction(Scene, /*ViewIfDynamicMeshCommand*/&View, nullptr);

	Pass.DispatchPassSetup(,,,,MeshPassProcessor,,,,,,);
}

/* Output:
Context.MeshDrawCommands
FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage
Context.PrimitiveIdBufferData
*/
BuildMeshDrawCommandPrimitiveIdBuffer(
	Context.bDynamicInstancing, // in
	Context.MeshDrawCommands, // inout
	Context.MeshDrawCommandStorage, // inout
	Context.PrimitiveIdBufferData, // out
	Context.PrimitiveIdBufferDataSize, // in
	Context.TempVisibleMeshDrawCommands,
	//Context.MaxInstances, // for logging
	//Context.VisibleMeshDrawCommandsNum, // for logging
	//Context.NewPassVisibleMeshDrawCommandsNum, // for logging
	//Context.ShaderPlatform, // in
	//Context.InstanceFactor // in
	);

void BuildMeshDrawCommandPrimitiveIdBuffer(
	bool bDynamicInstancing,
	FMeshCommandOneFrameArray& VisibleMeshDrawCommands,
	FDynamicMeshDrawCommandStorage& MeshDrawCommandStorage,
	void* PrimitiveIdData,
	int32 PrimitiveIdDataSize,
	FMeshCommandOneFrameArray& TempVisibleMeshDrawCommands,
	,,,
	EShaderPlatform ShaderPlatform,
	uint32 InstanceFactor = 1)
{
	uint32 PrimitiveIdIndex = 0;
	int32* PrimitiveIds = (int32*) PrimitiveIdData;
	
	if (bDynamicInstancing)
	{
		int32 CurrentStateBucketId = -1;
		uint32* CurrentDynamicallyInstancedMeshCommandNumInstances = nullptr;
		
		for (int32 i = 0; i < VisibleMeshDrawCommands.Num(); i++)
		{
			const FVisibleMeshDrawCommand& VisibleMeshDrawCommand = VisibleMeshDrawCommands[i];

			if (VisibleMeshDrawCommand.StateBucketId == CurrentStateBucketId && VisibleMeshDrawCommand.StateBucketId != -1)
			{
				if (CurrentDynamicallyInstancedMeshCommandNumInstances)
				{
					const int32 CurrentNumInstances = *CurrentDynamicallyInstancedMeshCommandNumInstances;
					*CurrentDynamicallyInstancedMeshCommandNumInstances = CurrentNumInstances + 1;
				}
				else
				{
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
				}
			}
			else
			{
				// First time state bucket setup
				CurrentStateBucketId = VisibleMeshDrawCommand.StateBucketId;

				if (VisibleMeshDrawCommand.MeshDrawCommand->PrimitiveIdStreamIndex >= 0
					&& VisibleMeshDrawCommand.MeshDrawCommand->NumInstances == 1
					&& i + 1 < VisibleMeshDrawCommands.Num()
					&& CurrentStateBucketId == VisibleMeshDrawCommands[i + 1].StateBucketId)
				{
					const int32 Index = MeshDrawCommandStorage.MeshDrawCommands.AddElement(*VisibleMeshDrawCommand.MeshDrawCommand);
					FMeshDrawCommand& NewCommand = MeshDrawCommandStorage.MeshDrawCommands[Index];
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

					NewVisibleMeshDrawCommand.Setup(
						&NewCommand,
						VisibleMeshDrawCommand.DrawPrimitiveId,
						VisibleMeshDrawCommand.ScenePrimitiveId,
						VisibleMeshDrawCommand.StateBucketId,
						VisibleMeshDrawCommand.MeshFillMode,
						VisibleMeshDrawCommand.MeshCullMode,
						VisibleMeshDrawCommand.SortKey);

					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));

					CurrentDynamicallyInstancedMeshCommandNumInstances = &NewCommand.NumInstances;
				}
				else
				{
					CurrentDynamicallyInstancedMeshCommandNumInstances = nullptr;
					FVisibleMeshDrawCommand NewVisibleMeshDrawCommand = VisibleMeshDrawCommand;
					NewVisibleMeshDrawCommand.PrimitiveIdBufferOffset = PrimitiveIdIndex;
					TempVisibleMeshDrawCommands.Emplace(MoveTemp(NewVisibleMeshDrawCommand));
				}
			}

			for (uint32 InstanceFactorIndex = 0; InstanceFactorIndex < InstanceFactor; InstanceFactorIndex++, PrimitiveIdIndex++)
			{
				PrimitiveIds[PrimitiveIdIndex] = VisibleMeshDrawCommand.DrawPrimitiveId;
			}
		}

		FMemory::Memswap(&VisibleMeshDrawCommands, &TempVisibleMeshDrawCommands, sizeof(TempVisibleMeshDrawCommands));
		TempVisibleMeshDrawCommands.Reset();
	}
	else
	{
		for (int32 i = 0; i < VisibleMeshDrawCommands.Num(); i++)
		{
			const FVisibleMeshDrawCommand& VisibleMeshDrawCommand = VisibleMeshDrawCommands[i];
			for (uint32 InstanceFactorIndex = 0; InstanceFactorIndex < InstanceFactor; InstanceFactorIndex++, PrimitiveIdIndex++)
			{
				PrimitiveIds[PrimitiveIdIndex] = VisibleMeshDrawCommand.DrawPrimitiveId;
			}
		}
	}
}


bool FDeferredShadingSceneRenderer::RenderBasePassView(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, 
	FExclusiveDepthStencil::Type BasePassDepthStencilAccess, 
	const FMeshPassProcessorRenderState& InDrawRenderState)
{
	View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(nullptr, RHICmdList);
}

void FParallelMeshDrawCommandPass::DispatchDraw(FParallelCommandListSet* ParallelCommandListSet = nullptr, FRHICommandList& RHICmdList) const
{
	WaitForMeshPassSetupTask();
	
	void* Data = RHILockVertexBuffer(PrimitiveIdVertexBufferPoolEntry.BufferRHI, 0, TaskContext.PrimitiveIdBufferDataSize, RLM_WriteOnly);
	FMemory::Memcpy(Data, TaskContext.PrimitiveIdBufferData, TaskContext.PrimitiveIdBufferDataSize);
	RHIUnlockVertexBuffer(PrimitiveIdVertexBufferPoolEntry.BufferRHI);

	FMeshDrawCommandStateCache StateCache;

	int i = 0;
	for (const auto& VisibleMeshDrawCommand : TaskContext.MeshDrawCommands)
	{
		// unreal bug!!!
		// int32 PrimitiveIdBufferOffset = (TaskContext.bDynamicInstancing ? VisibleMeshDrawCommand.PrimitiveIdBufferOffset : i) * sizeof(int32);
		int32 PrimitiveIdBufferOffset = (TaskContext.bDynamicInstancing ? VisibleMeshDrawCommand.PrimitiveIdBufferOffset : i * TaskContext.InstanceFactor) * sizeof(int32);
		
		FMeshDrawCommand::SubmitDraw(
			*VisibleMeshDrawCommand.MeshDrawCommand, 
			TaskContext.MinimalPipelineStatePassSet, 
			PrimitiveIdVertexBufferPoolEntry.BufferRHI, 
			PrimitiveIdBufferOffset, 
			TaskContext.InstanceFactor,, 
			StateCache);

		++i;
	}
}



void FMeshDrawCommand::SubmitDraw(
	const FMeshDrawCommand& MeshDrawCommand, 
	const FGraphicsMinimalPipelineStateSet& GraphicsMinimalPipelineStateSet,
	const FRHIVertexBuffer* ScenePrimitiveIdsBuffer,
	int32 PrimitiveIdOffset,
	uint32 InstanceFactor,,
	FMeshDrawCommandStateCache& StateCache)
{
	const FGraphicsMinimalPipelineStateInitializer& MeshPipelineState = MeshDrawCommand.CachedPipelineId.GetPipelineState(GraphicsMinimalPipelineStateSet);

	if (MeshDrawCommand.CachedPipelineId.GetId() != StateCache.PipelineId)
	{
		...
	}
	if (MeshDrawCommand.StencilRef != StateCache.StencilRef)
	{
		...
	}

	for (int32 VertexBindingIndex = 0; VertexBindingIndex < MeshDrawCommand.VertexStreams.Num(); VertexBindingIndex++)
	{
		const FVertexInputStream& Stream = MeshDrawCommand.VertexStreams[VertexBindingIndex];

		if (MeshDrawCommand.PrimitiveIdStreamIndex != -1 && Stream.StreamIndex == MeshDrawCommand.PrimitiveIdStreamIndex)
		{
			RHICmdList.SetStreamSource(Stream.StreamIndex, ScenePrimitiveIdsBuffer, PrimitiveIdOffset);
			StateCache.VertexStreams[Stream.StreamIndex] = Stream;
		}
		else if (StateCache.VertexStreams[Stream.StreamIndex] != Stream)
		{
			RHICmdList.SetStreamSource(Stream.StreamIndex, Stream.VertexBuffer, Stream.Offset);
			StateCache.VertexStreams[Stream.StreamIndex] = Stream;
		}
	}

	MeshDrawCommand.ShaderBindings.SetOnCommandList(RHICmdList, MeshPipelineState.BoundShaderState.AsBoundShaderState(), StateCache.ShaderBindings);

	if (MeshDrawCommand.IndexBuffer)
	{
		if (MeshDrawCommand.NumPrimitives > 0)
		{
			RHICmdList.DrawIndexedPrimitive(
				MeshDrawCommand.IndexBuffer,
				MeshDrawCommand.VertexParams.BaseVertexIndex,
				0,
				MeshDrawCommand.VertexParams.NumVertices,
				MeshDrawCommand.FirstIndex,
				MeshDrawCommand.NumPrimitives,
				MeshDrawCommand.NumInstances * InstanceFactor
			);
		}
		else
		{
			RHICmdList.DrawIndexedPrimitiveIndirect(
				MeshDrawCommand.IndexBuffer, 
				MeshDrawCommand.IndirectArgs.Buffer, 
				MeshDrawCommand.IndirectArgs.Offset
				);
		}
	}
	else
	{
		if (MeshDrawCommand.NumPrimitives > 0)
		{
			RHICmdList.DrawPrimitive(
				MeshDrawCommand.VertexParams.BaseVertexIndex + MeshDrawCommand.FirstIndex,
				MeshDrawCommand.NumPrimitives,
				MeshDrawCommand.NumInstances * InstanceFactor);
		}
		else
		{
			RHICmdList.DrawPrimitiveIndirect(
				MeshDrawCommand.IndirectArgs.Buffer,
				MeshDrawCommand.IndirectArgs.Offset
			);
		}
	}
}