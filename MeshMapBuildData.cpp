class ULevel : public UObject, public IInterface_AssetUserData {
	UMapBuildDataRegistry* MapBuildData;
}

class UMapBuildDataRegistry : public UObject {
	TEnumAsByte<enum ELightingBuildQuality> LevelLightingQuality;
	
	TMap<FGuid, FMeshMapBuildData> MeshBuildData;
	TMap<FGuid, FLightComponentMapBuildData> LightBuildData;
	TArray<FLightmapResourceCluster> LightmapResourceClusters;
}

/* class FLightmapClusterResourceInput {
	const UTexture2D* LightMapTextures[2];
	const UTexture2D* SkyOcclusionTexture;
	const UTexture2D* AOMaterialMaskTexture;
	const ULightMapVirtualTexture2D* LightMapVirtualTexture;
	const UTexture2D* ShadowMapTexture;
} */
class FLightmapResourceCluster : public FRenderResource {
	FLightmapClusterResourceInput Input;
	FUniformBufferRHIRef UniformBuffer;
}

class FMeshMapBuildData {	
	FLightMapRef LightMap;
	FShadowMapRef ShadowMap;
	TArray<FGuid> IrrelevantLights;
	TArray<FPerInstanceLightmapData> PerInstanceLightmapData;
	const FLightmapResourceCluster* ResourceCluster = nullptr;
};

FMeshMapBuildData* UMapBuildDataRegistry::GetMeshBuildData(FGuid MeshId)
{
	FMeshMapBuildData* FoundData = MeshBuildData.Find(MeshId);
	if (FoundData && !FoundData->ResourceCluster)
	{
		return nullptr;
	}
	return FoundData;
}
//////////////////////////////////////////////////////////////////////////////////////

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
};
class FStaticMeshStaticLightingTextureMapping : public FStaticLightingTextureMapping {/** Represents a static mesh primitive with texture mapped static lighting. */
	TWeakObjectPtr<UStaticMeshComponent> Primitive;/** The primitive this mapping represents. */
	const int32 LODIndex;/** The LOD this mapping represents. */
};
//////////////////////////////////////////////////////////////////////////////////////




//////////////////////////////////////////////////////////////////////////////////////
void FLightmassProcessor::ImportMappings(bool bProcessImmediately = false)
{
	for(auto& Guid : CompletedMappingTasks.ExtractAll())
	{
		ImportMapping(Guid, bProcessImmediately);
	}
}

void FLightmassProcessor::ImportMapping( const FGuid& MappingGuid, bool bProcessImmediately )
{
	if (IsStaticLightingTextureMapping(MappingGuid))
	{
		ImportStaticLightingTextureMapping(MappingGuid, bProcessImmediately);
	}
	else
	{
		ULightComponent* Light = FindLight(MappingGuid);

		if (Light)
		{
			ImportStaticShadowDepthMap(Light);
		}
		else
		{
			check(0);
		}
	}
}

