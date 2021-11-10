class ENGINE_API ULightComponent;
class ENGINE_API FLightSceneProxy;
class FLightSceneInfo;
class FLightSceneInfoCompact;


bool ULightComponent::IsPrecomputedLightingValid() const
{
	return GetLightComponentMapBuildData() != NULL && HasStaticShadowing();
}

bool FLightSceneInfo::IsPrecomputedLightingValid() const
{
	// bPrecomputedLightingIsValid = Proxy->GetLightComponent()->IsPrecomputedLightingValid();
	return (bPrecomputedLightingIsValid && NumUnbuiltInteractions < GWholeSceneShadowUnbuiltInteractionThreshold) 
		|| Proxy->IsMovable();
}

bool ULightComponent::IsPrecomputedLightingValid() const
{
	return GetLightComponentMapBuildData() != NULL && !IsMovable();
}

/*
Indexed by same Id
FScene::Lights <-> FSceneRenderer::VisibleLightInfos <-> FViewInfo::VisibleLightInfos
*/
class FLightSceneInfo : public FRenderResource
{
	FLightPrimitiveInteraction* DynamicInteractionOftenMovingPrimitiveList;
	FLightPrimitiveInteraction* DynamicInteractionStaticPrimitiveList;
	
	FLightSceneProxy* Proxy;/** The light's scene proxy. */
	int32 Id;/** If bVisible == true, this is the index of the primitive in Scene->Lights. */
	int32 NumUnbuiltInteractions;/** Number of dynamic interactions with statically lit primitives. */
	bool bCreatePerObjectShadowsForDynamicObjects;/** Cached value from the light proxy's virtual function, since it is checked many times during shadow setup. */
}

FLightSceneInfo::FLightSceneInfo(FLightSceneProxy* InProxy, bool InbVisible)
	: DynamicInteractionOftenMovingPrimitiveList(NULL)
	, DynamicInteractionStaticPrimitiveList(NULL)
	, Proxy(InProxy)
	, Id(INDEX_NONE)
	, TileIntersectionResources(nullptr)
	, HeightFieldTileIntersectionResources(nullptr)
	, DynamicShadowMapChannel(-1)
	, bPrecomputedLightingIsValid(InProxy->GetLightComponent()->IsPrecomputedLightingValid())
	, bVisible(InbVisible)
	, bEnableLightShaftBloom(InProxy->GetLightComponent()->bEnableLightShaftBloom)
	, BloomScale(InProxy->GetLightComponent()->BloomScale)
	, BloomThreshold(InProxy->GetLightComponent()->BloomThreshold)
	, BloomMaxBrightness(InProxy->GetLightComponent()->BloomMaxBrightness)
	, BloomTint(InProxy->GetLightComponent()->BloomTint)
	, NumUnbuiltInteractions(0)
	, bCreatePerObjectShadowsForDynamicObjects(Proxy->ShouldCreatePerObjectShadowsForDynamicObjects())
	, Scene(InProxy->GetLightComponent()->GetScene()->GetRenderScene())
{
	BeginInitResource(this);
}

class FLightSceneInfoCompact {
	VectorRegister BoundingSphereVector;// XYZ: origin, W:sphere radius
	FLinearColor Color;
	FLightSceneInfo* LightSceneInfo;// must not be 0
	uint32 LightType : 2;
	uint32 bCastDynamicShadow : 1;
	uint32 bCastStaticShadow : 1;
	uint32 bStaticLighting : 1;
}

