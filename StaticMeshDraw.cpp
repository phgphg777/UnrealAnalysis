/*
FPrimitiveSceneInfo::AddStaticMeshes() -> 
FStaticMeshSceneProxy::DrawStaticElements() ->
FStaticMeshSceneProxy::GetMeshElement() && FBatchingSPDI::DrawMesh()
*/

FStaticMeshBatchRelevance(
	const FStaticMeshBatch& StaticMesh, 
	float InScreenSize, 
	bool InbSupportsCachingMeshDrawCommands, 
	bool InbUseSkyMaterial, 
	bool bInUseSingleLayerWaterMaterial, 
	ERHIFeatureLevel::Type FeatureLevel)
: Id(StaticMesh.Id)
, ScreenSize(InScreenSize)
, NumElements(StaticMesh.Elements.Num())
, CommandInfosBase(0)
, LODIndex(StaticMesh.LODIndex)
//, bDitheredLODTransition(StaticMesh.bDitheredLODTransition)
, bRequiresPerElementVisibility(StaticMesh.bRequiresPerElementVisibility)
, bSelectable(StaticMesh.bSelectable)
, CastShadow(StaticMesh.CastShadow)
, bUseForMaterial(StaticMesh.bUseForMaterial)
, bUseForDepthPass(StaticMesh.bUseForDepthPass)
, bUseAsOccluder(StaticMesh.bUseAsOccluder)
//, bUseSkyMaterial(InbUseSkyMaterial)
//, bUseSingleLayerWaterMaterial(bInUseSingleLayerWaterMaterial)
//, bUseHairStrands(StaticMesh.UseForHairStrands(FeatureLevel))
//, bRenderToVirtualTexture(StaticMesh.bRenderToVirtualTexture)
//, RuntimeVirtualTextureMaterialType(StaticMesh.RuntimeVirtualTextureMaterialType)
, bSupportsCachingMeshDrawCommands(InbSupportsCachingMeshDrawCommands) {}

void FPrimitiveSceneInfo::AddStaticMeshes(, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos)
{
	ParallelForTemplate(SceneInfos.Num(), [Scene, &SceneInfos](int32 Index)
	{
		FPrimitiveSceneInfo* SceneInfo = SceneInfos[Index];
		// Cache the primitive's static mesh elements.
		FBatchingSPDI BatchingSPDI(SceneInfo);
		BatchingSPDI.SetHitProxy(SceneInfo->DefaultDynamicHitProxy);
		SceneInfo->Proxy->DrawStaticElements(&BatchingSPDI);
	});

	for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
	{
		for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
		{
			FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshIndex];
			FStaticMeshBatch& Mesh = SceneInfo->StaticMeshes[MeshIndex];

			FSparseArrayAllocationInfo SceneArrayAllocation = Scene->StaticMeshes.AddUninitialized();
			Scene->StaticMeshes[SceneArrayAllocation.Index] = &Mesh;
			Mesh.Id = SceneArrayAllocation.Index;
			MeshRelevance.Id = SceneArrayAllocation.Index;

			if (Mesh.bRequiresPerElementVisibility)
			{
				Mesh.BatchVisibilityId = Scene->StaticMeshBatchVisibility.AddUninitialized().Index;
				Scene->StaticMeshBatchVisibility[Mesh.BatchVisibilityId] = true;
			}
		}
	}

	CacheMeshDrawCommands(RHICmdList, Scene, SceneInfos);
}