class Lightmass::FStaticShadowDepthMapData {
	FMatrix WorldToLight;
	int32 ShadowMapSizeX;
	int32 ShadowMapSizeY;
};
class FStaticShadowDepthMapData {
	FMatrix WorldToLight;
	int32 ShadowMapSizeX;
	int32 ShadowMapSizeY;
	TArray<FFloat16> DepthSamples;
};
class FLightComponentMapBuildData {
	int32 ShadowMapChannel = -1;
	FStaticShadowDepthMapData DepthMap;
};
/*
Add:  UMapBuildDataRegistry::LightBuildData by (Light->LightGuid, FLightComponentMapBuildData())
Read: UMapBuildDataRegistry::LightBuildData[Light->LightGuid].DepthMap from Swam
*/
void FLightmassProcessor::ImportStaticShadowDepthMap(ULightComponent* Light)
{
	BeginReleaseResource(&Light->StaticShadowDepthMap);

	const FString ChannelName = Lightmass::CreateChannelName(Light->LightGuid, Lightmass::LM_DOMINANTSHADOW_VERSION, Lightmass::LM_DOMINANTSHADOW_EXTENSION);
	const int32 Channel = Swarm.OpenChannel( *ChannelName, LM_DOMINANTSHADOW_CHANNEL_FLAGS );

	UMapBuildDataRegistry* Registry = Light->GetOwner()->GetLevel()->GetOrCreateMapBuildData();
	FLightComponentMapBuildData& CurrentLightData = Registry->FindOrAllocateLightBuildData(Light->LightGuid, true);

	Lightmass::FStaticShadowDepthMapData ShadowMapData;
	Swarm.ReadChannel(Channel, &ShadowMapData, sizeof(ShadowMapData));
	
	CurrentLightData.DepthMap.WorldToLight = ShadowMapData.WorldToLight;
	CurrentLightData.DepthMap.ShadowMapSizeX = ShadowMapData.ShadowMapSizeX;
	CurrentLightData.DepthMap.ShadowMapSizeY = ShadowMapData.ShadowMapSizeY;

	ReadArray(Channel, CurrentLightData.DepthMap.DepthSamples);
	Swarm.CloseChannel(Channel);
}


class FStaticShadowDepthMap : public FTexture
{
	const FStaticShadowDepthMapData* Data = nullptr;
	virtual void InitRHI();
};

class ENGINE_API ULightComponent : public ULightComponentBase {
	FStaticShadowDepthMap StaticShadowDepthMap;
}

class UMapBuildDataRegistry : public UObject {
	TMap<FGuid, FLightComponentMapBuildData> LightBuildData;
	
	FLightComponentMapBuildData* GetLightBuildData(FGuid LightId)
	{
		return LightBuildData.Find(LightId);
	}
}

const FLightComponentMapBuildData* ULightComponent::GetLightComponentMapBuildData() const
{
	ULevel* OwnerLevel = GetOwner()->GetLevel();

	if (OwnerLevel && OwnerLevel->MapBuildData)
	{
		UMapBuildDataRegistry* MapBuildData = OwnerLevel->MapBuildData;
		if (MapBuildData)
		{
			return MapBuildData->GetLightBuildData(LightGuid);
		}
	}
	return NULL;
}

void ULightComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	UWorld* World = GetWorld();
	const bool bHidden = !ShouldComponentAddToScene() || !ShouldRender() || Intensity <= 0.f;
	if (!bHidden)
	{
		InitializeStaticShadowDepthMap();
		World->Scene->AddLight(this);
		bAddedToSceneVisible = true;
	}
	// Add invisible stationary lights to the scene in the editor
	// Even invisible stationary lights consume a shadowmap channel so they must be included in the stationary light overlap preview
	else if (GIsEditor 
		&& !World->IsGameWorld()
		&& CastShadows 
		&& CastStaticShadows 
		&& Mobility == EComponentMobility::Stationary)
	{
		InitializeStaticShadowDepthMap();
		World->Scene->AddInvisibleLight(this);
	}
}

void ULightComponent::InitializeStaticShadowDepthMap()
{
	if (Mobility == EComponentMobility::Stationary)
	{
		const FStaticShadowDepthMapData* DepthMapData = NULL;
		const FLightComponentMapBuildData* ShadowBuildData = GetLightComponentMapBuildData();
	
		if (ShadowBuildData)
		{
			DepthMapData = &ShadowBuildData->DepthMap;
		}

		FStaticShadowDepthMap* DepthMap = &StaticShadowDepthMap;
		ENQUEUE_RENDER_COMMAND(SetDepthMapData)(
			[DepthMap, DepthMapData](FRHICommandList& RHICmdList)
			{
				DepthMap->Data = DepthMapData;
				DepthMap->InitResource();
			});

		BeginInitResource(&StaticShadowDepthMap);
	}
}