void FLightSceneInfoCompact::FLightSceneInfoCompact(FLightSceneInfo* InLightSceneInfo)
{
	LightSceneInfo = InLightSceneInfo;
	FSphere BoundingSphere = InLightSceneInfo->Proxy->GetBoundingSphere();
	BoundingSphere.W = BoundingSphere.W > 0.0f ? BoundingSphere.W : FLT_MAX;
	BoundingSphereVector = BoundingSphere;
	Color = InLightSceneInfo->Proxy->GetColor();
	LightType = InLightSceneInfo->Proxy->GetLightType();
	bCastDynamicShadow = InLightSceneInfo->Proxy->CastsDynamicShadow();
	bCastStaticShadow = InLightSceneInfo->Proxy->CastsStaticShadow();
	bStaticLighting = InLightSceneInfo->Proxy->HasStaticLighting();
}
////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////
void ULightComponent::CreateRenderState_Concurrent(FRegisterComponentContext* Context)
{
	Super::CreateRenderState_Concurrent(Context);

	if (bAffectsWorld)
	{
		UWorld* World = GetWorld();
		const bool bHidden = !ShouldComponentAddToScene() || !ShouldRender() || Intensity <= 0.f;
		if (!bHidden)
		{
			InitializeStaticShadowDepthMap();
			World->Scene->AddLight(this);
			bAddedToSceneVisible = true;
		}
	}
}

void FScene::AddLight(ULightComponent* Light)
{
	FLightSceneProxy* Proxy = Light->CreateSceneProxy();
	
	if(Proxy)
	{
		Light->SceneProxy = Proxy;
		Proxy->SetTransform(Light->GetComponentTransform().ToMatrixNoScale(), Light->GetLightPosition());
		Proxy->LightSceneInfo = new FLightSceneInfo(Proxy, true);

		++NumVisibleLights_GameThread;

		FScene* Scene = this;
		FLightSceneInfo* LightSceneInfo = Proxy->LightSceneInfo;

		ENQUEUE_RENDER_COMMAND(FAddLightCommand)(
		[Scene, LightSceneInfo](FRHICommandListImmediate& RHICmdList)
		{
			Scene->AddLightSceneInfo_RenderThread(LightSceneInfo);
		});
	}
}

void FScene::AddLightSceneInfo_RenderThread(FLightSceneInfo* LightSceneInfo)
{
	LightSceneInfo->Id = Lights.Add( FLightSceneInfoCompact(LightSceneInfo) );
	ProcessAtmosphereLightAddition_RenderThread(LightSceneInfo);
	LightSceneInfo->AddToScene();
}