void FStaticMeshSceneProxy::DrawStaticElements(FStaticPrimitiveDrawInterface* PDI)
{
	for(int32 LODIndex = ClampedMinLOD; LODIndex < NumLODs; LODIndex++)
	{
		const FStaticMeshLODResources& LODModel = RenderData->LODResources[LODIndex];
		float ScreenSize = GetScreenSize(LODIndex);

		bool bUseUnifiedMeshForShadow = false;
		bool bUseUnifiedMeshForDepth = false;

		if (GUseShadowIndexBuffer && LODModel.bHasDepthOnlyIndices)
		{
			if (bSafeToUseUnifiedMesh)
			{
				if (bUseUnifiedMeshForShadow || bUseUnifiedMeshForDepth)
				{
					FMeshBatch MeshBatch;
					if (GetShadowMeshElement(LODIndex, 0, PrimitiveDPG, MeshBatch, bAllSectionsUseDitheredLODTransition))
					{
						MeshBatch.CastShadow = bUseUnifiedMeshForShadow;
						MeshBatch.bUseForDepthPass = bUseUnifiedMeshForDepth;
						MeshBatch.bUseAsOccluder = bUseUnifiedMeshForDepth;
						MeshBatch.bUseForMaterial = false;
						PDI->DrawMesh(MeshBatch, ScreenSize);
					}
				}
			}
		}

		for(int32 SectionIndex = 0;SectionIndex < LODModel.Sections.Num();SectionIndex++)
		{
			if( GIsEditor )
			{
				const FLODInfo::FSectionInfo& Section = LODs[LODIndex].Sections[SectionIndex];
				bIsMeshElementSelected = Section.bSelected;
				PDI->SetHitProxy(Section.HitProxy);
			}

			FMeshBatch BaseMeshBatch;
			if (GetMeshElement(LODIndex, 0, SectionIndex, PrimitiveDPG, bIsMeshElementSelected, true, BaseMeshBatch))
			{
				// Standard mesh elements.
				// If we have submitted an optimized shadow-only mesh, remaining mesh elements must not cast shadows.
				FMeshBatch MeshBatch(BaseMeshBatch);
				MeshBatch.CastShadow &= !bUseUnifiedMeshForShadow;
				MeshBatch.bUseAsOccluder &= !bUseUnifiedMeshForDepth;
				MeshBatch.bUseForDepthPass &= !bUseUnifiedMeshForDepth;
				PDI->DrawMesh(MeshBatch, ScreenSize);
			}
			
		}
	}
}

void FBatchingSPDI::DrawMesh(const FMeshBatch& Mesh, float ScreenSize)
{
	if (Mesh.HasAnyDrawCalls())
	{
		check(Mesh.VertexFactory);
		check(Mesh.VertexFactory->IsInitialized());
		checkSlow(IsInParallelRenderingThread());

		FPrimitiveSceneProxy* PrimitiveSceneProxy = PrimitiveSceneInfo->Proxy;
		PrimitiveSceneProxy->VerifyUsedMaterial(Mesh.MaterialRenderProxy);

		FStaticMeshBatch* StaticMesh = new(PrimitiveSceneInfo->StaticMeshes) FStaticMeshBatch(
			PrimitiveSceneInfo,
			Mesh,
			CurrentHitProxy ? CurrentHitProxy->Id : FHitProxyId()
			);

		const ERHIFeatureLevel::Type FeatureLevel = PrimitiveSceneInfo->Scene->GetFeatureLevel();
		StaticMesh->PreparePrimitiveUniformBuffer(PrimitiveSceneProxy, FeatureLevel);

		const bool bSupportsCachingMeshDrawCommands = 
			StaticMesh->VertexFactory->GetType()->SupportsCachingMeshDrawCommands() && 
			StaticMesh->Elements.Num() == 1 &&
			!PrimitiveSceneProxy->CastsVolumetricTranslucentShadow();

		bool bUseSkyMaterial = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->IsSky();
		bool bUseSingleLayerWaterMaterial = Mesh.MaterialRenderProxy->GetMaterial(FeatureLevel)->GetShadingModels().HasShadingModel(MSM_SingleLayerWater);
		FStaticMeshBatchRelevance* StaticMeshRelevance = new(PrimitiveSceneInfo->StaticMeshRelevances) FStaticMeshBatchRelevance(
			*StaticMesh, 
			ScreenSize, 
			bSupportsCachingMeshDrawCommands,
			//bUseSkyMaterial,
			//bUseSingleLayerWaterMaterial,
			//FeatureLevel
		);
	}
}

