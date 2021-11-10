class FComponentRecreateRenderStateContext {
	UActorComponent* Component;
	TSet<FSceneInterface*>* ScenesToUpdateAllPrimitiveSceneInfos;
};

class FGlobalComponentRecreateRenderStateContext {
	TArray<FComponentRecreateRenderStateContext> ComponentContexts;
	TSet<FSceneInterface*> ScenesToUpdateAllPrimitiveSceneInfos;
};

FComponentRecreateRenderStateContext::FComponentRecreateRenderStateContext(UActorComponent* InComponent, TSet<FSceneInterface*>* InScenesToUpdateAllPrimitiveSceneInfos)
{
	ScenesToUpdateAllPrimitiveSceneInfos = InScenesToUpdateAllPrimitiveSceneInfos;

	if (InComponent->IsRegistered() && InComponent->IsRenderStateCreated())
	{
		InComponent->DestroyRenderState_Concurrent();
		Component = InComponent;
		ScenesToUpdateAllPrimitiveSceneInfos->Add(Component->GetScene());
	}
	else
	{
		Component = nullptr;
	}
}

FComponentRecreateRenderStateContext::~FComponentRecreateRenderStateContext()
{
	if (Component && !Component->IsRenderStateCreated() && Component->IsRegistered())
	{
		Component->CreateRenderState_Concurrent(nullptr); // Call Scene->AddPrimitive(this);
		ScenesToUpdateAllPrimitiveSceneInfos->Add(Component->GetScene());
	}
}

FGlobalComponentRecreateRenderStateContext::FGlobalComponentRecreateRenderStateContext()
{
	FlushRenderingCommands();

	for (UActorComponent* Component : TObjectRange<UActorComponent>())
	{
		if (Component->IsRegistered() && Component->IsRenderStateCreated())
		{
			ComponentContexts.Add(
				FComponentRecreateRenderStateContext(Component, &ScenesToUpdateAllPrimitiveSceneInfos) );
		}
	}

	UpdateAllPrimitiveSceneInfos();
}

FGlobalComponentRecreateRenderStateContext::~FGlobalComponentRecreateRenderStateContext()
{
	ComponentContexts.Empty();

	UpdateAllPrimitiveSceneInfos();
}

void FGlobalComponentRecreateRenderStateContext::UpdateAllPrimitiveSceneInfos()
{
	UpdateAllPrimitiveSceneInfosForScenes(MoveTemp(ScenesToUpdateAllPrimitiveSceneInfos));
}

void UpdateAllPrimitiveSceneInfosForScenes(TSet<FSceneInterface*> ScenesToUpdateAllPrimitiveSceneInfos)
{
	ENQUEUE_RENDER_COMMAND(UpdateAllPrimitiveSceneInfosCmd)(
		[ScenesToUpdateAllPrimitiveSceneInfos](FRHICommandListImmediate& RHICmdList)
	{
		for (FSceneInterface* Scene : ScenesToUpdateAllPrimitiveSceneInfos)
		{
			Scene->UpdateAllPrimitiveSceneInfos(RHICmdList);
		}
	});
}

FScene::UpdateAllPrimitiveSceneInfos() -> FPrimitiveSceneInfo::AddToScene() -> 
FPrimitiveSceneInfo::AddStaticMeshes() -> CacheMeshDrawCommands()



// GEngine->Tick() -> UUnrealEdEngine::Tick() -> UEditorEngine::UpdateBuildLighting() ->
// FStaticLightingManager::UpdateBuildLighting() -> FStaticLightingSystem::UpdateLightingBuild() ->
// FStaticLightingManager::ProcessLightingData() ->
bool FStaticLightingSystem::FinishLightmassProcess()
{
	InvalidateStaticLighting();
	bSuccessful = LightmassProcessor->CompleteRun();
	ULightComponent::ReassignStationaryLightChannels(GWorld, true, LightingScenario);
	EncodeTextures(bSuccessful);
	LightmassProcessor->CloseJob();
	ApplyNewLightingData(bSuccessful);
}