void FLightSceneInfo::AddToScene()
{
	const FLightSceneInfoCompact& LightSceneInfoCompact = Scene->Lights[Id];

	// Only need to create light interactions for lights that can cast a shadow, As deferred shading doesn't need to know anything about the primitives that a light affects
	if ( Proxy->CastsDynamicShadow()
		|| Proxy->CastsStaticShadow()
		|| Proxy->HasStaticLighting() )// Lights that should be baked need to check for interactions to track unbuilt state correctly
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();
		
		if (LightSceneInfoCompact.LightType == LightType_Directional)
		{
			Scene->DirectionalShadowCastingLightIDs.Add(Id);

			for (FPrimitiveSceneInfo *PrimitiveSceneInfo : Scene->Primitives)// All primitives may interact with a directional light
			{
				CreateLightPrimitiveInteraction(LightSceneInfoCompact, PrimitiveSceneInfo);
			}
		}
		else
		{
			Scene->LocalShadowCastingLightOctree.AddElement(LightSceneInfoCompact);// Add the light to the scene's light octree.
			
			// Find primitives that the light affects in the primitive octree.
			for (FScenePrimitiveOctree::TConstElementBoxIterator<SceneRenderingAllocator> PrimitiveIt(Scene->PrimitiveOctree, GetBoundingBox()); PrimitiveIt.HasPendingElements(); PrimitiveIt.Advance())
			{
				CreateLightPrimitiveInteraction(LightSceneInfoCompact, PrimitiveIt.GetCurrentElement());
			}
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////
// Determines the relevance of this primitive's elements to the given light.
void FPrimitiveSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const
{
	bDynamic = true;		// The light is dynamic for this primitive
	bRelevant = true;		// The light is relevant for this primitive
	bLightMapped = false;	// The light is light mapped for this primitive
	bShadowMapped = false;
}

ELightInteractionType FStaticMeshSceneProxy::FLODInfo::GetInteractionType(const FLightSceneProxy* LightSceneProxy) const
{
	ELightInteractionType Ret;

	if (bGlobalVolumeLightmap) // UPrimitiveComponent::LightmapType == ELightmapType::ForceVolumetric
	{
		Ret = LightSceneProxy->HasStaticLighting() ? LIT_CachedLightMap :
			LightSceneProxy->HasStaticLighting() ? LIT_CachedSignedDistanceFieldShadowMap2D : LIT_Dynamic;
	}
	else
	{
		Ret = LIT_Dynamic;

		if(LightSceneProxy->HasStaticShadowing())
		{
			const FGuid LightGuid = LightSceneProxy->GetLightGuid();

			if(LightMap && LightMap->ContainsLight(LightGuid))
			{
				Ret = LIT_CachedLightMap;
			}
			else if(ShadowMap && ShadowMap->ContainsLight(LightGuid))
			{
				Ret = LIT_CachedSignedDistanceFieldShadowMap2D;
			}
			else if(IrrelevantLights.Contains(LightGuid))
			{
				Ret = LIT_CachedIrrelevant;
			}
		}
	}

	return Ret;
}

void FStaticMeshSceneProxy::GetLightRelevance(const FLightSceneProxy* LightSceneProxy, bool& bDynamic, bool& bRelevant, bool& bLightMapped, bool& bShadowMapped) const
{
	bRelevant = false;
	bDynamic = true;
	bLightMapped = true;
	bShadowMapped = true;

	check(LODs.Num() > 0);

	for (int32 LODIndex = 0; LODIndex < LODs.Num(); LODIndex++)
	{
		ELightInteractionType InteractionType = LODs[LODIndex].GetInteractionType(LightSceneProxy);

		if (InteractionType != LIT_CachedIrrelevant)
		{
			bRelevant = true;
		}

		if (InteractionType != LIT_Dynamic)
		{
			bDynamic = false;
		}

		if (InteractionType != LIT_CachedLightMap && InteractionType != LIT_CachedIrrelevant)
		{
			bLightMapped = false;
		}

		if (InteractionType != LIT_CachedSignedDistanceFieldShadowMap2D)
		{
			bShadowMapped = false;
		}
	}
}
////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////
void FLightSceneInfo::CreateLightPrimitiveInteraction(const FLightSceneInfoCompact& LightSceneInfoCompact, const FPrimitiveSceneInfoCompact& PrimitiveSceneInfoCompact)
{
	if(	LightSceneInfoCompact.AffectsPrimitive(PrimitiveSceneInfoCompact.Bounds, PrimitiveSceneInfoCompact.Proxy) )
	{
		FLightPrimitiveInteraction::Create(this, PrimitiveSceneInfoCompact.PrimitiveSceneInfo);
	}
}

void FLightPrimitiveInteraction::Create(FLightSceneInfo* LightSceneInfo, FPrimitiveSceneInfo* PrimitiveSceneInfo)
{
	bool bRelevant = false;
	bool bDynamic = true;
	bool bIsLightMapped = true;
	bool bShadowMapped = false;
	PrimitiveSceneInfo->Proxy->GetLightRelevance(LightSceneInfo->Proxy, bDynamic, bRelevant, bIsLightMapped, bShadowMapped);

	if (bRelevant && bDynamic)
	{
		const bool bTranslucentObjectShadow = LightSceneInfo->Proxy->CastsTranslucentShadows() && PrimitiveSceneInfo->Proxy->CastsVolumetricTranslucentShadow();
		const bool bInsetObjectShadow = LightSceneInfo->Proxy->GetLightType() == LightType_Directional && PrimitiveSceneInfo->Proxy->CastsInsetShadow();// Currently only supporting inset shadows on directional lights, but could be made to work with any whole scene shadows
		const bool bMovableDirectional = LightSceneInfo->Proxy->GetLightType() == LightType_Directional && LightSceneInfo->Proxy->IsMovable();
		// Movable directional lights determine shadow relevance dynamically based on the view and CSM settings. 
		if (!bMovableDirectional || bInsetObjectShadow || bTranslucentObjectShadow)
		{
			FLightPrimitiveInteraction* Interaction = new FLightPrimitiveInteraction(
				LightSceneInfo, 
				PrimitiveSceneInfo, 
				bDynamic, 		// true
				bIsLightMapped, // false, in case of StaticMesh
				bShadowMapped, 	// false, in case of StaticMesh
				bTranslucentObjectShadow, 
				bInsetObjectShadow);
		}
	}
}

class FLightPrimitiveInteraction {
	FLightSceneInfo* LightSceneInfo;/** The light which affects the primitive. */
	FPrimitiveSceneInfo* PrimitiveSceneInfo;/** The primitive which is affected by the light. */
	
	FLightPrimitiveInteraction** PrevPrimitiveLink;/** A pointer to the NextPrimitive member of the previous interaction in the light's interaction list. */
	FLightPrimitiveInteraction* NextPrimitive;/** The next interaction in the light's interaction list. */
	FLightPrimitiveInteraction** PrevLightLink;/** A pointer to the NextLight member of the previous interaction in the primitive's interaction list. */
	FLightPrimitiveInteraction* NextLight;/** The next interaction in the primitive's interaction list. */

	int32 LightId;/** The index into Scene->Lights of the light which affects the primitive. */
	uint32 bCastShadow : 1;/** True if the primitive casts a shadow from the light. */
	uint32 bLightMapped : 1;/** True if the primitive has a light-map containing the light. */
	uint32 bIsDynamic : 1;/** True if the interaction is dynamic. */
	uint32 bIsShadowMapped : 1;/** Whether the light's shadowing is contained in the primitive's static shadow map. */
	uint32 bUncachedStaticLighting : 1;/** True if the interaction is an uncached static lighting interaction. */
	uint32 bHasTranslucentObjectShadow : 1;/** True if the interaction has a translucent per-object shadow. */
	uint32 bHasInsetObjectShadow : 1;/** True if the interaction has an inset per-object shadow. */
	uint32 bSelfShadowOnly : 1;/** True if the primitive only shadows itself. */
	uint32 bMobileDynamicPointLight : 1;/** True this is an ES2 dynamic point light interaction. */
};

class FPrimitiveSceneProxy
{
	uint8 bGoodCandidateForCachedShadowmap = true;
	bool IsMovable() const 
		{ return Mobility == EComponentMobility::Movable || Mobility == EComponentMobility::Stationary; }
	bool IsOftenMoving() const 
		{ return Mobility == EComponentMobility::Movable; }
	bool IsMeshShapeOftenMoving() const 
		{ return Mobility == EComponentMobility::Movable || !bGoodCandidateForCachedShadowmap; }
}
FStaticMeshSceneProxy::FStaticMeshSceneProxy()
{
	bGoodCandidateForCachedShadowmap = 
		TEXT("r.Shadow.CacheWPOPrimitives") || !MaterialRelevance.bUsesWorldPositionOffset;
}

FLightPrimitiveInteraction::FLightPrimitiveInteraction(
	FLightSceneInfo* InLightSceneInfo,
	FPrimitiveSceneInfo* InPrimitiveSceneInfo,
	bool bInIsDynamic = true,
	bool bInLightMapped = false,
	bool bInIsShadowMapped = false,
	bool bInHasTranslucentObjectShadow,
	bool bInHasInsetObjectShadow
	) :
	LightSceneInfo(InLightSceneInfo),
	PrimitiveSceneInfo(InPrimitiveSceneInfo),
	LightId(InLightSceneInfo->Id),
	bLightMapped(bInLightMapped),
	bIsDynamic(bInIsDynamic),
	bIsShadowMapped(bInIsShadowMapped),
	bUncachedStaticLighting(false),
	bHasTranslucentObjectShadow(bInHasTranslucentObjectShadow),
	bHasInsetObjectShadow(bInHasInsetObjectShadow),
	bSelfShadowOnly(false),
	bMobileDynamicPointLight(false)
{
	// Determine whether this light-primitive interaction produces a shadow.
	if(PrimitiveSceneInfo->Proxy->HasStaticLighting())
	{
		const bool bHasStaticShadow =
			LightSceneInfo->Proxy->HasStaticShadowing() &&
			LightSceneInfo->Proxy->CastsStaticShadow() &&
			PrimitiveSceneInfo->Proxy->CastsStaticShadow();
		const bool bHasDynamicShadow =
			!LightSceneInfo->Proxy->HasStaticLighting() &&
			LightSceneInfo->Proxy->CastsDynamicShadow() &&
			PrimitiveSceneInfo->Proxy->CastsDynamicShadow();
		bCastShadow = bHasStaticShadow || bHasDynamicShadow;
	}
	else
	{
		bCastShadow = LightSceneInfo->Proxy->CastsDynamicShadow() && PrimitiveSceneInfo->Proxy->CastsDynamicShadow();
	}

	if(bCastShadow)
	{
		if (PrimitiveSceneInfo->Proxy->HasStaticLighting()
			&& PrimitiveSceneInfo->Proxy->CastsStaticShadow()
			&& PrimitiveSceneInfo->Proxy->GetLightmapType() != ELightmapType::ForceSurface
			&& (LightSceneInfo->Proxy->HasStaticLighting() || (LightSceneInfo->Proxy->HasStaticShadowing() && !bInIsShadowMapped)))
		{
			bUncachedStaticLighting = true;

			if (!GUnbuiltPreviewShadowsInGame && !InLightSceneInfo->Scene->IsEditorScene())
			{
				bCastShadow = false;
			}

			LightSceneInfo->NumUnbuiltInteractions++;
			FPlatformAtomics::InterlockedIncrement(&PrimitiveSceneInfo->Scene->NumUncachedStaticLightingInteractions);
			PrimitiveSceneInfo->Proxy->NumUncachedStaticLightingInteractions++;
		}
	}

	bSelfShadowOnly = PrimitiveSceneInfo->Proxy->CastsSelfShadowOnly();
	FlushCachedShadowMapData();

	// Add the interaction to the light's interaction list.
	PrevPrimitiveLink = PrimitiveSceneInfo->Proxy->IsMeshShapeOftenMoving() 
		? &LightSceneInfo->DynamicInteractionOftenMovingPrimitiveList 	// Movable Primitive
		: &LightSceneInfo->DynamicInteractionStaticPrimitiveList;		// Static && Stationary Primitive
	NextPrimitive = *PrevPrimitiveLink;
	if(*PrevPrimitiveLink)
	{
		(*PrevPrimitiveLink)->PrevPrimitiveLink = &NextPrimitive;
	}
	*PrevPrimitiveLink = this;

	// Add the interaction to the primitives' interaction list.
	PrevLightLink = &PrimitiveSceneInfo->LightList;
	NextLight = *PrevLightLink;
	if(*PrevLightLink)
	{
		(*PrevLightLink)->PrevLightLink = &NextLight;
	}
	*PrevLightLink = this;
}

void FLightPrimitiveInteraction::FlushCachedShadowMapData()
{
	if (LightSceneInfo && PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->Scene)
	{
		if (bCastShadow && !PrimitiveSceneInfo->Proxy->IsMeshShapeOftenMoving())
		{
			FCachedShadowMapData* CachedShadowMapData = PrimitiveSceneInfo->Scene->CachedShadowMaps.Find(LightSceneInfo->Id);

			if (CachedShadowMapData)
			{
				CachedShadowMapData->ShadowMap.Release();
			}
		}
	}
}