void FPrimitiveSceneInfo::CacheMeshDrawCommands(, , const TArrayView<FPrimitiveSceneInfo*>& SceneInfos)
{
	struct FMeshInfoAndIndex {
		int32 InfoIndex;
		int32 MeshIndex;
	};
	static constexpr int BATCH_SIZE = 64;
	const int NumBatches = (SceneInfos.Num() + BATCH_SIZE - 1) / BATCH_SIZE;

	auto DoWorkLambda = [Scene, SceneInfos](int32 Index)
	{
		TArray<FMeshInfoAndIndex> MeshBatches;

		int LocalMax = FMath::Min((Index * BATCH_SIZE) + BATCH_SIZE, SceneInfos.Num());
		for (int LocalIndex = (Index * BATCH_SIZE); LocalIndex < LocalMax; LocalIndex++)
		{
			FPrimitiveSceneInfo* SceneInfo = SceneInfos[LocalIndex];
			SceneInfo->StaticMeshCommandInfos.AddDefaulted(EMeshPass::Num * SceneInfo->StaticMeshes.Num());
			
			for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
			{
				if ( SceneInfo->StaticMeshRelevances[MeshIndex].bSupportsCachingMeshDrawCommands )
				{
					MeshBatches.Add( FMeshInfoAndIndex{ LocalIndex, MeshIndex } );
				}
			}
		}

		for (int32 PassIndex = 0; PassIndex < EMeshPass::Num; PassIndex++)
		{
			EMeshPass::Type PassType = (EMeshPass::Type)PassIndex;

			if ((FPassProcessorManager::GetPassFlags(ShadingPath, PassType) & EMeshPassFlags::CachedMeshCommands) != EMeshPassFlags::None)
			{
				FCachedMeshDrawCommandInfo CommandInfo(PassType);

				FCriticalSection& CachedMeshDrawCommandLock = Scene->CachedMeshDrawCommandLock[PassType];
				FCachedPassMeshDrawList& SceneDrawList = Scene->CachedDrawLists[PassType];
				FStateBucketMap& CachedMeshDrawCommandStateBuckets = Scene->CachedMeshDrawCommandStateBuckets[PassType];
				FCachedPassMeshDrawListContext CachedPassMeshDrawListContext(CommandInfo, CachedMeshDrawCommandLock, SceneDrawList, CachedMeshDrawCommandStateBuckets, *Scene);

				PassProcessorCreateFunction CreateFunction = FPassProcessorManager::GetCreateFunction(ShadingPath, PassType);
				FMeshPassProcessor* PassMeshProcessor = CreateFunction(Scene, nullptr, &CachedPassMeshDrawListContext);

				if (PassMeshProcessor != nullptr)
				{
					for ( {LocalIndex, MeshIndex} : MeshBatches )
					{
						FPrimitiveSceneInfo* SceneInfo = SceneInfos[LocalIndex];
						
						CommandInfo = FCachedMeshDrawCommandInfo(PassType);
						FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshIndex];

						PassMeshProcessor->AddMeshBatch(SceneInfo->StaticMeshes[MeshIndex], ~0ull, SceneInfo->Proxy);

						if (CommandInfo.CommandIndex != -1 || CommandInfo.StateBucketId != -1)
						{
							MeshRelevance.CommandInfosMask.Set(PassType);
							MeshRelevance.CommandInfosBase++;

							int CommandInfoIndex = MeshIndex * EMeshPass::Num + PassType;
							SceneInfo->StaticMeshCommandInfos[CommandInfoIndex] = CommandInfo;
						}
					}
					PassMeshProcessor->~FMeshPassProcessor();
				}
			}
		}

		for (int LocalIndex = (Index * BATCH_SIZE); LocalIndex < LocalMax; LocalIndex++)
		{
			FPrimitiveSceneInfo* SceneInfo = SceneInfos[LocalIndex];	
			int PrefixSum = 0;
			for (int32 MeshIndex = 0; MeshIndex < SceneInfo->StaticMeshes.Num(); MeshIndex++)
			{
				FStaticMeshBatchRelevance& MeshRelevance = SceneInfo->StaticMeshRelevances[MeshIndex];
				if (MeshRelevance.CommandInfosBase > 0)
				{
					EMeshPass::Type PassType = EMeshPass::DepthPass;
					int NewPrefixSum = PrefixSum;
					for (;;)
					{
						PassType = MeshRelevance.CommandInfosMask.SkipEmpty(PassType);
						if (PassType == EMeshPass::Num)
							break;

						int CommandInfoIndex = MeshIndex * EMeshPass::Num + PassType;
						SceneInfo->StaticMeshCommandInfos[NewPrefixSum] = SceneInfo->StaticMeshCommandInfos[CommandInfoIndex];
						NewPrefixSum++;
						PassType = EMeshPass::Type(PassType + 1);
					}

					MeshRelevance.CommandInfosBase = PrefixSum;
					PrefixSum = NewPrefixSum;
				}
			}

			SceneInfo->StaticMeshCommandInfos.SetNum(PrefixSum, true);
		}
	};

	ParallelForTemplate(NumBatches, DoWorkLambda, EParallelForFlags::PumpRenderingThread);
}