void FStaticLightingSystem::InvalidateStaticLighting()
{
	FGlobalComponentRecreateRenderStateContext Context;

	for( int32 i=0; i<World->GetNumLevels(); i++ )
	{
		ULevel* Level = World->GetLevel(i); 

		const bool bBuildLightingForLevel = Options.ShouldBuildLightingForLevel( Level );
		if (bBuildLightingForLevel)
		{
			Level->ReleaseRenderingResources();
			if (Level->MapBuildData)
			{
				Level->MapBuildData->InvalidateStaticLighting(World, false, &BuildDataResourcesToKeep);
			}
		}
	}
}

void UMapBuildDataRegistry::InvalidateStaticLighting(UWorld* World, bool bRecreateRenderState, const TSet<FGuid>* ResourcesToKeep = {})
{
	MeshBuildData.Empty();
	LightBuildData.Empty();
	MarkPackageDirty();

	for (int32 i = 0; i < World->GetNumLevels(); i++)
	{
		World->GetLevel(i)->ReleaseRenderingResources();
		{
			PrecomputedLightVolume->RemoveFromScene(OwningWorld->Scene);
			PrecomputedVolumetricLightmap->RemoveFromScene(OwningWorld->Scene);
		}
	}

	ReleaseResources(ResourcesToKeep);
	{
		for (TMap<FGuid, FPrecomputedVolumetricLightmapData*>::TIterator It(LevelPrecomputedVolumetricLightmapBuildData); It; ++It)
		{
			BeginReleaseResource(It.Value());
		}

		for (FLightmapResourceCluster& ResourceCluster : LightmapResourceClusters)
		{
			BeginReleaseResource(&ResourceCluster);
		}
	}

	FlushRenderingCommands();

	EmptyLevelData(ResourcesToKeep);
	{
		TMap<FGuid, FPrecomputedVolumetricLightmapData*> PrevPrecomputedVolumetricLightmapData;
		FMemory::Memswap(&LevelPrecomputedVolumetricLightmapBuildData , &PrevPrecomputedVolumetricLightmapData, sizeof(LevelPrecomputedVolumetricLightmapBuildData));

		for (TMap<FGuid, FPrecomputedVolumetricLightmapData*>::TIterator It(PrevPrecomputedVolumetricLightmapData); It; ++It)
		{
			delete It.Value();
		}

		LightmapResourceClusters.Empty();
	}

	MarkPackageDirty();

	ClearSkyAtmosphereBuildData();
}

bool FLightmassProcessor::CompleteRun()
{
	ImportVolumeSamples();
	ImportVolumetricLightmap();
	ImportPrecomputedVisibility();
	ImportMeshAreaLightData();
	ImportVolumeDistanceFieldData();
	ImportMappings(false);
	{
		FGlobalComponentRecreateRenderStateContext ReregisterContext;
		FlushRenderingCommands();
		ProcessAvailableMappings();
	}
	ApplyPrecomputedVisibility();
	ProcessAlertMessages();
}

//////////////////////////////////////////////////////////////////////////////////////
class FStaticLightingMesh : public virtual FRefCountedObject {
	const TArray<ULightComponent*> RelevantLights;/** The lights which affect the mesh's primitive. */
	const UPrimitiveComponent* const Component;/** The primitive component this mesh was created by. */
	FGuid Guid;/** Unique ID for tracking this lighting mesh during distributed lighting */
	FGuid SourceMeshGuid;/** Cached guid for the source mesh */

}
class FStaticMeshStaticLightingMesh : public FStaticLightingMesh {}

class FStaticLightingMapping : public virtual FRefCountedObject {/** A mapping between world-space surfaces and a static lighting cache. */
	class FStaticLightingMesh* Mesh;/** The mesh associated with the mapping. */
	UObject* const Owner;/** The object which owns the mapping. */
	uint32 bProcessMapping : 1;/** true if the mapping should be processed by Lightmass. */
}
class FStaticLightingTextureMapping : public FStaticLightingMapping {/** A mapping between world-space surfaces and static lighting cache textures. */
	const int32 SizeX;/** The width of the static lighting textures used by the mapping. */
	const int32 SizeY;/** The height of the static lighting textures used by the mapping. */
	const int32 LightmapTextureCoordinateIndex;/** The lightmap texture coordinate index which is used for the mapping. */
	const bool bBilinearFilter;/** Whether to apply a bilinear filter to the sample or not. */

