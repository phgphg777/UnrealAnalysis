
class ENGINE_API FLightMap : private FDeferredCleanupInterface {
	TArray<FGuid> LightGuids;/** The GUIDs of lights which this light-map stores. */
}
class ENGINE_API FLightMap2D : public FLightMap {
	// ULightMapVirtualTexture2D *VirtualTexture = nullptr; /** The virtual textures containing the light-map data. */
	ULightMapTexture2D* Textures[2] = {nullptr, nullptr};/** The textures containing the {HQ,LQ} light-map data. */
	FVector4 ScaleVectors[4];/** A scale to apply to the coefficients. */
	FVector4 AddVectors[4];/** Bias value to apply to the coefficients. */
	FVector2D CoordinateScale;/** The scale which is applied to the light-map coordinates before sampling the light-map textures. */
	FVector2D CoordinateBias;/** The bias which is applied to the light-map coordinates before sampling the light-map textures. */

	ULightMapTexture2D* SkyOcclusionTexture = nullptr;
	ULightMapTexture2D* AOMaterialMaskTexture = nullptr;
	
	UShadowMapTexture2D* ShadowMapTexture = nullptr;
	FVector4 InvUniformPenumbraSize;/** Stores the inverse of the penumbra size, normalized.  Stores 1 to interpret the shadowmap as a shadow factor directly, instead of as a distance field. */
	bool bShadowChannelValid[4];/** Tracks which of the 4 channels has valid texture data. */
};

class ENGINE_API FShadowMap : public FShadowMap {
	TArray<FGuid> LightGuids;/** The GUIDs of lights which this shadow-map stores. */
}
class ENGINE_API FShadowMap2D : public FShadowMap {
	UShadowMapTexture2D* Texture;/** The texture which contains the shadow-map data. */
	FVector2D CoordinateScale;/** The scale which is applied to the shadow-map coordinates before sampling the shadow-map textures. */
	FVector2D CoordinateBias;/** The bias which is applied to the shadow-map coordinates before sampling the shadow-map textures. */
	bool bChannelValid[4];/** Tracks which of the 4 channels has valid texture data. */
	FVector4 InvUniformPenumbraSize;/** Stores the inverse of the penumbra size, normalized.  Stores 1 to interpret the shadowmap as a shadow factor directly, instead of as a distance field. */
};