	/**
	 * Called when the static lighting has been computed to apply it to the mapping's owner.
	 * This function is responsible for deleting ShadowMapData and QuantizedData.
	 * @param LightMapData - The light-map data which has been computed for the mapping.
	 */
	virtual void Apply(
		struct FQuantizedLightmapData* QuantizedData, 
		const TMap<ULightComponent*,
		class FShadowMapData2D*>& ShadowMapData, 
		ULevel* LightingScenario) = 0;
};
class FStaticMeshStaticLightingTextureMapping : public FStaticLightingTextureMapping {/** Represents a static mesh primitive with texture mapped static lighting. */
	TWeakObjectPtr<UStaticMeshComponent> Primitive;/** The primitive this mapping represents. */
	const int32 LODIndex;/** The LOD this mapping represents. */
};
/*
class FLandscapeStaticLightingMesh : public FStaticLightingMesh {}
class FBSPSurfaceStaticLighting : public FStaticLightingTextureMapping, public FStaticLightingMesh {}
class FStaticLightingGlobalVolumeMapping : public FStaticLightingTextureMapping {}
class FLandscapeStaticLightingTextureMapping : public FStaticLightingTextureMapping {}
*/

struct FLightMapCoefficients {/** The quantized coefficients for a single light-map texel. */
	uint8 Coverage;
	uint8 Coefficients[4][4];
	uint8 SkyOcclusion[4];
	uint8 AOMaterialMask;
};
struct FQuantizedLightmapData {
	uint32 SizeX;
	uint32 SizeY;
	TArray<FLightMapCoefficients> Data;
	float Scale[4][4];
	float Add[4][4];
	TArray<FGuid> LightGuids;/** The GUIDs of lights which this light-map stores. */
	bool bHasSkyShadowing;
};

struct FQuantizedSignedDistanceFieldShadowSample {
	uint8 Distance;
	uint8 PenumbraSize;
	uint8 Coverage;
};
class FShadowMapData2D {
	uint32 SizeX;
	uint32 SizeY;
};
class FQuantizedShadowSignedDistanceFieldData2D : public FShadowMapData2D {
	TArray<FQuantizedSignedDistanceFieldShadowSample> Data;
};

class FLightmassProcessor {
	struct FMappingImportHelper {
		StaticLightingType Type = SLT_Texture;
		FGuid MappingGuid;/** The mapping guid read in */
		bool bProcessed;/** Whether the mapping has been processed yet */
	}
	struct FTextureMappingImportHelper : public FMappingImportHelper {
		FStaticLightingTextureMapping* TextureMapping;/** The texture mapping being imported */
		FQuantizedLightmapData* QuantizedData;/** The imported quantized lightmap data */
		TMap<ULightComponent*,FShadowMapData2D*> ShadowMapData;
	}
	TMap<FGuid, FMappingImportHelper*> ImportedMappings;/** Mappings that have been imported but not processed. */
}
//////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////
/*
Out: ULightComponent::PreviewShadowMapChannel						,for all stationary lights in the targetworld
Out: UMapBuildDataRegistry::LightBuildData[guid].ShadowMapChannel 	,only when bAssignForLightingBuild == true
*/
void ULightComponent::ReassignStationaryLightChannels(UWorld* TargetWorld, bool bAssignForLightingBuild, ULevel* LightingScenario = nullptr)
{
	struct FLightAndChannel {
		ULightComponent* Light;
		int32 Channel = -1;
	};

	TMap<FLightAndChannel*, TArray<FLightAndChannel*> > LightToOverlapMap;

	// Build an array of all static shadowing lights that need to be assigned
	for (TObjectIterator<ULightComponent> LightIt(,,); LightIt; ++LightIt)
	{
		ULightComponent* const LightComponent = *LightIt;
		AActor* LightOwner = LightComponent->GetOwner();

		const bool bLightIsInWorld = LightOwner && TargetWorld->ContainsActor(LightOwner) && !LightOwner->IsPendingKill();
		if (bLightIsInWorld 
			&& LightComponent->HasStaticShadowing()
			&& !LightComponent->HasStaticLighting())
		{
			if (LightComponent->bAffectsWorld
				&& LightComponent->CastShadows 
				&& LightComponent->CastStaticShadows)
			{
				LightToOverlapMap.Add(new FLightAndChannel(LightComponent), TArray<FLightAndChannel*>());
			}
			else
			{
				LightComponent->PreviewShadowMapChannel = INDEX_NONE;
				LightComponent->UpdateLightSpriteTexture();
			}
		}
	}

	// Build an array of overlapping lights
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		ULightComponent* CurrentLight = It.Key()->Light;
		TArray<FLightAndChannel*>& OverlappingLights = It.Value();

		for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator OtherIt(LightToOverlapMap); OtherIt; ++OtherIt)
		{
			ULightComponent* OtherLight = OtherIt.Key()->Light;

			if (CurrentLight != OtherLight 
				&& CurrentLight->AffectsBounds(FBoxSphereBounds(OtherLight->GetBoundingSphere()))
				&& OtherLight->AffectsBounds(FBoxSphereBounds(CurrentLight->GetBoundingSphere())))
			{
				OverlappingLights.Add(OtherIt.Key());
			}
		}
	}
	
	// Sort lights with the most overlapping lights first
	LightToOverlapMap.ValueSort(FCompareLightsByArrayCount());

	// Go through lights and assign shadowmap channels
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		bool bChannelUsed[4] = {0};
		FLightAndChannel* CurrentLight = It.Key();
		const TArray<FLightAndChannel*>& OverlappingLights = It.Value();

		// Mark which channels have already been assigned to overlapping lights
		for (int32 OverlappingIndex = 0; OverlappingIndex < OverlappingLights.Num(); OverlappingIndex++)
		{
			FLightAndChannel* OverlappingLight = OverlappingLights[OverlappingIndex];

			if (OverlappingLight->Channel != INDEX_NONE)
			{
				bChannelUsed[OverlappingLight->Channel] = true;
			}
		}

		// Use the lowest free channel
		for (int32 ChannelIndex = 0; ChannelIndex < 4; ChannelIndex++)
		{
			if (!bChannelUsed[ChannelIndex])
			{
				CurrentLight->Channel = ChannelIndex;
				break;
			}
		}
	}

	// Go through the assigned lights and update their render state and icon
	for (TMap<FLightAndChannel*, TArray<FLightAndChannel*> >::TIterator It(LightToOverlapMap); It; ++It)
	{
		FLightAndChannel* CurrentLight = It.Key();

		if (CurrentLight->Light->PreviewShadowMapChannel != CurrentLight->Channel)
		{
			CurrentLight->Light->PreviewShadowMapChannel = CurrentLight->Channel;
			CurrentLight->Light->MarkRenderStateDirty();
		}

		CurrentLight->Light->UpdateLightSpriteTexture();

		if (bAssignForLightingBuild)
		{
			UMapBuildDataRegistry* Registry = CurrentLight->Light->GetOwner()->GetLevel()->MapBuildData;
			FLightComponentMapBuildData& LightBuildData = Registry->GetLightBuildData(CurrentLight->Light->LightGuid);

			LightBuildData.ShadowMapChannel = CurrentLight->Channel;
			Registry->MarkPackageDirty();
		}

		delete CurrentLight;
	}
}
//////////////////////////////////////////////////////////////////////////////////////