class FLightCacheInterface {/** An interface to cached lighting for a specific mesh. */
	bool bGlobalVolumeLightmap = false;
	const FLightMap* LightMap = nullptr;
	const FShadowMap* ShadowMap = nullptr;
	const FLightmapResourceCluster* ResourceCluster = nullptr;
	FUniformBufferRHIRef PrecomputedLightingUniformBuffer;/** The uniform buffer holding mapping the lightmap policy resources. */
};
///////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////
class FLightMapInteraction {/**Information about an interaction between a light and a mesh.*/
	FVector4 HighQualityCoefficientScales[2];
	FVector4 HighQualityCoefficientAdds[2];
	const ULightMapTexture2D* HighQualityTexture = nullptr;
	const ULightMapTexture2D* SkyOcclusionTexture = nullptr;
	const ULightMapTexture2D* AOMaterialMaskTexture = nullptr;
	//const ULightMapVirtualTexture2D* VirtualTexture = nullptr;
	FVector4 LowQualityCoefficientScales[2];
	FVector4 LowQualityCoefficientAdds[2];
	const ULightMapTexture2D* LowQualityTexture = nullptr;
	bool bAllowHighQualityLightMaps;
	uint32 NumLightmapCoefficients;
	ELightMapInteractionType Type = LMIT_None;
	FVector2D CoordinateScale;
	FVector2D CoordinateBias;
};
FLightMapInteraction FLightCacheInterface::GetLightMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (bGlobalVolumeLightmap)
	{
		return FLightMapInteraction::GlobalVolume();
	}
	return LightMap ? LightMap->GetInteraction(InFeatureLevel) : FLightMapInteraction();
}
FLightMapInteraction FLightMap2D::GetInteraction(ERHIFeatureLevel::Type InFeatureLevel) const
{
	bool bHighQuality = TEXT("r.HighQualityLightMaps") != 0;
	int32 LightmapIndex = bHighQuality ? 0 : 1;

	bool bValidTextures = Textures[LightmapIndex] && Textures[LightmapIndex]->Resource;
	if (bValidTextures)
	{
		return FLightMapInteraction::Texture(Textures, SkyOcclusionTexture, AOMaterialMaskTexture, 
			ScaleVectors, AddVectors, CoordinateScale, CoordinateBias, bHighQuality);
	}
	return FLightMapInteraction();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////
class FShadowMapInteraction {/** Information about the static shadowing information for a primitive. */
	UShadowMapTexture2D* ShadowTexture = nullptr;
	//const ULightMapVirtualTexture2D* VirtualTexture = nullptr;
	FVector2D CoordinateScale;
	FVector2D CoordinateBias;
	bool bChannelValid[4] = {false,false,false,false};
	FVector4 InvUniformPenumbraSize = FVector4(0, 0, 0, 0);
	EShadowMapInteractionType Type = SMIT_None;
}
FShadowMapInteraction FLightCacheInterface::GetShadowMapInteraction(ERHIFeatureLevel::Type InFeatureLevel) const
{
	if (bGlobalVolumeLightmap)
	{
		return FShadowMapInteraction::GlobalVolume();
	}
	return ShadowMap ? ShadowMap->GetInteraction(InFeatureLevel) : FShadowMapInteraction();
}
FShadowMapInteraction FShadowMap2D::GetInteraction() const
{
	if (Texture)
	{
		return FShadowMapInteraction::Texture(Texture, CoordinateScale, CoordinateBias, bChannelValid, InvUniformPenumbraSize);
	}
	return FShadowMapInteraction();
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////////////
class ENGINE_API FStaticMeshSceneProxy : public FPrimitiveSceneProxy {
	class FLODInfo : public FLightCacheInterface {
	}
}

FStaticMeshSceneProxy::FStaticMeshSceneProxy(UStaticMeshComponent* InComponent, false)
{
	for(int32 LODIndex = 0;LODIndex < RenderData->LODResources.Num(); LODIndex++)
	{
		FLODInfo* NewLODInfo = new (LODs) FLODInfo(
			InComponent, 
			RenderData->LODVertexFactories, 
			LODIndex, 
			ClampedMinLOD, 
			RenderData->bLODsShareStaticLighting);
	}
}

FStaticMeshSceneProxy::FLODInfo::FLODInfo(
	const UStaticMeshComponent* InComponent, 
	const FStaticMeshVertexFactoriesArray& InLODVertexFactories, 
	int32 LODIndex, 
	int32 InClampedMinLOD, 
	bool bLODsShareStaticLighting)
{
	if (InComponent->LightmapType == ELightmapType::ForceVolumetric)
	{
		bGlobalVolumeLightmap = true;
	}

	if (LODIndex < InComponent->LODData.Num() && LODIndex >= InClampedMinLOD)
	{
		const FStaticMeshComponentLODInfo& ComponentLODInfo = InComponent->LODData[LODIndex];

		if (InComponent->LightmapType != ELightmapType::ForceVolumetric)
		{
			const FMeshMapBuildData* MeshMapBuildData = InComponent->GetMeshMapBuildData(ComponentLODInfo);
			if (MeshMapBuildData)
			{
				LightMap = MeshMapBuildData->LightMap;
				ShadowMap = MeshMapBuildData->ShadowMap;
				ResourceCluster = MeshMapBuildData->ResourceCluster;
				IrrelevantLights = MeshMapBuildData->IrrelevantLights;
			}
		}

		...
	}
	...
}

// Convert FStaticMeshComponentLODInfo to FMeshMapBuildData
FMeshMapBuildData* UStaticMeshComponent::GetMeshMapBuildData(const FStaticMeshComponentLODInfo& LODInfo)
{
	if (LODInfo.OverrideMapBuildData)
	{
		return LODInfo.OverrideMapBuildData.Get();
	}

	AActor* Owner = GetOwner();

	if (   GetOwner() 
		&& GetOwner()->GetLevel()
		&& GetOwner()->GetLevel()->MapBuildData)
	{
		UMapBuildDataRegistry* MapBuildData = GetOwner()->GetLevel()->MapBuildData;

		return MapBuildData->GetMeshBuildData(LODInfo.MapBuildDataId);
	}
	
	return NULL;
}




class ENGINE_API AActor : public UObject {
	void InvalidateLightingCache()
	{
		if (GIsEditor && !GIsDemoMode)
		{
			InvalidateLightingCacheDetailed(false);
		}
	}
	void InvalidateLightingCacheDetailed(bool bTranslationOnly)
	{
		if(GIsEditor && !GIsDemoMode)
		{
			for (UActorComponent* Component : GetComponents())
			{
				if (Component && Component->IsRegistered())
				{
					Component->InvalidateLightingCacheDetailed(true, bTranslationOnly);
				}
			}
		}
	}
}

class ENGINE_API UActorComponent : public UObject {
	void InvalidateLightingCache()
	{
		InvalidateLightingCacheDetailed(true, false);
	}
	virtual void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) {}
}

class ENGINE_API UPrimitiveComponent : public USceneComponent {
	int32 VisibilityId;/** Used for precomputed visibility */
	void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly) 
	{
		if (bInvalidateBuildEnqueuedLighting)
		{// If a static lighting build has been enqueued for this primitive, don't stomp on its visibility ID.
			VisibilityId = INDEX_NONE;
		}
	}
}

class ENGINE_API UStaticMeshComponent : public UMeshComponent {
	TArray<FStaticMeshComponentLODInfo> LODData;/** Contains static lighting data along with instanced mesh vertex colors. */

	void InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
	{
		Modify(true);// Save the static mesh state for transactions

		Super::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting, bTranslationOnly);

		for(int32 i = 0; i < LODData.Num(); i++)
		{
			LODData[i].MapBuildDataId.Invalidate(); // = 0;
		}

		MarkRenderStateDirty();
	}
}

class ENGINE_API ULightComponentBase : public USceneComponent {
	/**
	 * GUID used to associate a light component with precomputed shadowing information across levels.
	 * The GUID changes whenever the light position changes.
	 */
	FGuid LightGuid;
}
void ULightComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
{
	if (Mobility != EComponentMobility::Movable)
	{
		Modify();// Save the light state for transactions.
		BeginReleaseResource(&StaticShadowDepthMap);
		LightGuid = FGuid::NewGuid();
		MarkRenderStateDirty();
	} 
	else 
	{
		LightGuid.Invalidate(); // = 0;
	}

	if (GIsEditor
		&& GetWorld() != NULL
		&& Mobility == EComponentMobility::Stationary)
	{
		ReassignStationaryLightChannels(GetWorld(), false, NULL);
	}
}

void UDirectionalLightComponent::InvalidateLightingCacheDetailed(bool bInvalidateBuildEnqueuedLighting, bool bTranslationOnly)
{
	if (!bTranslationOnly)// Directional lights don't care about translation
	{
		Super::InvalidateLightingCacheDetailed(bInvalidateBuildEnqueuedLighting, bTranslationOnly);
	}
}