//////////////////////////////////////////////////////////////////////////////////////
void FStaticLightingSystem::ApplyNewLightingData(bool bLightingSuccessful=true)
{
	for (int32 i = 0; i < World->GetNumLevels(); i++)
	{
		ULevel* Level = World->GetLevel(i);
		uint32 ActorCount = Level->Actors.Num();
		UMapBuildDataRegistry* Registry = (LightingScenario ? LightingScenario : Level) -> GetOrCreateMapBuildData();

		for (uint32 ActorIndex = 0; ActorIndex < ActorCount; ++ActorIndex)
		{
			AActor* Actor = Level->Actors[ActorIndex];

			TInlineComponentArray<ULightComponent*> LightComponents;
			Actor->GetComponents(LightComponents);
			for (int32 ComponentIndex = 0; ComponentIndex < LightComponents.Num(); ComponentIndex++)
			{
				ULightComponent* LightComponent = LightComponents[ComponentIndex];
				if (LightComponent && (LightComponent->HasStaticShadowing() || LightComponent->HasStaticLighting()))
				{
					if (!Registry->GetLightBuildData(LightComponent->LightGuid))
					{
						// Add a dummy entry for ULightComponent::IsPrecomputedLightingValid()
						Registry->FindOrAllocateLightBuildData(LightComponent->LightGuid, true);
					}
				}
			}

			TInlineComponentArray<USkyAtmosphereComponent*> SkyAtmosphereComponents;
			Actor->GetComponents(SkyAtmosphereComponents);
			for (int32 ComponentIndex = 0; ComponentIndex < SkyAtmosphereComponents.Num(); ComponentIndex++)
			{
				...
			}
		}

		const bool bBuildLightingForLevel = Options.ShouldBuildLightingForLevel( Level );
		if( bBuildLightingForLevel )
		{
			Registry->LevelLightingQuality = Options.QualityLevel;
			Registry->MarkPackageDirty();
		}

		Registry->SetupLightmapResourceClusters();
		// From now on, UStaticMeshComponent::GetMeshMapBuildData() returns non-NULL data !!

		Level->InitializeRenderingResources();
		{
			PrecomputedVolumetricLightmap->AddToScene(OwningWorld->Scene, EffectiveMapBuildData, LevelBuildDataId, IsPersistentLevel());
			MapBuildData->InitializeClusterRenderingResources(OwningWorld->Scene->GetFeatureLevel());
			{
				for (FLightmapResourceCluster& Cluster : LightmapResourceClusters)
				{
					Cluster.UpdateUniformBuffer(InFeatureLevel);
				}
			}
		}
	}

	{
		FGlobalComponentRecreateRenderStateContext RecreateRenderState;
	}
}

void UMapBuildDataRegistry::SetupLightmapResourceClusters()
{
	TSet<FLightmapClusterResourceInput> LightmapClusters;
	LightmapClusters.Empty(1 + MeshBuildData.Num() / 30);

	for (TMap<FGuid, FMeshMapBuildData>::TIterator It(MeshBuildData); It; ++It)
	{
		const FMeshMapBuildData& Data = It.Value();
		LightmapClusters.Add(GetClusterInput(Data));
	}

	LightmapResourceClusters.Empty(LightmapClusters.Num());
	LightmapResourceClusters.AddDefaulted(LightmapClusters.Num());

	for (TMap<FGuid, FMeshMapBuildData>::TIterator It(MeshBuildData); It; ++It)
	{
		FMeshMapBuildData& Data = It.Value();
		const FLightmapClusterResourceInput ClusterInput = GetClusterInput(Data);

		const FSetElementId ClusterId = LightmapClusters.FindId(ClusterInput);
		const int32 ClusterIndex = ClusterId.AsInteger();
		LightmapResourceClusters[ClusterIndex].Input = ClusterInput;
		
		Data.ResourceCluster = &LightmapResourceClusters[ClusterIndex];
	}

	// Init empty cluster uniform buffers so they can be referenced by cached mesh draw commands.
	// Can't create final uniform buffers as feature level is unknown at this point.
	for (FLightmapResourceCluster& Cluster : LightmapResourceClusters)
	{
		BeginInitResource(&Cluster);
		{
			FLightmapResourceCluster::InitRHI();
		}
	}
}

void FLightmapResourceCluster::InitRHI() 
{
	FLightmapResourceClusterShaderParameters Parameters;
	GetLightmapClusterResourceParameters(GMaxRHIFeatureLevel, FLightmapClusterResourceInput(), nullptr, Parameters);
	UniformBuffer = FLightmapResourceClusterShaderParameters::CreateUniformBuffer(Parameters, UniformBuffer_MultiFrame);
}

void FLightmapResourceCluster::UpdateUniformBuffer(ERHIFeatureLevel::Type InFeatureLevel)
{
	FLightmapResourceCluster* Cluster = this;

	ENQUEUE_RENDER_COMMAND(SetFeatureLevel)(
		[Cluster, InFeatureLevel](FRHICommandList& RHICmdList)
	{
		FLightmapResourceClusterShaderParameters Parameters;
		GetLightmapClusterResourceParameters(InFeatureLevel, Cluster->Input, nullptr, Parameters);
		RHIUpdateUniformBuffer(Cluster->UniformBuffer, &Parameters);
	});
}