// TranslucentShadow is PerObjectShadow!!

class FProjectedShadowInfo : public FRefCountedObject {
public:
	FViewInfo* ShadowDepthView;
	TUniformBufferRef<FShadowDepthPassUniformParameters> ShadowDepthPassUniformBuffer;
	
	FShadowMapRenderTargets RenderTargets;/** The depth or color targets this shadow was rendered to. */
	EShadowDepthCacheMode CacheMode;
	FViewInfo* DependentView;/** The main view this shadow must be rendered in, or NULL for a view independent shadow. */
	int32 ShadowId;/** Index of the shadow into FVisibleLightInfo::AllProjectedShadows. */
	/*
	FVector PreShadowTranslation;
	FMatrix ShadowViewMatrix;
	FMatrix SubjectAndReceiverMatrix;
	FMatrix ReceiverMatrix;
	FMatrix InvReceiverMatrix;
	float InvMaxSubjectDepth;
	float MaxSubjectZ;
	float MinSubjectZ;
	FConvexVolume CasterFrustum;
	FConvexVolume ReceiverFrustum;
	float MinPreSubjectZ;
	*/
	FSphere ShadowBounds;
	FShadowCascadeSettings CascadeSettings;

	uint32 X;/** X and Y position of the shadow in the appropriate depth buffer.  These are only initialized after the shadow has been allocated. */
	uint32 Y;/** The actual contents of the shadowmap are at X + BorderSize, Y + BorderSize. */
	uint32 ResolutionX;/** Resolution of the shadow, excluding the border. */
	uint32 ResolutionY;/** The full size of the region allocated to this shadow is therefore ResolutionX + 2 * BorderSize, ResolutionY + 2 * BorderSize. */
	uint32 BorderSize;/** Size of the border, if any, used to allow filtering without clamping for shadows stored in an atlas. */
	float MaxScreenPercent;/** The largest percent of either the width or height of any view. */
	TArray<float, TInlineAllocator<2> > FadeAlphas;/** Fade Alpha per view. */
	uint32 bAllocated : 1;/** Whether the shadow has been allocated in the shadow depth buffer, and its X and Y properties have been initialized. */
	uint32 bRendered : 1;/** Whether the shadow's projection has been rendered. */
	uint32 bAllocatedInPreshadowCache : 1;/** Whether the shadow has been allocated in the preshadow cache, so its X and Y properties offset into the preshadow cache depth buffer. */	
	uint32 bDepthsCached : 1;/** Whether the shadow is in the preshadow cache and its depths are up to date. */
	uint32 bDirectionalLight : 1;// redundant to LightSceneInfo->Proxy->GetLightType() == LightType_Directional, could be made ELightComponentType LightType
	uint32 bOnePassPointLightShadow : 1;/** Whether the shadow is a point light shadow that renders all faces of a cubemap in one pass. */
	uint32 bWholeSceneShadow : 1;/** Whether this shadow affects the whole scene or only a group of objects. */
	uint32 bTranslucentShadow : 1;/** Whether this shadow should support casting shadows from translucent surfaces. */
	uint32 bRayTracedDistanceField : 1;/** Whether the shadow will be computed by ray tracing the distance field. */
	uint32 bCapsuleShadow : 1;/** Whether this is a per-object shadow that should use capsule shapes to shadow instead of the mesh's triangles. */
	uint32 bPreShadow : 1;/** Whether the shadow is a preshadow or not.  A preshadow is a per object shadow that handles the static environment casting on a dynamic receiver. */
	uint32 bSelfShadowOnly : 1;/** To not cast a shadow on the ground outside the object and having higher quality (useful for first person weapon). */
	uint32 bPerObjectOpaqueShadow : 1;/** Whether the shadow is a per object shadow or not. */
	/*
	uint32 bTransmission : 1;
	uint32 bHairStrandsDeepShadow : 1;
	TArray<FMatrix> OnePassShadowViewProjectionMatrices;
	TArray<FMatrix> OnePassShadowViewMatrices;
	TArray<FConvexVolume> OnePassShadowFrustums;
	float PerObjectShadowFadeStart;
	float InvPerObjectShadowFadeLength;
	*/
private:
	const FLightSceneInfo* LightSceneInfo;
	FLightSceneInfoCompact LightSceneInfoCompact;
	const FPrimitiveSceneInfo* ParentSceneInfo;/* Parent primitive of the shadow group that created this shadow, if not a bWholeSceneShadow. */

	PrimitiveArrayType DynamicSubjectPrimitives;/** dynamic shadow casting elements */
	PrimitiveArrayType ReceiverPrimitives;/** For preshadows, this contains the receiver primitives to mask the projection to. */
	PrimitiveArrayType SubjectTranslucentPrimitives;/** Subject primitives with translucent relevance. */

	TArray<FMeshBatchAndRelevance> DynamicSubjectMeshElements;/** Dynamic mesh elements for subject primitives. */
	TArray<FMeshBatchAndRelevance> DynamicSubjectTranslucentMeshElements;/** Dynamic mesh elements for translucent subject primitives. */
	TArray<const FStaticMeshBatch*> SubjectMeshCommandBuildRequests;

	int32 NumDynamicSubjectMeshElements;/** Number of elements of DynamicSubjectMeshElements meshes. */
	int32 NumSubjectMeshCommandBuildRequestElements;/** Number of elements of SubjectMeshCommandBuildRequests meshes. */

	FMeshCommandOneFrameArray ShadowDepthPassVisibleCommands;
	FParallelMeshDrawCommandPass ShadowDepthPass;

	/*
	TArray<FShadowMeshDrawCommandPass, TInlineAllocator<2>> ProjectionStencilingPasses;
	FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
	FGraphicsMinimalPipelineStateSet GraphicsMinimalPipelineStateSet;
	bool NeedsShaderInitialisation;
	*/
	
	/*
	float ShaderDepthBias;
	float ShaderSlopeDepthBias;
	float ShaderMaxSlopeDepthBias;
	*/
}

class FVisibleLightInfo {	
	TArray<FProjectedShadowInfo*> AllProjectedShadows;
	TArray<FProjectedShadowInfo*> MemStackProjectedShadows;
	//TArray<FProjectedShadowInfo*> ShadowsToProject;
	//TArray<FProjectedShadowInfo*> CapsuleShadowsToProject;
	//TArray<FProjectedShadowInfo*> RSMsToProject;
	TArray<TRefCountPtr<FProjectedShadowInfo>> ProjectedPreShadows;
	TArray<FProjectedShadowInfo*> OccludedPerObjectShadows;
};

class FVisibleLightViewInfo {
	uint32 bInViewFrustum;
	TArray<FPrimitiveViewRelevance> ProjectedShadowViewRelevanceMap;
	FSceneBitArray ProjectedShadowVisibilityMap;
	TArray<FPrimitiveSceneInfo*> VisibleDynamicLitPrimitives;
	//FMobileCSMSubjectPrimitives MobileCSMSubjectPrimitives;
};

class FScene : public FSceneInterface {
	TMap<int32, FCachedShadowMapData> CachedShadowMaps;
	TRefCountPtr<IPooledRenderTarget> PreShadowCacheDepthZ;
	TArray<TRefCountPtr<FProjectedShadowInfo> > CachedPreshadows;
	FTextureLayout PreshadowCacheLayout;
}

class FSceneRenderer {
	TArray<FViewInfo> Views;
	TArray<FVisibleLightInfo> VisibleLightInfos; // VisibleLightInfos.Num() == Scene->Lights.Num()
	FSortedShadowMaps SortedShadowsForShadowDepthPass;
}

class FViewInfo : public FSceneView {
	TArray<FVisibleLightViewInfo> VisibleLightInfos; // VisibleLightInfos.Num() == Scene->Lights.Num()
}

struct FSortedShadowMapAtlas {
	FShadowMapRenderTargetsRefCounted RenderTargets;
	TArray<FProjectedShadowInfo*> Shadows;
};

struct FSortedShadowMaps {
	TArray<FSortedShadowMapAtlas> ShadowMapAtlases; // CSM + [Spot(Movable primitives only) + PerObject] + Spot(Static primitives only)
	TArray<FSortedShadowMapAtlas> ShadowMapCubemaps;
	FSortedShadowMapAtlas PreshadowCache;
	TArray<FSortedShadowMapAtlas> TranslucencyShadowMapAtlases;
}


float CalculateShadowFadeAlpha(const float x, const uint32 FadeOneRes, const uint32 FadeZeroRes)
{
	assume(FadeZeroRes < FadeOneRes);

	if (x < FadeZeroRes)
	{
		return 0.0f
	}	
	if (FadeOneRes < x)
	{
		return 1.0f
	}

	const float Exponent = TEXT("r.Shadow.FadeExponent");	
	const float y = 				FMath::Pow( (x - FadeStartRes)  / (FadeOneRes - FadeZeroRes), Exponent );
	const float y_FadeStartRes_1 =  FMath::Pow( 1.0f 				/ (FadeOneRes - FadeZeroRes), Exponent ); // y value When x = FadeStartRes + 1
	
	// Rescale the fade alpha to reduce the change between no fading and the first value, which reduces popping with small ShadowFadeExponent's
	return (y - y_FadeStartRes_1) / (1.0f - y_FadeStartRes_1);
}

int32 FSceneRenderTargets::GetCubeShadowDepthZResolution(int32 ShadowResolution) const
{
	int32 MaxResolution = FMath::Min(TEXT("r.Shadow.MaxResolution"), 16384);
	MaxResolution /= 2;// Use a lower resolution because cubemaps use a lot of memory

	const int32 SurfaceSizes[5] = {		// In default,
		MaxResolution,					// 1024
		MaxResolution / 2,				// 512
		MaxResolution / 4,				// 256
		MaxResolution / 8,				// 128
		TEXT("r.Shadow.MinResolution")	// 32
	};

	for (int32 SearchIndex = 0; SearchIndex < 5; SearchIndex++)
	{
		if (ShadowResolution >= SurfaceSizes[SearchIndex])
		{
			return SurfaceSizes[SearchIndex];
		}
	}
}

void BuildLightViewFrustumConvexHull(const FVector& LightOrigin, const FConvexVolume& Frustum, FConvexVolume& ConvexHull)
{
	const int32 EdgeCount = 8;
	const int32 PlaneCount = 5;
	check(Frustum.Planes.Num() == PlaneCount);

	enum EFrustumPlanes {
		FLeft,
		FRight,
		FTop,
		FBottom,
		FFar
	};

	const EFrustumPlanes Edges[EdgeCount][2] = {
		{ FFar  , FLeft },{ FFar  , FRight  },
		{ FFar  , FTop }, { FFar  , FBottom },
		{ FLeft , FTop }, { FLeft , FBottom },
		{ FRight, FTop }, { FRight, FBottom }
	};

	float Distance[PlaneCount];
	bool  Visible[PlaneCount];

	for (int32 PlaneIndex = 0; PlaneIndex < PlaneCount; ++PlaneIndex)
	{
		const FPlane& Plane = Frustum.Planes[PlaneIndex];
		float Dist = Plane.PlaneDot(LightOrigin);
		bool bVisible = Dist < 0.0f;

		Distance[PlaneIndex] = Dist;
		Visible[PlaneIndex] = bVisible;

		if (bVisible)
		{
			ConvexHull.Planes.Add(Plane);
		}
	}

	for (int32 EdgeIndex = 0; EdgeIndex < EdgeCount; ++EdgeIndex)
	{
		EFrustumPlanes I1 = Edges[EdgeIndex][0];
		EFrustumPlanes I2 = Edges[EdgeIndex][1];

		if (Visible[I1] != Visible[I2])
		{
			// Add plane that passes through edge and light origin (How!!!)
			FPlane Plane = Frustum.Planes[I1] * Distance[I2] - Frustum.Planes[I2] * Distance[I1];
			if (Visible[I2])
			{
				Plane = Plane.Flip();
			}
			ConvexHull.Planes.Add(Plane);
		}
	}

	ConvexHull.Init();
}


bool FLocalLightSceneProxy::GetWholeSceneProjectedShadowInitializer(const FSceneViewFamily& ViewFamily, TArray<FWholeSceneProjectedShadowInitializer, TInlineAllocator<6> >& OutInitializers) const
{
	OutInitializer.PreShadowTranslation = -GetLightToWorld().GetOrigin();
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector(0), FVector(Radius), Radius);
}

bool FSpotLightSceneProxy::GetWholeSceneProjectedShadowInitializer(const FSceneViewFamily& ViewFamily, TArray<FWholeSceneProjectedShadowInitializer, TInlineAllocator<6> >& OutInitializers) const
{
	OutInitializer.PreShadowTranslation = -GetLightToWorld().GetOrigin();
	const FSphere AbsoluteBoundingSphere = FSpotLightSceneProxy::GetBoundingSphere();
	OutInitializer.SubjectBounds = FBoxSphereBounds(
		AbsoluteBoundingSphere.Center - GetOrigin(), FVector(AbsoluteBoundingSphere.W), AbsoluteBoundingSphere.W);
}

bool FDirectionalLightSceneProxy::GetViewDependentWholeSceneProjectedShadowInitializer(const FSceneView& View, int32 InCascadeIndex, bool bPrecomputedLightingIsValid, FWholeSceneProjectedShadowInitializer& OutInitializer) const override
{
	FSphere Bounds = GetShadowSplitBounds(View, InCascadeIndex, bPrecomputedLightingIsValid, &OutInitializer.CascadeSettings);
	OutInitializer.PreShadowTranslation = -Bounds.Center;
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, FVector(Bounds.W/FMath::Sqrt(3.0f)), Bounds.W);
	return true;
}

void FProjectedShadowInfo::SetupWholeSceneProjection(
	FLightSceneInfo* InLightSceneInfo,
	FViewInfo* InDependentView,
	const FWholeSceneProjectedShadowInitializer& Initializer,
	uint32 InResolutionX,
	uint32 InResolutionY,
	uint32 InBorderSize,
	bool bInReflectiveShadowMap)
{
	ShadowBounds = FSphere(-Initializer.PreShadowTranslation, Initializer.SubjectBounds.SphereRadius);
}


bool FLocalLightSceneProxy::GetPerObjectProjectedShadowInitializer(const FBoxSphereBounds& PrimBounds, FPerObjectProjectedShadowInitializer& OutInitializer) const
{
	// Use a perspective projection looking at the primitive from the light position.
	FVector LightPosition = GetOrigin();
	FVector LightVector = PrimBounds.Origin - LightPosition;
	float LightDistance = LightVector.Size();
	const float SubjectRadius = PrimBounds.BoxExtent.Size();
	const float ShadowRadiusMultiplier = 1.1f;

	if (LightDistance <= SubjectRadius * ShadowRadiusMultiplier)
	{
		LightVector = SubjectRadius * LightVector.GetSafeNormal() * ShadowRadiusMultiplier;
		LightPosition = (PrimBounds.Origin - LightVector);
		LightDistance = SubjectRadius * ShadowRadiusMultiplier;
	}

	OutInitializer.PreShadowTranslation = -LightPosition;
	OutInitializer.SubjectBounds = FBoxSphereBounds(PrimBounds.Origin - LightPosition, PrimBounds.BoxExtent, PrimBounds.SphereRadius);
}

bool FDirectionalLightSceneProxy::GetPerObjectProjectedShadowInitializer(const FBoxSphereBounds& PrimBounds, FPerObjectProjectedShadowInitializer& OutInitializer) const override
{
	OutInitializer.PreShadowTranslation = -PrimBounds.Origin;
	OutInitializer.SubjectBounds = FBoxSphereBounds(FVector::ZeroVector, PrimBounds.BoxExtent, PrimBounds.SphereRadius);
}

bool FProjectedShadowInfo::SetupPerObjectProjection(
	FLightSceneInfo* InLightSceneInfo,
	const FPrimitiveSceneInfo* InParentSceneInfo,
	const FPerObjectProjectedShadowInitializer& Initializer,
	bool bInPreShadow,
	uint32 InResolutionX,
	uint32 MaxShadowResolutionY,
	uint32 InBorderSize,
	float InMaxScreenPercent,
	bool bInTranslucentShadow)
{
	// Finally, ShadowBounds.Center == PrimBounds.Origin (world space coordinates)
	ShadowBounds = FSphere(Initializer.SubjectBounds.Origin - Initializer.PreShadowTranslation, Initializer.SubjectBounds.SphereRadius);
}



void FSceneRenderer::AddViewDependentWholeSceneShadowsForView(
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfos, 
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& ShadowInfosThatNeedCulling,
	FVisibleLightInfo& VisibleLightInfo, 
	const FLightSceneInfo& LightSceneInfo)
{
	assume(Views.Num() == 1);
	FViewInfo& View = Views[0];

	const bool bExtraDistanceFieldCascade = LightSceneInfo.Proxy->ShouldCreateRayTracedCascade(, 
		LightSceneInfo.IsPrecomputedLightingValid(), View.MaxShadowCascades);// View.MaxShadowCascades = Math::Min(TEXT("r.Shadow.CSM.MaxCascades"), 10);
	const int32 ProjectionCount = 
		LightSceneInfo.Proxy->GetNumViewDependentWholeSceneShadows(View, LightSceneInfo.IsPrecomputedLightingValid()) 
		+ (bExtraDistanceFieldCascade?1:0);

	for (int32 Index = 0; Index < ProjectionCount; Index++)
	{
		FWholeSceneProjectedShadowInitializer ProjectedShadowInitializer;
		int32 LocalIndex = Index;

		if(bExtraDistanceFieldCascade && LocalIndex + 1 == ProjectionCount)
		{
			LocalIndex = INDEX_NONE;
		}

		if (LightSceneInfo.Proxy->GetViewDependentWholeSceneProjectedShadowInitializer(View, LocalIndex, LightSceneInfo.IsPrecomputedLightingValid(), ProjectedShadowInitializer))
		{
			int32 ShadowBufferRes = FMath::Min(TEXT("r.Shadow.MaxCSMResolution"), 2048);
			
			FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get()) FProjectedShadowInfo;
			ProjectedShadowInfo->SetupWholeSceneProjection(
				&LightSceneInfo,
				&View,
				ProjectedShadowInitializer,
				ShadowBufferRes - SHADOW_BORDER * 2,
				ShadowBufferRes - SHADOW_BORDER * 2,
				SHADOW_BORDER, /*RSM = false*/);
			ProjectedShadowInfo->FadeAlphas = {LightSceneInfo.Proxy->GetShadowAmount(), };

			ShadowInfos.Add(ProjectedShadowInfo);
			if (!ProjectedShadowInfo->bRayTracedDistanceField)
			{
				ShadowInfosThatNeedCulling.Add(ProjectedShadowInfo);
			}

			VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);
			VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
		}
	}
}

void FSceneRenderer::CreateWholeSceneProjectedShadow(
	FLightSceneInfo* LightSceneInfo,
	uint32& InOutNumPointShadowCachesUpdatedThisFrame,
	uint32& InOutNumSpotShadowCachesUpdatedThisFrame)
{
	FWholeSceneProjectedShadowInitializer ProjectedShadowInitializer;
	if (!LightSceneInfo->Proxy->GetWholeSceneProjectedShadowInitializer(ViewFamily, ProjectedShadowInitializer))
		return;
	
	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
	const FViewInfo& View = Views[0];

	const uint32 ShadowBorder = ProjectedShadowInitializers[0].bOnePassPointLightShadow ? 0 : SHADOW_BORDER;
	const uint32 ShadowBufferResolution  = FMath::Min(TEXT("r.Shadow.MaxResolution"), 16384);
	const uint32 MaxShadowResolution 	 = ShadowBufferResolution - ShadowBorder * 2;
	const uint32 MinShadowResolution     = TEXT("r.Shadow.MinResolution");
	const uint32 ShadowFadeResolution    = TEXT("r.Shadow.FadeResolution");

	const float ScreenRadius = LightSceneInfo->Proxy->GetEffectiveScreenRadius(View.ShadowViewMatrices);

	float RawDesiredResolution = ScreenRadius;
	switch (LightSceneInfo->Proxy->GetLightType())
	{
	case LightType_Point: 	RawDesiredResolution *= TEXT("r.Shadow.TexelsPerPixelPointlight");
	case LightType_Spot: 	RawDesiredResolution *= TEXT("r.Shadow.TexelsPerPixelSpotlight");
	case LightType_Rect:	RawDesiredResolution *= TEXT("r.Shadow.TexelsPerPixelRectlight");
	}

	const float FadeAlpha = CalculateShadowFadeAlpha( RawDesiredResolution, ShadowFadeResolution, MinShadowResolution );
	const float ShadowResolutionScale = LightSceneInfo->Proxy->GetShadowResolutionScale();

	float DesiredResolution = RawDesiredResolution;
	{
		if (ShadowResolutionScale > 1.0f)
		{
			DesiredResolution *= ShadowResolutionScale;
		}

		DesiredResolution = FMath::Min<float>(DesiredResolution, MaxShadowResolution);

		if (ShadowResolutionScale <= 1.0f)
		{
			DesiredResolution *= ShadowResolutionScale;
		}
	}
	DesiredResolution = FMath::Max<float>(DesiredResolution, MinShadowResolution);

	if (FadeAlpha > 1.0f / 256.0f)
	{
		Scene->FlushAsyncLightPrimitiveInteractionCreation();

		// FMath::CeilLogTwo(DesiredResolution + 1) garantee that CachedShadowRes > DesiredResolution when moving away
		int32 SizeX = DesiredResolution < MaxShadowResolution ? 1 << (FMath::CeilLogTwo(DesiredResolution + 1) - 1) - ShadowBorder * 2 : MaxShadowResolution;
		
		if (ProjectedShadowInitializer.bOnePassPointLightShadow)
		{
			SizeX = SceneContext.GetCubeShadowDepthZResolution(DesiredResolution);// Round to a resolution that is supported for one pass point light shadows
		}

		int32 NumShadowMaps;
		EShadowDepthCacheMode CacheMode[2];

		if (!ProjectedShadowInitializer.bRayTracedDistanceField)
		{
			int32 ShadowMapSize = SizeX + ShadowBorder * 2;

			ComputeWholeSceneShadowCacheModes(
				/*In*/LightSceneInfo,
				/*In*/ProjectedShadowInitializer.bOnePassPointLightShadow,
				/*In*/ViewFamily.CurrentRealTime,
				/*In*/DesiredResolution,
				/*In*/MaxShadowResolution,
				/*InOut*/Scene,
				/*InOut*/ProjectedShadowInitializer,
				/*InOut*/ShadowMapSize,
				/*InOut*/InOutNumPointShadowCachesUpdatedThisFrame,
				/*InOut*/InOutNumSpotShadowCachesUpdatedThisFrame,
				/*Out*/NumShadowMaps,
				/*Out*/CacheMode);

			SizeX = ShadowMapSize - ShadowBorder * 2;
		}

		for (int32 CacheModeIndex = 0; CacheModeIndex < NumShadowMaps; CacheModeIndex++)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get()) FProjectedShadowInfo;

			ProjectedShadowInfo->SetupWholeSceneProjection(
				LightSceneInfo,
				NULL,
				ProjectedShadowInitializer,
				SizeX,
				SizeX,
				ShadowBorder,
				false	// no RSM
				);
			ProjectedShadowInfo->CacheMode = CacheMode[CacheModeIndex];
			ProjectedShadowInfo->FadeAlphas = FadeAlphas;

			if (ProjectedShadowInitializer.bOnePassPointLightShadow)
			{
				...
			}

			if (!ProjectedShadowInfo->bRayTracedDistanceField)
			{
				assume(TEXT("r.Shadow.LightViewConvexHullCull") == 1);
				assume(TEXT("r.Shadow.CachedShadowsCastFromMovablePrimitives") || LightSceneInfo->Proxy->GetForceCachedShadowsForMovablePrimitives());

				FConvexVolume* LightViewFrustumConvexHull = nullptr;
				if (CacheMode[CacheModeIndex] != SDCM_StaticPrimitivesOnly)
				{
					LightViewFrustumConvexHull = new FConvexVolume;
					BuildLightViewFrustumConvexHull(LightSceneInfo->Proxy->GetOrigin(), Views[0].ViewFrustum, *LightViewFrustumConvexHull);
				}

				if (CacheMode[CacheModeIndex] != SDCM_StaticPrimitivesOnly)
				{
					for (FLightPrimitiveInteraction* Interaction : LightSceneInfo->GetDynamicInteractionOftenMovingPrimitiveList(false))
					{
						if (Interaction->HasShadow() && !Interaction->CastsSelfShadowOnly())
						{
							FBoxSphereBounds& Bounds = Interaction->GetPrimitiveSceneInfo()->Proxy->GetBounds();
							
							if (!LightViewFrustumConvexHull || LightViewFrustumConvexHull->IntersectBox(Bounds.Origin, Bounds.BoxExtent))
							{
								ProjectedShadowInfo->AddSubjectPrimitive(Interaction->GetPrimitiveSceneInfo(), &Views, FeatureLevel, false);
							}
						}
					}
				}

				if (CacheMode[CacheModeIndex] != SDCM_MovablePrimitivesOnly)
				{
					for (FLightPrimitiveInteraction* Interaction : LightSceneInfo->GetDynamicInteractionStaticPrimitiveList(false))
					{
						if (Interaction->HasShadow() && !Interaction->CastsSelfShadowOnly())
						{
							FBoxSphereBounds& Bounds = Interaction->GetPrimitiveSceneInfo()->Proxy->GetBounds();
							
							if (!LightViewFrustumConvexHull || LightViewFrustumConvexHull->IntersectBox(Bounds.Origin, Bounds.BoxExtent))
							{
								ProjectedShadowInfo->AddSubjectPrimitive(Interaction->GetPrimitiveSceneInfo(), &Views, FeatureLevel, false);
							}
						}
					}
				}
			}

			bool bRenderShadow = true;
			if (CacheMode[CacheModeIndex] == SDCM_StaticPrimitivesOnly)
			{
				bRenderShadow = Scene->CachedShadowMaps.FindChecked(LightSceneInfo.Id).bCachedShadowMapHasPrimitives
					= ProjectedShadowInfo->HasSubjectPrims();
			}

			if (bRenderShadow)
			{
				VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
			}
			VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);
		}
	}
}

void ComputeWholeSceneShadowCacheModes(
	const FLightSceneInfo* LightSceneInfo,
	bool bCubeShadowMap, ,
	float DesiredResolution,
	uint32 MaxShadowResolution,
	FScene* Scene,
	FWholeSceneProjectedShadowInitializer& InOutProjectedShadowInitializer,
	int32& InOutShadowMapSize,
	uint32& InOutNumPointShadowCachesUpdatedThisFrame,
	uint32& InOutNumSpotShadowCachesUpdatedThisFrame,
	int32& OutNumShadowMaps, 
	EShadowDepthCacheMode* OutCacheModes)
{
	uint32* NumCachesUpdatedThisFrame = LightSceneInfo->Proxy->GetLightType() == LightType_Spot
		? &InOutNumSpotShadowCachesUpdatedThisFrame
		: &InOutNumPointShadowCachesUpdatedThisFrame;

	OutNumShadowMaps = 1;
	
	if (!TEXT("r.Shadow.CacheWholeSceneShadows"))
	{
		Scene->CachedShadowMaps.Remove(LightSceneInfo->Id);
		OutCacheModes[0] = SDCM_Uncached;
		return;
	}
	
	FCachedShadowMapData* CachedShadowMapData = Scene->CachedShadowMaps.Find(LightSceneInfo->Id);
	int32 CachedShadowMapRes = CachedShadowMapData->ShadowMap.IsValid() ? CachedShadowMapData->ShadowMap.GetSize().X : 0;
	int64 TotalCachedMemorySize = Scene->GetCachedWholeSceneShadowMapsSize();

	if (!CachedShadowMapData)
	{
		if (TotalCachedMemorySize < TEXT("r.Shadow.WholeSceneShadowCacheMb") * 1024 * 1024)
		{
			OutNumShadowMaps = 2;
			OutCacheModes[0] = SDCM_StaticPrimitivesOnly;// ShadowMap with static primitives rendered first so movable shadowmap can composite
			OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
			Scene->CachedShadowMaps.Add(LightSceneInfo->Id, FCachedShadowMapData(InOutProjectedShadowInitializer, RealTime));
			++*NumCachesUpdatedThisFrame;
		}
		else
		{
			OutCacheModes[0] = SDCM_Uncached;
		}
		return;
	}

	CachedShadowMapData->LastUsedTime = RealTime;

	if ( InOutProjectedShadowInitializer != CachedShadowMapData->Initializer )
	{
		CachedShadowMapData->Initializer = InOutProjectedShadowInitializer;
		CachedShadowMapData->ShadowMap.Release();
		OutCacheModes[0] = SDCM_Uncached;
		return;
	}

	if ( CachedShadowMapRes == InOutShadowMapSize )
	{
		OutCacheModes[0] = SDCM_MovablePrimitivesOnly;
		return;
	}

	if (TotalCachedMemorySize >= TEXT("r.Shadow.WholeSceneShadowCacheMb") * 1024 * 1024)
	{
		CachedShadowMapData->ShadowMap.Release();
		OutCacheModes[0] = SDCM_Uncached;
		return;
	}

	OutNumShadowMaps = 2;
	OutCacheModes[0] = SDCM_StaticPrimitivesOnly;
	OutCacheModes[1] = SDCM_MovablePrimitivesOnly;
	++*NumCachesUpdatedThisFrame;
	
	assume(CachedShadowMapRes < MaxShadowResolution);
	assume(TEXT("r.Shadow.MaxNumPointShadowCacheUpdatesPerFrame") == -1);
	assume(TEXT("r.Shadow.MaxNumSpotShadowCacheUpdatesPerFrame") == -1);

	if (InOutShadowMapSize < CachedShadowMapRes)
	{
		assume(CachedShadowMapRes - DesiredResolution > 0);
		float DropRatio = (CachedShadowMapRes - DesiredResolution) / (CachedShadowMapRes - InOutShadowMapSize);

		if (DropRatio < 0.5f)// Fallback to existing shadow cache
		{
			InOutShadowMapSize = CachedShadowMapRes;
			OutNumShadowMaps = 1;
			OutCacheModes[0] = SDCM_MovablePrimitivesOnly;
			--*NumCachesUpdatedThisFrame;
		}
	}
}

void FSceneRenderer::InitDynamicShadows(FRHICommandListImmediate& RHICmdList, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	const bool bProjectEnablePointLightShadows = TEXT("r.SupportPointLightWholeSceneShadows");

	uint32 NumPointShadowCachesUpdatedThisFrame = 0;
	uint32 NumSpotShadowCachesUpdatedThisFrame = 0;
	TArray<FProjectedShadowInfo*> PreShadows;
	TArray<FProjectedShadowInfo*> ViewDependentWholeSceneShadows;
	TArray<FProjectedShadowInfo*> ViewDependentWholeSceneShadowsThatNeedCulling;
	
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

		if (LightSceneInfoCompact.bCastStaticShadow || LightSceneInfoCompact.bCastDynamicShadow)
		{
			bool bIsVisibleInAnyView = false;

			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				bIsVisibleInAnyView = LightSceneInfo->ShouldRenderLight(Views[ViewIndex]);

				if (bIsVisibleInAnyView) 
				{
					break;
				}
			}

			if (bIsVisibleInAnyView)
			{
				const bool bPointLightShadow = LightSceneInfoCompact.LightType == LightType_Point || 
											   LightSceneInfoCompact.LightType == LightType_Rect;

				const bool bCreateShadowForMovableLight = // Only create whole scene shadows for lights that don't precompute shadowing (movable lights)
					LightSceneInfo->Proxy->IsMovable() 
					&& LightSceneInfoCompact.bCastDynamicShadow;
					&& (!bPointLightShadow || bProjectEnablePointLightShadows);

				const bool bCreateShadowToPreviewStaticLight = // Also create a whole scene shadow for lights with precomputed shadows that are unbuilt
					!LightSceneInfo->Proxy->IsMovable() 
					&& LightSceneInfoCompact.bCastStaticShadow 
					&& !LightSceneInfo->IsPrecomputedLightingValid();
					&& (!bPointLightShadow || bProjectEnablePointLightShadows);

				const bool bCreateShadowForOverflowStaticShadowing = // Create a whole scene shadow for lights that want static shadowing but didn't get assigned to a valid shadowmap channel due to overlap
					LightSceneInfo->Proxy->IsStationary()
					&& LightSceneInfoCompact.bCastStaticShadow
					&& LightSceneInfo->IsPrecomputedLightingValid() && LightSceneInfo->Proxy->GetShadowMapChannel() == INDEX_NONE;
					&& (!bPointLightShadow || bProjectEnablePointLightShadows);

				if (bCreateShadowForMovableLight || bCreateShadowToPreviewStaticLight || bCreateShadowForOverflowStaticShadowing)
				{
					if(LightSceneInfo->Proxy->GetLightType() != LightType_Directional)
					{
						CreateWholeSceneProjectedShadow(LightSceneInfo, , );
					}
				}

				// 1. Movable && bCastDynamicShadow
				// 2. Static && (bCastStaticShadow && !IsPrecomputedLightingValid)
				// 3. Stationary && [bCastDynamicShadow || (bCastStaticShadow && !IsPrecomputedLightingValid)]
				if ((!LightSceneInfo->Proxy->HasStaticLighting() && LightSceneInfoCompact.bCastDynamicShadow) || bCreateShadowToPreviewStaticLight)
				{
					if(LightSceneInfo->Proxy->GetLightType() == LightType_Directional)
					{
						AddViewDependentWholeSceneShadowsForView(ViewDependentWholeSceneShadows, ViewDependentWholeSceneShadowsThatNeedCulling, VisibleLightInfo, *LightSceneInfo);
					}
					
					Scene->FlushAsyncLightPrimitiveInteractionCreation();

					FLightPrimitiveInteraction* Interaction;
					for ( Interaction = LightSceneInfo->DynamicInteractionOftenMovingPrimitiveList; Interaction; ++Interaction )
					{
						SetupInteractionShadows(RHICmdList, Interaction, VisibleLightInfo, bStaticSceneOnly, ViewDependentWholeSceneShadows, PreShadows);
					}

					for ( Interaction = LightSceneInfo->DynamicInteractionStaticPrimitiveList; Interaction; ++Interaction )
					{
						SetupInteractionShadows(RHICmdList, Interaction, VisibleLightInfo, bStaticSceneOnly, ViewDependentWholeSceneShadows, PreShadows);
					}
					
				}
			}
		}
	}

	/*
	Out: FViewInfo::VisibleLightInfos[LightIdx].ProjectedShadowViewRelevanceMap
	Out: FViewInfo::VisibleLightInfos[LightIdx].ProjectedShadowVisibilityMap
	Out: FProjectedShadowInfo::ShadowId
	*/
	InitProjectedShadowVisibility(RHICmdList);

	/*
	Out: FScene::PreshadowCacheLayout
	Out: FScene::CachedPreshadows
	Out: FProjectedShadowInfo::[X, Y, bAllocated, bAllocatedInPreshadowCache]
	*/
	UpdatePreshadowCache(FSceneRenderTargets::Get(RHICmdList));

	/*
	Out: FProjectedShadowInfo::ShadowDepthPassVisibleCommands
	Out: FProjectedShadowInfo::SubjectMeshCommandBuildRequests
	Out: FProjectedShadowInfo::NumSubjectMeshCommandBuildRequestElements
	Out: FProjectedShadowInfo::DynamicSubjectPrimitives
	*/
	GatherShadowPrimitives(PreShadows, ViewDependentWholeSceneShadowsThatNeedCulling, bStaticSceneOnly);

	/*
	Out: FSceneRenderer::VisibleLightInfos[LightIdx].ShadowsToProject
	Out: FSceneRenderer::SortedShadowsForShadowDepthPass
	Out: FProjectedShadowInfo::[X, Y, bAllocated, RenderTargets.DepthTarget, ShadowDepthView]
	Out: FScene::CachedShadowMaps[LightIdx].ShadowMap
	Out: FScene::PreShadowCacheDepthZ
	*/
	AllocateShadowDepthTargets(RHICmdList);

	// Generate mesh element arrays from shadow primitive arrays
	GatherShadowDynamicMeshElements(DynamicIndexBuffer, DynamicVertexBuffer, DynamicReadBuffer);

}

bool FLightSceneProxy::ShouldCreatePerObjectShadowsForDynamicObjects() const
{
	// Only create per-object shadows for Stationary lights, which use static shadowing from the world and therefore need a way to integrate dynamic objects
	return HasStaticShadowing() && !HasStaticLighting();
}

bool FDirectionalLightSceneProxy::ShouldCreatePerObjectShadowsForDynamicObjects() const override
{
	return FLightSceneProxy::ShouldCreatePerObjectShadowsForDynamicObjects()
		&& WholeSceneDynamicShadowRadius < TEXT("r.MaxCSMRadiusToAllowPerObjectShadows")
		&& bUseInsetShadowsForMovableObjects;
}
bool ShouldCreateObjectShadowForStationaryLight(
	const FLightSceneInfo* LightSceneInfo, 
	const FPrimitiveSceneProxy* PrimitiveSceneProxy, 
	bool bInteractionShadowMapped) 
{
	const ValidStationaryLight = LightSceneInfo->IsPrecomputedLightingValid() && LightSceneInfo->Proxy->GetShadowMapChannel() != INDEX_NONE;
	const ShadowMappedPrimitive = PrimitiveSceneProxy->HasStaticLighting() && bInteractionShadowMapped;
	return ValidStationaryLight 
		&& LightSceneInfo->Proxy->ShouldCreatePerObjectShadowsForDynamicObjects()
		&& !ShadowMappedPrimitive;
}

void FSceneRenderer::SetupInteractionShadows(
	FRHICommandListImmediate& RHICmdList,
	const FLightPrimitiveInteraction* Interaction, 
	FVisibleLightInfo& VisibleLightInfo, 
	bool bStaticSceneOnly,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& PreShadows)
{
	assume(!bStaticSceneOnly);
	assume(GUseTranslucencyShadowDepths);
	assume(!bShadowHandledByParent);

	FPrimitiveSceneInfo* PrimitiveSceneInfo = Interaction->GetPrimitiveSceneInfo();
	const bool bCreateTranslucentObjectShadow = Interaction->HasTranslucentObjectShadow();
	const bool bCreateInsetObjectShadow = Interaction->HasInsetObjectShadow(); //LightSceneInfo->Proxy->GetLightType() == LightType_Directional && PrimitiveSceneInfo->Proxy->CastsInsetShadow()
	const bool bCreateObjectShadowForStationaryLight = ShouldCreateObjectShadowForStationaryLight(Interaction->GetLight(), PrimitiveSceneInfo->Proxy, Interaction->IsShadowMapped());

	// 1. Valid Statinary Local Light
	// 2. InsetShadowsForMovableObjects-On Valid Statinary Directional Light
	// 3. Directional Light & InsetShadow-On Primitive
	// 4. TranslucentShadow-On Light & TranslucentShadow-On Primitive
	if (Interaction->HasShadow() 
		&& (bCreateObjectShadowForStationaryLight || bCreateInsetObjectShadow || bCreateTranslucentObjectShadow))
	{
		CreatePerObjectProjectedShadow(RHICmdList, Interaction, bCreateTranslucentObjectShadow, bCreateInsetObjectShadow || bCreateObjectShadowForStationaryLight, ViewDependentWholeSceneShadows, PreShadows);
	}
}

void FSceneRenderer::CreatePerObjectProjectedShadow(
	FRHICommandListImmediate& RHICmdList,
	FLightPrimitiveInteraction* Interaction, 
	bool bCreateTranslucentObjectShadow, 
	bool bCreateOpaqueObjectShadow,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& OutPreShadows)
{
	FPrimitiveSceneInfo* PrimitiveSceneInfo = Interaction->GetPrimitiveSceneInfo();
	const int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();

	FLightSceneInfo* LightSceneInfo = Interaction->GetLight();
	FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];

	assume(Views.Num() == 1);
	assume(NumBufferedFrames == 1);
	assume(!View.bIgnoreExistingQueries && View.State);

	const FViewInfo& View = Views[0];
	FPrimitiveViewRelevance ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];
	if (!ViewRelevance.bInitializedThisFrame)
	{
		ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(&View);
	}
		
	const bool bPrimitiveIsShadowRelevant = ViewRelevance.bShadowRelevance;
	const bool bOpaqueShadowIsVisible = bCreateOpaqueObjectShadow && !View.State->IsShadowOccluded(, OpaqueKey,);
	const bool bTranslucentShadowIsVisible = bCreateTranslucentObjectShadow && !View.State->IsShadowOccluded(, TranslucentKey,);

	bool bSubjectIsVisible = (PrimitiveSceneInfo->Proxy->ShouldRenderInMainPass() && View.PrimitiveVisibilityMap[PrimitiveSceneInfo->GetIndex()]);
	bool bOpaque = ViewRelevance.bOpaque;
	bool bTranslucentRelevance = ViewRelevance.HasTranslucency();
	bool bOpaqueShadowIsVisibleThisFrame = (bPrimitiveIsShadowRelevant && bOpaqueShadowIsVisible);
	bool bTranslucentShadowIsVisibleThisFrame = (bPrimitiveIsShadowRelevant && bTranslucentShadowIsVisible);
	bool bShadowIsPotentiallyVisibleNextFrame = bPrimitiveIsShadowRelevant;
	
	if (!bShadowIsPotentiallyVisibleNextFrame)
	{
		return;
	}

	assume(!PrimitiveSceneInfo->Proxy->bLightAttachmentsAsGroup);
	const uint32 ShadowBufferResolution  = FMath::Min(TEXT("r.Shadow.MaxResolution"), 16384);
	const uint32 MaxShadowResolution 	 = ShadowBufferResolution - SHADOW_BORDER * 2;
	const uint32 MinShadowResolution     = TEXT("r.Shadow.MinResolution");
	const uint32 ShadowFadeResolution    = TEXT("r.Shadow.FadeResolution");
	const uint32 MinPreShadowResolution  = TEXT("r.Shadow.MinPreShadowResolution");
	const uint32 PreShadowFadeResolution = TEXT("r.Shadow.PreShadowFadeResolution");
	
	FBoxSphereBounds OriginalBounds = PrimitiveSceneInfo->Proxy->GetBounds();
	float ShadowViewDistFromBounds = (OriginalBounds.Origin - View.ViewMatrices.GetViewOrigin()).Size();
	float ScreenRadius = View.ShadowViewMatrices.GetScreenScale() * OriginalBounds.SphereRadius / FMath::Max(ShadowViewDistFromBounds, 1.0f);
	
	float RawDesiredResolution = ScreenRadius * TEXT("r.Shadow.TexelsPerPixel");
	float FadeAlpha = CalculateShadowFadeAlpha(RawDesiredResolution, ShadowFadeResolution, MinShadowResolution) * LightSceneInfo->Proxy->GetShadowAmount();
	float PreShadowFadeAlpha = CalculateShadowFadeAlpha(RawDesiredResolution * TEXT("r.Shadow.PreShadowResolutionFactor"), PreShadowFadeResolution, MinPreShadowResolution) * LightSceneInfo->Proxy->GetShadowAmount();
	float ShadowResolutionScale = LightSceneInfo->Proxy->GetShadowResolutionScale();

	float DesiredResolution = RawDesiredResolution;
	{
		if (ShadowResolutionScale > 1.0f)
		{
			DesiredResolution *= ShadowResolutionScale;
		}

		DesiredResolution = FMath::Min<float>(DesiredResolution, MaxShadowResolution);

		if (ShadowResolutionScale <= 1.0f)
		{
			DesiredResolution *= ShadowResolutionScale;
		}
	}
	DesiredResolution = FMath::Max<float>(DesiredResolution, MinShadowResolution);

	float ScreenPercent = FMath::Max(
		1.0f / 2.0f * View.ShadowViewMatrices.GetProjectionScale().X,
		1.0f / 2.0f * View.ShadowViewMatrices.GetProjectionScale().Y ) * OriginalBounds.SphereRadius / ShadowViewDistFromBounds;

	check(!Interaction->IsShadowMapped());
	FBoxSphereBounds Bounds = OriginalBounds;

	bool bRenderPreShadow = 
		TEXT("r.Shadow.Preshadows") == 1 
		&& bOpaque
		&& bSubjectIsVisible 
		&& !LightSceneInfo->Proxy->IsMovable()
		&& !(PrimitiveSceneInfo->Proxy->HasStaticLighting() && Interaction->IsShadowMapped())
		&& !(PrimitiveSceneInfo->Proxy->UseSingleSampleShadowFromStationaryLights() && LightSceneInfo->Proxy->GetLightType() == LightType_Directional);

	if (bRenderPreShadow && TEXT("r.Shadow.CachePreshadow") == 1)
	{
		float PreshadowExpandFraction = TEXT("r.Shadow.PreshadowExpand");
		Bounds.SphereRadius += (Bounds.BoxExtent * PreshadowExpandFraction).Size();
		Bounds.BoxExtent *= PreshadowExpandFraction + 1.0f;
	}

	FPerObjectProjectedShadowInitializer ShadowInitializer;
	LightSceneInfo->Proxy->GetPerObjectProjectedShadowInitializer(Bounds, ShadowInitializer);

	if (TEXT("r.Shadow.PerObject") == 1 && FadeAlpha > 1.0f / 256.0f)
	{
		int32 Resolution = DesiredResolution < MaxShadowResolution ? 1 << (FMath::CeilLogTwo(DesiredResolution) - 1) : MaxShadowResolution;

		if (bOpaque && bCreateOpaqueObjectShadow && bShadowIsPotentiallyVisibleNextFrame)
		{
			FProjectedShadowInfo* ProjectedShadowInfo = new(FMemStack::Get(),1,16) FProjectedShadowInfo;

			if(ProjectedShadowInfo->SetupPerObjectProjection(
				LightSceneInfo,
				PrimitiveSceneInfo,
				ShadowInitializer,
				false,	// no preshadow
				Resolution,
				MaxShadowResolution,
				SHADOW_BORDER,
				ScreenPercent,
				false))	// no translucent shadow
			{
				ProjectedShadowInfo->bPerObjectOpaqueShadow = true;
				ProjectedShadowInfo->FadeAlphas = { FadeAlpha, };
				
				if (bOpaqueShadowIsVisibleThisFrame)
				{
					ProjectedShadowInfo->AddSubjectPrimitive(PrimitiveSceneInfo, &Views, FeatureLevel, false);
					VisibleLightInfo.AllProjectedShadows.Add(ProjectedShadowInfo);
				}
				else
				{
					VisibleLightInfo.OccludedPerObjectShadows.Add(ProjectedShadowInfo);
				}

				VisibleLightInfo.MemStackProjectedShadows.Add(ProjectedShadowInfo);
			}
		}

		if (bTranslucentRelevance && bCreateTranslucentObjectShadow && bShadowIsPotentiallyVisibleNextFrame)
		{
			...
		}
	}

	if (bRenderPreShadow && PreShadowFadeAlpha > 1.0f / 256.0f)
	{
		int32 PreshadowSizeX = 1 << ( FMath::CeilLogTwo(TEXT("r.Shadow.PreShadowResolutionFactor") * DesiredResolution) - 1 );

		TRefCountPtr<FProjectedShadowInfo> ProjectedPreShadowInfo = GetCachedPreshadow(
			Interaction, , 
			OriginalBounds, 
			PreshadowSizeX);

		if(!ProjectedPreShadowInfo)
		{
			ProjectedPreShadowInfo = new FProjectedShadowInfo;// Not using the scene rendering mem stack because this shadow info may need to persist for multiple frames if it gets cached

			if( false == ProjectedPreShadowInfo->SetupPerObjectProjection(
				LightSceneInfo,
				PrimitiveSceneInfo,
				ShadowInitializer,
				true,	// preshadow
				PreshadowSizeX,
				FMath::TruncToInt(MaxShadowResolutionY * CVarPreShadowResolutionFactor.GetValueOnRenderThread()),
				SHADOW_BORDER,
				ScreenPercent,
				false))	// not translucent shadow
			{
				goto PRESHADOW_END;	
			}
		}

		ProjectedPreShadowInfo->FadeAlphas = { PreShadowFadeAlpha, };
		ProjectedPreShadowInfo->ReceiverPrimitives.Add(PrimitiveSceneInfo);

		VisibleLightInfo.AllProjectedShadows.Add(ProjectedPreShadowInfo);
		VisibleLightInfo.ProjectedPreShadows.Add(ProjectedPreShadowInfo);

		// Only add to OutPreShadows if the preshadow doesn't already have depths cached, Since OutPreShadows is used to generate information only used when rendering the shadow depths.
		if (!ProjectedPreShadowInfo->bDepthsCached && ProjectedPreShadowInfo->CasterFrustum.PermutedPlanes.Num())
		{
			OutPreShadows.Add(ProjectedPreShadowInfo);
		}
	}
PRESHADOW_END:
}


void FSceneRenderer::InitProjectedShadowVisibility(FRHICommandListImmediate& RHICmdList)
{
	FViewInfo& View = Views[0];

	// Initialize the views' ProjectedShadowVisibilityMaps and remove shadows without subjects.
	for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights);LightIt;++LightIt)
	{
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightIt.GetIndex()];
		FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];

		FVisibleLightViewInfo& VisibleLightViewInfo = Views[0].VisibleLightInfos[LightIt.GetIndex()];
		VisibleLightViewInfo.ProjectedShadowVisibilityMap.Init(false, VisibleLightInfo.AllProjectedShadows.Num());
		VisibleLightViewInfo.ProjectedShadowViewRelevanceMap.AddZeroed(VisibleLightInfo.AllProjectedShadows.Num());
	
		for( int32 ShadowIndex=0; ShadowIndex < VisibleLightInfo.AllProjectedShadows.Num(); ShadowIndex++ )
		{
			FProjectedShadowInfo& ProjectedShadowInfo = *VisibleLightInfo.AllProjectedShadows[ShadowIndex];
			ProjectedShadowInfo.ShadowId = ShadowIndex;

			// In FSceneRenderer::InitDynamicShadows(), if LightSceneInfo->ShouldRenderLight() return false, any shadows don't added.
			check(VisibleLightViewInfo.bInViewFrustum);
			
			FPrimitiveViewRelevance ViewRelevance;
			if(ProjectedShadowInfo.GetParentSceneInfo()) // PerObjectShadow & PreShadow
			{
				ViewRelevance = ProjectedShadowInfo.GetParentSceneInfo()->Proxy->GetViewRelevance(&View);
			}
			else // WholeSceneShadow
			{
				ViewRelevance.bDrawRelevance = 
				ViewRelevance.bStaticRelevance = 
				ViewRelevance.bDynamicRelevance = 
				ViewRelevance.bShadowRelevance = true;
			}							
			
			// In FSceneRenderer::CreatePerObjectProjectedShadow(), if ViewRelevance.bShadowRelevance == false, we returned.
			check(ViewRelevance.bShadowRelevance); 

			assume(NumBufferedFrames == 1);
			assume(!View.bIgnoreExistingQueries && View.State);
			bool bShadowIsOccluded = View.State->IsShadowOccluded(, FSceneViewState::FProjectedShadowKey(ProjectedShadowInfo),);
			
			VisibleLightViewInfo.ProjectedShadowVisibilityMap[ShadowIndex] = !bShadowIsOccluded;
			VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowIndex] = ViewRelevance;
		}
	}
}

void FSceneRenderer::GatherShadowPrimitives(
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& PreShadows,
	const TArray<FProjectedShadowInfo*,SceneRenderingAllocator>& ViewDependentWholeSceneShadows,
	bool bStaticSceneOnly
	)
{
	if (PreShadows.Num()==0 && ViewDependentWholeSceneShadows.Num()==0)
		return;

	assume(TEXT("r.Shadow.UseOctreeForCulling") == 0);
	const int32 PacketSize = TEXT("r.ParallelGatherNumPrimitivesPerPacket");
	const int32 NumPackets = FMath::DivideAndRoundUp(Scene->Primitives.Num(), PacketSize);

	TArray<FGatherShadowPrimitivesPacket*> Packets;
	Packets.Reserve(NumPackets);
	for (int32 PacketIndex = 0; PacketIndex < NumPackets; PacketIndex++)
	{
		const int32 StartPrimitiveIndex = PacketIndex * PacketSize;
		const int32 NumPrimitives = FMath::Min(PacketSize, Scene->Primitives.Num() - StartPrimitiveIndex);
		FGatherShadowPrimitivesPacket* Packet = new(FMemStack::Get()) FGatherShadowPrimitivesPacket(,, 
			NULL, 
			StartPrimitiveIndex, 
			NumPrimitives, 
			PreShadows, 
			ViewDependentWholeSceneShadows,,);
		Packets.Add(Packet);
	}

	// FilterPrimitivesForShadows;
	ParallelFor(Packets.Num(), 
		[&Packets](int32 Index)
		{
			Packets[Index]->AnyThreadTask();
		},
		TEXT("r.ParallelGatherShadowPrimitives") == 0 );

	// RenderThreadFinalize;
	for (int32 PacketIndex = 0; PacketIndex < Packets.Num(); PacketIndex++)
	{
		FGatherShadowPrimitivesPacket* Packet = Packets[PacketIndex];
		Packet->RenderThreadFinalize();
		Packet->~FGatherShadowPrimitivesPacket();// Class was allocated on the memstack which does not call destructors
	}
}

struct FGatherShadowPrimitivesPacket {
	// Inputs
	const FScene* Scene;
	TArray<FViewInfo>& Views;
	const FScenePrimitiveOctree::FNode* Node;
	int32 StartPrimitiveIndex;
	int32 NumPrimitives;
	const TArray<FProjectedShadowInfo*>& PreShadows;
	const TArray<FProjectedShadowInfo*>& ViewDependentWholeSceneShadows;

	// Outputs
	TArray< TArray<FPrimitiveSceneInfo*> > PreShadowSubjectPrimitives;
	TArray< TArray<FPrimitiveSceneInfo*> > ViewDependentWholeSceneShadowSubjectPrimitives;
}

FGatherShadowPrimitivesPacket::FGatherShadowPrimitivesPacket(,,
	const FScenePrimitiveOctree::FNode* InNode,
	int32 InStartPrimitiveIndex,
	int32 InNumPrimitives,
	const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& InPreShadows,
	const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& InViewDependentWholeSceneShadows,,) 
	: , 
	, Node(InNode)
	, StartPrimitiveIndex(InStartPrimitiveIndex)
	, NumPrimitives(InNumPrimitives)
	, PreShadows(InPreShadows)
	, ViewDependentWholeSceneShadows(InViewDependentWholeSceneShadows), ,
{
	PreShadowSubjectPrimitives.Empty(PreShadows.Num());
	PreShadowSubjectPrimitives.AddDefaulted(PreShadows.Num());

	ViewDependentWholeSceneShadowSubjectPrimitives.Empty(ViewDependentWholeSceneShadows.Num());
	ViewDependentWholeSceneShadowSubjectPrimitives.AddDefaulted(ViewDependentWholeSceneShadows.Num());
}

void FGatherShadowPrimitivesPacket::AnyThreadTask()
{
	for (int32 PrimitiveIndex = StartPrimitiveIndex; PrimitiveIndex < StartPrimitiveIndex + NumPrimitives; PrimitiveIndex++)
	{
		FPrimitiveFlagsCompact PrimitiveFlagsCompact = Scene->PrimitiveFlagsCompact[PrimitiveIndex];

		if (PrimitiveFlagsCompact.bCastDynamicShadow)
		{
			FilterPrimitiveForShadows(
				Scene->PrimitiveBounds[PrimitiveIndex].BoxSphereBounds, 
				PrimitiveFlagsCompact, 
				Scene->Primitives[PrimitiveIndex], 
				Scene->PrimitiveSceneProxies[PrimitiveIndex]);
		}
	}
}

void FGatherShadowPrimitivesPacket::FilterPrimitiveForShadows(
	const FBoxSphereBounds& PrimitiveBounds, 
	FPrimitiveFlagsCompact PrimitiveFlagsCompact, 
	FPrimitiveSceneInfo* PrimitiveSceneInfo, 
	FPrimitiveSceneProxy* PrimitiveProxy)
{
	if (PreShadows.Num() && PrimitiveFlagsCompact.bCastStaticShadow && PrimitiveFlagsCompact.bStaticLighting)
	{
		for (int32 ShadowIndex = 0, PreShadows.Num(); ShadowIndex < PreShadows.Num(); ShadowIndex++)
		{
			const FProjectedShadowInfo* ProjectedShadowInfo = PreShadows[ShadowIndex];
			bool bInFrustum = ProjectedShadowInfo->CasterFrustum.IntersectBox(PrimitiveBounds.Origin, ProjectedShadowInfo->PreShadowTranslation, PrimitiveBounds.BoxExtent);

			if (bInFrustum && ProjectedShadowInfo->GetLightSceneInfoCompact().AffectsPrimitive(PrimitiveBounds, PrimitiveProxy))
			{
				PreShadowSubjectPrimitives[ShadowIndex].Add(PrimitiveSceneInfo);
			}
		}
	}

	for (int32 ShadowIndex = 0;ShadowIndex < ViewDependentWholeSceneShadows.Num(); ShadowIndex++)
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = ViewDependentWholeSceneShadows[ShadowIndex];
		const FLightSceneInfo& LightSceneInfo = ProjectedShadowInfo->GetLightSceneInfo();
		const FLightSceneProxy& LightProxy = *LightSceneInfo.Proxy;

		if(!ProjectedShadowInfo->GetLightSceneInfoCompact().AffectsPrimitive(PrimitiveBounds, PrimitiveProxy))
		{
			continue;
		}
		if(LightProxy.HasStaticLighting() && LightSceneInfo.IsPrecomputedLightingValid())
		{
			continue;
		}
		if(PrimitiveProxy->CastsInsetShadow()) // Exclude cases that just InsetShadow-On Primitive
		{
			continue;
		}
		if(ShouldCreateObjectShadowForStationaryLight(&LightSceneInfo, PrimitiveProxy, true)) // Exclude cases that Movable Primitive & InsetForMovable-On stationary light
		{
			continue;
		}

		const FVector LightDirection = LightProxy.GetDirection();
		const FVector PrimitiveToShadowCenter = ProjectedShadowInfo->ShadowBounds.Center - PrimitiveBounds.Origin;
		const float ProjectedDistanceFromShadowOriginAlongLightDir = PrimitiveToShadowCenter | LightDirection;
		const float PrimitiveDistanceFromCylinderAxisSq = (-LightDirection * ProjectedDistanceFromShadowOriginAlongLightDir + PrimitiveToShadowCenter).SizeSquared();
		const float CombinedRadiusSq = FMath::Square(ProjectedShadowInfo->ShadowBounds.W + PrimitiveBounds.SphereRadius);

		if (PrimitiveDistanceFromCylinderAxisSq < CombinedRadiusSq
			&& (ProjectedDistanceFromShadowOriginAlongLightDir >= 0 || PrimitiveToShadowCenter.SizeSquared() < CombinedRadiusSq)
			&& ProjectedShadowInfo->CascadeSettings.ShadowBoundsAccurate.IntersectBox(PrimitiveBounds.Origin, PrimitiveBounds.BoxExtent))
		{
			const float Distance = (PrimitiveBounds.Origin - ProjectedShadowInfo->DependentView->ShadowViewMatrices.GetViewOrigin()).Size();
			const float RoughScreenSize = PrimitiveBounds.SphereRadius / Distance;
			
			if (RoughScreenSize > TEXT("r.Shadow.RadiusThreshold")) 
			{
				ViewDependentWholeSceneShadowSubjectPrimitives[ShadowIndex].Add(PrimitiveSceneInfo);
			}
		}
	}
}

void FGatherShadowPrimitivesPacket::RenderThreadFinalize()
{
	for (int32 ShadowIndex = 0; ShadowIndex < PreShadowSubjectPrimitives.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = PreShadows[ShadowIndex];

		for(FPrimitiveSceneInfo* CasterPrimitive : PreShadowSubjectPrimitives[ShadowIndex])
		{
			ProjectedShadowInfo->AddSubjectPrimitive(CasterPrimitive, &Views, , );
		}
	}

	for (int32 ShadowIndex = 0; ShadowIndex < ViewDependentWholeSceneShadowSubjectPrimitives.Num(); ShadowIndex++)
	{
		FProjectedShadowInfo* ProjectedShadowInfo = ViewDependentWholeSceneShadows[ShadowIndex];
		
		for(FPrimitiveSceneInfo* CasterPrimitive : ViewDependentWholeSceneShadowSubjectPrimitives[ShadowIndex])
		{
			ProjectedShadowInfo->AddSubjectPrimitive(CasterPrimitive, NULL, , );
		}
	}
}

void FSceneRenderer::AllocateShadowDepthTargets(FRHICommandListImmediate& RHICmdList)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	TArray<FProjectedShadowInfo*> Shadows;				
	TArray<FProjectedShadowInfo*> WholeSceneDirectionalShadows;	
	TArray<FProjectedShadowInfo*> WholeScenePointShadows;	
	TArray<FProjectedShadowInfo*> CachedSpotlightShadows;	
	TArray<FProjectedShadowInfo*> CachedPreShadows;			
	TArray<FProjectedShadowInfo*> TranslucentShadows;

	for (const FLightSceneInfo* LightSceneInfo : Scene->Lights)
	{
		FVisibleLightInfo& VisibleLightInfo = VisibleLightInfos[LightSceneInfo->Id];
		const FVisibleLightViewInfo& VisibleLightViewInfo = View[0].VisibleLightInfos[LightSceneInfo->Id];

		WholeSceneDirectionalShadows.Empty();

		for (FProjectedShadowInfo* ProjectedShadowInfo : VisibleLightInfo.AllProjectedShadows)
		{
			const int32 ShadowIndex = ProjectedShadowInfo->ShadowId;
			const FPrimitiveViewRelevance ViewRelevance = VisibleLightViewInfo.ProjectedShadowViewRelevanceMap[ShadowIndex];
			const bool bHasViewRelevance = (ProjectedShadowInfo->bTranslucentShadow && ViewRelevance.HasTranslucency()) 
				|| (!ProjectedShadowInfo->bTranslucentShadow && ViewRelevance.bOpaque);

			bool bShadowIsVisible = bHasViewRelevance && VisibleLightViewInfo.ProjectedShadowVisibilityMap[ShadowIndex];
			if (!bShadowIsVisible)
				continue;

			bool bNeedsProjection = ProjectedShadowInfo->CacheMode != SDCM_StaticPrimitivesOnly
				
			if (bNeedsProjection)
			{
				if (ProjectedShadowInfo->bCapsuleShadow)
				{
					VisibleLightInfo.CapsuleShadowsToProject.Add(ProjectedShadowInfo);
				}
				else
				{
					VisibleLightInfo.ShadowsToProject.Add(ProjectedShadowInfo);
				}
			}

			if (ProjectedShadowInfo->bCapsuleShadow || ProjectedShadowInfo->bRayTracedDistanceField)
				continue;

			if (ProjectedShadowInfo->bPreShadow && ProjectedShadowInfo->bAllocatedInPreshadowCache)
			{
				CachedPreShadows.Add(ProjectedShadowInfo);
			}
			else if (ProjectedShadowInfo->bDirectionalLight && ProjectedShadowInfo->bWholeSceneShadow)
			{
				WholeSceneDirectionalShadows.Add(ProjectedShadowInfo);
			}
			else if (ProjectedShadowInfo->bOnePassPointLightShadow)
			{
				WholeScenePointShadows.Add(ProjectedShadowInfo);
			}
			else if (ProjectedShadowInfo->bTranslucentShadow)
			{
				TranslucentShadows.Add(ProjectedShadowInfo);
			}
			else if (ProjectedShadowInfo->CacheMode == SDCM_StaticPrimitivesOnly)
			{
				CachedSpotlightShadows.Add(ProjectedShadowInfo);
			}
			else // Per-Object || (WholeScene && Spot && !SDCM_StaticPrimitivesOnly) || (Pre && !bAllocatedInPreshadowCache)
			{
				Shadows.Add(ProjectedShadowInfo);
			}
		}

		VisibleLightInfo.ShadowsToProject.Sort(FCompareFProjectedShadowInfoBySplitIndex());
		AllocateCSMDepthTargets(RHICmdList, WholeSceneDirectionalShadows);
	}

	AllocatePreShadowDepthTargets(RHICmdList, CachedPreShadows);
	AllocateOnePassPointLightDepthTargets(RHICmdList, WholeScenePointShadows);
	AllocateCachedSpotlightShadowDepthTargets(RHICmdList, CachedSpotlightShadows);
	AllocatePerObjectShadowDepthTargets(RHICmdList, Shadows);
	AllocateTranslucentShadowDepthTargets(RHICmdList, TranslucentShadows);

	// Update translucent shadow map uniform buffers.
	...

	// Remove cache entries that haven't been used in a while
	for (TMap<int32, FCachedShadowMapData>::TIterator CachedShadowMapIt(Scene->CachedShadowMaps); CachedShadowMapIt; ++CachedShadowMapIt)
	{
		FCachedShadowMapData& ShadowMapData = CachedShadowMapIt.Value();

		if (ShadowMapData.ShadowMap.IsValid() && ViewFamily.CurrentRealTime - ShadowMapData.LastUsedTime > 2.0f)
		{
			ShadowMapData.ShadowMap.Release();
		}
	}
}

void FSceneRenderer::AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*>& WholeSceneDirectionalShadows)
{
	if (WholeSceneDirectionalShadows.Num() == 0)
		return;
	
	assume(bAllowAtlasing);
	assume(MaxTextureSize == 8192 == 1<<13); // It can include 16 * (2048x2048) cascade splits!!

	FTextureLayout Layout(1, 1, MaxTextureSize, MaxTextureSize, false, ETextureLayoutAspectRatio::None, false);
	TArray<FProjectedShadowInfo*> AllocatedSplits;

	for (FProjectedShadowInfo* ProjectedShadowInfo : WholeSceneDirectionalShadows)
	{
		if (Layout.AddElement(
			ProjectedShadowInfo->X,
			ProjectedShadowInfo->Y,
			ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
			ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2))
		{
			ProjectedShadowInfo->bAllocated = true;
			AllocatedSplits.Add(ProjectedShadowInfo);
		}
	}

	FSortedShadowMapAtlas& CSMAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();

	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
		FIntPoint(Layout.GetSizeX(), Layout.GetSizeY()),
		PF_ShadowDepth, 
		FClearValueBinding::DepthOne, 
		TexCreate_None, 
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 
		false));
	Desc.Flags |= GFastVRamConfig.ShadowCSM;
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, CSMAtlas.RenderTargets.DepthTarget, TEXT("WholeSceneShadowmap"));
	
	for (FProjectedShadowInfo* ProjectedShadowInfo : AllocatedSplits)
	{
		ProjectedShadowInfo->RenderTargets.DepthTarget = CSMAtlas.RenderTargets.DepthTarget.GetReference();
		ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
		CSMAtlas.Shadows.Add(ProjectedShadowInfo);
	}
}

void FSceneRenderer::AllocatePreShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*>& CachedPreShadows)
{
	if (CachedPreShadows.Num() == 0)
		return;

	if (!Scene->PreShadowCacheDepthZ)
	{
		FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(
			SceneContext.GetPreShadowCacheTextureResolution(), // In default, 4096x4096
			PF_ShadowDepth, 
			FClearValueBinding::None, 
			TexCreate_None, 
			TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 
			false, 1, false));
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, Scene->PreShadowCacheDepthZ, TEXT("PreShadowCacheDepthZ"), true, 
			ERenderTargetTransience::NonTransient);
	}

	FSortedShadowMapAtlas& PreShadowAtlas = SortedShadowsForShadowDepthPass.PreshadowCache;
	PreShadowAtlas.RenderTargets.DepthTarget = Scene->PreShadowCacheDepthZ;

	for (FProjectedShadowInfo* ProjectedShadowInfo : CachedPreShadows)
	{
		ProjectedShadowInfo->RenderTargets.DepthTarget = PreShadowAtlas.RenderTargets.DepthTarget.GetReference();
		ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
		PreShadowAtlas.Shadows.Add(ProjectedShadowInfo);
	}
}

void FSceneRenderer::AllocateOnePassPointLightDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeScenePointShadows)
{
	for (FProjectedShadowInfo* ProjectedShadowInfo : WholeScenePointShadows)
	{
	}
}

void FSceneRenderer::AllocateCachedSpotlightShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& CachedSpotlightShadows)
{
	for (FProjectedShadowInfo* ProjectedShadowInfo : CachedSpotlightShadows)
	{
		FIntPoint ShadowResolution(
			ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2, 
			ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2);
		FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
			ShadowResolution, 
			PF_ShadowDepth, 
			FClearValueBinding::DepthOne, 
			TexCreate_None, 
			TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 
			false, 1, false);

		FSortedShadowMapAtlas& SpotAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted(); // a spot per atlas
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, SpotAtlas.RenderTargets.DepthTarget, TEXT("CachedShadowDepthMap"), true, 
			ERenderTargetTransience::NonTransient);

		FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
		CachedShadowMapData.ShadowMap = SpotAtlas.RenderTargets;

		ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
		ProjectedShadowInfo->bAllocated = true;
		ProjectedShadowInfo->RenderTargets.DepthTarget = SpotAtlas.RenderTargets.DepthTarget.GetReference();
		ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
		SpotAtlas.Shadows.Add(ProjectedShadowInfo);
	}
}

void FSceneRenderer::AllocatePerObjectShadowDepthTargets(FRHICommandListImmediate& RHICmdList, TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& Shadows)
{
	if (Shadows.Num() == 0)
		continue;

	FIntPoint AtlasResolution = SceneContext.GetShadowDepthTextureResolution(); // In default, 2048x2048
	FPooledRenderTargetDesc Desc = FPooledRenderTargetDesc::Create2DDesc(
		AtlasResolution, 
		PF_ShadowDepth, 
		FClearValueBinding::DepthOne, 
		TexCreate_None, 
		TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, 
		false);
	Desc.Flags |= GFastVRamConfig.ShadowPerObject;

	FTextureLayout Layout(1, 1, 1, 1, false, ETextureLayoutAspectRatio::None, false);
	Shadows.Sort(FCompareFProjectedShadowInfoByResolution());

	for (FProjectedShadowInfo* ProjectedShadowInfo : Shadows)
	{
		if (ProjectedShadowInfo->CacheMode == SDCM_MovablePrimitivesOnly && !ProjectedShadowInfo->HasSubjectPrims())
		{
			FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(ProjectedShadowInfo->GetLightSceneInfo().Id);
			ProjectedShadowInfo->X = ProjectedShadowInfo->Y = 0;
			ProjectedShadowInfo->bAllocated = true;
			// Skip the shadow depth pass since there are no movable primitives to composite, project from the cached shadowmap directly which contains static primitive depths
			ProjectedShadowInfo->RenderTargets.DepthTarget = CachedShadowMapData.ShadowMap.DepthTarget.GetReference();
			continue;
		}

		bool FirstTry = true;
		bool bAllocated = false;

		while(!bAllocated)
		{
			if (Layout.AddElement(
				ProjectedShadowInfo->X,
				ProjectedShadowInfo->Y,
				ProjectedShadowInfo->ResolutionX + ProjectedShadowInfo->BorderSize * 2,
				ProjectedShadowInfo->ResolutionY + ProjectedShadowInfo->BorderSize * 2) )
			{
				bAllocated = ProjectedShadowInfo->bAllocated = true;
			}
			else
			{
				if (!FirstTry)
					check(0);

				Layout = FTextureLayout(1, 1, AtlasResolution.X, AtlasResolution.Y, false, ETextureLayoutAspectRatio::None, false);
				SortedShadowsForShadowDepthPass.ShadowMapAtlases.AddDefaulted();
			}

			FirstTry = false;
		} 
		
		FSortedShadowMapAtlas& CurrentAtlas = SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last();

		if (!CurrentAtlas.RenderTargets.IsValid() || GFastVRamConfig.bDirty)
		{
			GRenderTargetPool.FindFreeElement(RHICmdList, Desc, CurrentAtlas.RenderTargets.DepthTarget, TEXT("ShadowDepthAtlas"), true, 
				ERenderTargetTransience::NonTransient);
		}

		ProjectedShadowInfo->RenderTargets.DepthTarget = CurrentAtlas.RenderTargets.DepthTarget.GetReference();
		ProjectedShadowInfo->SetupShadowDepthView(RHICmdList, this);
		CurrentAtlas.Shadows.Add(ProjectedShadowInfo);
	}
}

void FProjectedShadowInfo::SetupShadowDepthView(FRHICommandListImmediate& RHICmdList, FSceneRenderer* Renderer)
{
	ShadowDepthView = Renderer->Views[0]->CreateSnapshot();
	ModifyViewForShadow(RHICmdList, ShadowDepthView);
}

void FProjectedShadowInfo::ModifyViewForShadow(FRHICommandList& RHICmdList, FViewInfo* FoundView) const
{
	FIntRect OriginalViewRect = FoundView->ViewRect;

	FoundView->ViewRect = FIntRect(0, 0, ResolutionX, ResolutionY);
	FoundView->ViewMatrices.HackRemoveTemporalAAProjectionJitter();
	FoundView->ViewMatrices.HackOverrideViewMatrixForShadows(ShadowViewMatrix);
	FoundView->MaterialTextureMipBias = 0;

	if (CascadeSettings.bFarShadowCascade)
	{
		(int32&)FoundView->DrawDynamicFlags |= (int32)EDrawDynamicFlags::FarShadowCascade;
	}
	if (bPreShadow && GPreshadowsForceLowestLOD)
	{
		(int32&)FoundView->DrawDynamicFlags |= EDrawDynamicFlags::ForceLowestLOD;
	}

	FoundView->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

	// Full setup is not needed !!
	FoundView->SetupUniformBufferParameters(,,, *FoundView->CachedViewUniformShaderParameters);

	if (IsWholeSceneDirectionalShadow())
	{
		// Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer has no meaningfull value !!
		FoundView->ViewUniformBuffer = FoundView->Family->Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer;
	}
	else
	{
		FoundView->ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(*FoundView->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
	}

	// we are going to set this back now because we only want the correct view rect for the uniform buffer. For LOD calculations, we want the rendering viewrect and proj matrix.
	FoundView->ViewRect = OriginalViewRect;
}

FViewInfo* FViewInfo::CreateSnapshot() const
{
	FViewInfo* Result = FreeViewInfoSnapshots.Num() > 0
		? FreeViewInfoSnapshots.Pop(false) 
		: (FViewInfo*)FMemory::Malloc(sizeof(FViewInfo), alignof(FViewInfo));
	FMemory::Memcpy(*Result, *this);

	// we want these to start null without a reference count, since we clear a ref later
	TUniformBufferRef<FViewUniformShaderParameters> NullViewUniformBuffer;
	FMemory::Memcpy(Result->ViewUniformBuffer, NullViewUniformBuffer); 

	TUniquePtr<FViewUniformShaderParameters> NullViewParameters;
	FMemory::Memcpy(Result->CachedViewUniformShaderParameters, NullViewParameters); 

	FRWBufferStructured NullOneFramePrimitiveShaderDataBuffer;
	FMemory::Memcpy(Result->OneFramePrimitiveShaderDataBuffer, NullOneFramePrimitiveShaderDataBuffer);
	
	FTextureRWBuffer2D NullOneFramePrimitiveShaderDataTexture;
	FMemory::Memcpy(Result->OneFramePrimitiveShaderDataTexture, NullOneFramePrimitiveShaderDataTexture);

	TStaticArray<FParallelMeshDrawCommandPass, EMeshPass::Num> NullParallelMeshDrawCommandPasses;
	FMemory::Memcpy(Result->ParallelMeshDrawCommandPasses, NullParallelMeshDrawCommandPasses);

	TArray<FPrimitiveUniformShaderParameters> NullDynamicPrimitiveShaderData;
	FMemory::Memcpy(Result->DynamicPrimitiveShaderData, NullDynamicPrimitiveShaderData);
	
	Result->DynamicPrimitiveShaderData = DynamicPrimitiveShaderData;
	
	for (int i = 0; i < EMeshPass::Num; i++)
	{
		Result->ParallelMeshDrawCommandPasses[i].InitCreateSnapshot();
	}
	
	Result->bIsSnapshot = true;
	ViewInfoSnapshots.Add(Result);
	
	return Result;
}


class FProjectedShadowInfo : public FRefCountedObject {
	FViewInfo* ShadowDepthView;

	/*In*/TArray<const FPrimitiveSceneInfo*> DynamicSubjectPrimitives;
	/*Out*/TArray<FMeshBatchAndRelevance> DynamicSubjectMeshElements;
	int32 NumDynamicSubjectMeshElements;
	
	FMeshCommandOneFrameArray ShadowDepthPassVisibleCommands;// Bring from Scene->CachedMeshDrawCommandStateBuckets[CSMPass] or Scene->CachedDrawLists[CSMPass]
	TArray<const FStaticMeshBatch*> SubjectMeshCommandBuildRequests;
	int32 NumSubjectMeshCommandBuildRequestElements;

	FParallelMeshDrawCommandPass ShadowDepthPass;
}

//-------------------------------------------------------------------------------------------//
FProjectedShadowInfo::ShadowDepthView <-> FSceneRenderer::Views[0]
FProjectedShadowInfo::DynamicSubjectPrimitives <-> FSceneRenderer::Scene.Primitives // checked with [HasDynamicMeshElementsMasks]
FProjectedShadowInfo::DynamicSubjectPrimitives.Num() <-> FSceneRenderer::Views[0].NumVisibleDynamicPrimitives
FProjectedShadowInfo::ShadowDepthPassVisibleCommands <-> X
FProjectedShadowInfo::SubjectMeshCommandBuildRequests <-> X
FProjectedShadowInfo::NumSubjectMeshCommandBuildRequestElements <-> X
FProjectedShadowInfo::DynamicSubjectMeshElements <-> FSceneRenderer::Views[0].DynamicMeshElements
FProjectedShadowInfo::NumDynamicSubjectMeshElements <-> FSceneRenderer::Views[0].NumVisibleDynamicMeshElements[PassId]
//-------------------------------------------------------------------------------------------//


/*
Out: FProjectedShadowInfo::DynamicSubjectPrimitives
Out: FProjectedShadowInfo::ShadowDepthPassVisibleCommands
Out: FProjectedShadowInfo::SubjectMeshCommandBuildRequests
Out: FProjectedShadowInfo::NumSubjectMeshCommandBuildRequestElements
*/
void FProjectedShadowInfo::AddSubjectPrimitive(FPrimitiveSceneInfo* PrimitiveSceneInfo, TArray<FViewInfo>* ViewArray, ERHIFeatureLevel::Type FeatureLevel, bool bRecordShadowSubjectsForMobileShading)
{
	if(ReceiverPrimitives.Contains(PrimitiveSceneInfo)
		|| (CascadeSettings.bFarShadowCascade && !PrimitiveSceneInfo->Proxy->CastsFarShadow()))
	{
		return;
	}

	const FPrimitiveSceneProxy* Proxy = PrimitiveSceneInfo->Proxy;
	int32 PrimitiveId = PrimitiveSceneInfo->GetIndex();
	FViewInfo& View = IsWholeSceneDirectionalShadow() ? *DependentView : (*ViewArray)[0];
	FPrimitiveViewRelevance& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimitiveId];
	bool bCasterIsVisible = View.PrimitiveVisibilityMap[PrimitiveId];
	assume(ViewRelevance.bInitializedThisFrame);
	
	bool bOpaque = ViewRelevance.bOpaque || ViewRelevance.bMasked;
	bool bTranslucentRelevance = ViewRelevance.HasTranslucency() && !ViewRelevance.bMasked;
	bool bShadowRelevance = ViewRelevance.bShadowRelevance;

	assume(!PrimitiveSceneInfo->NeedsUniformBufferUpdate());
	assume(!PrimitiveSceneInfo->NeedsUpdateStaticMeshes());
	check(PrimitiveSceneInfo->StaticMeshes.Num()==0 || ViewRelevance.bStaticRelevance); // If the primitive is static mesh, bStaticRelevance == true
	assume(!bTranslucentRelevance);

	if (bOpaque && bShadowRelevance)
	{
		const FBoxSphereBounds& Bounds = Proxy->GetBounds();
		const float Distance = ( Bounds.Origin - View.ShadowViewMatrices.GetViewOrigin() ).Size();
		bool bDrawingStaticMeshes = false;

		// Why cull only for WholeScene?
		if ( PrimitiveSceneInfo->StaticMeshes.Num() > 0
			/* (!bWholeSceneShadow || Bounds.SphereRadius / Distance > TEXT("r.Shadow.RadiusThreshold")) */
			&& !(bWholeSceneShadow && Bounds.SphereRadius / Distance < TEXT("r.Shadow.RadiusThreshold")) )
		{
			bDrawingStaticMeshes = ShouldDrawStaticMeshes(View, , PrimitiveSceneInfo);
		}

		if (!bDrawingStaticMeshes)
		{
			DynamicSubjectPrimitives.Add(PrimitiveSceneInfo);
		}
	}
}

/*
Out: FProjectedShadowInfo::ShadowDepthPassVisibleCommands
Out: FProjectedShadowInfo::SubjectMeshCommandBuildRequests
Out: FProjectedShadowInfo::NumSubjectMeshCommandBuildRequestElements
*/
bool FProjectedShadowInfo::ShouldDrawStaticMeshes(FViewInfo& View, , FPrimitiveSceneInfo* InPrimitiveSceneInfo)
{
	bool bDrawingStaticMeshes = false;
	int32 PrimitiveId = InPrimitiveSceneInfo->GetIndex();

	if (View.PrimitivesLODMask[PrimitiveId].ContainsLOD(MAX_int8))
	{
		float MeshScreenSizeSquared = 0;
		const int8 CurFirstLODIdx = InPrimitiveSceneInfo->Proxy->GetCurrentFirstLODIdx_RenderThread();
		const int32 ForcedLOD = -1;	
		const FBoxSphereBounds& Bounds = InPrimitiveSceneInfo->Proxy->GetBounds();

		View.PrimitivesLODMask[PrimitiveId] = ComputeLODForMeshes(
			InPrimitiveSceneInfo->StaticMeshRelevances, 
			View, 
			Bounds.Origin, 
			Bounds.SphereRadius, 
			ForcedLOD = -1, 
			MeshScreenSizeSquared = 0, 
			CurFirstLODIdx, 
			View.LODDistanceFactor = 1.0);
	}

	// Copy
	FLODMask ShadowLODToRender = View.PrimitivesLODMask[PrimitiveId];
	if (bPreShadow && TEXT("r.Shadow.PreshadowsForceLowestDetailLevel"))
	{
		...
		ShadowLODToRender.SetLOD(LODToRenderScan);
	}
	if (CascadeSettings.bFarShadowCascade)
	{
		...
		ShadowLODToRender.SetLOD(LODToRenderScan);
	}

	assume(!bSelfShadowOnly);
	const bool bCanCache = !View.PotentiallyFadingPrimitiveMap[PrimitiveId] && !InPrimitiveSceneInfo->NeedsUpdateStaticMeshes();

	for (int32 MeshIndex = 0; MeshIndex < InPrimitiveSceneInfo->StaticMeshRelevances.Num(); MeshIndex++)
	{
		const FStaticMeshBatchRelevance& StaticMeshRelevance = InPrimitiveSceneInfo->StaticMeshRelevances[MeshIndex];
		const FStaticMeshBatch& StaticMesh = InPrimitiveSceneInfo->StaticMeshes[MeshIndex];

		if (StaticMeshRelevance.CastShadow && ShadowLODToRender.ContainsLOD(StaticMeshRelevance.LODIndex))
		{
			if (IsWholeSceneDirectionalShadow() && bCanCache)
			{
				AddCachedMeshDrawCommandsForPass(
					PrimitiveId,
					InPrimitiveSceneInfo,
					StaticMeshRelevance,
					StaticMesh,
					InPrimitiveSceneInfo->Scene, ,
					/*Out*/ShadowDepthPassVisibleCommands,
					/*Out*/SubjectMeshCommandBuildRequests,
					/*Out*/NumSubjectMeshCommandBuildRequestElements);
			}
			else
			{
				NumSubjectMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
				SubjectMeshCommandBuildRequests.Add(&StaticMesh);
			}

			bDrawingStaticMeshes = true;
		}
	}
	
	return bDrawingStaticMeshes;
}

void FProjectedShadowInfo::AddCachedMeshDrawCommandsForPass(
	int32 PrimitiveIndex,
	const FPrimitiveSceneInfo* InPrimitiveSceneInfo,
	const FStaticMeshBatchRelevance& StaticMeshRelevance,
	const FStaticMeshBatch& StaticMesh,
	const FScene* Scene, ,
	FMeshCommandOneFrameArray& VisibleMeshCommands,
	TArray<const FStaticMeshBatch*, SceneRenderingAllocator>& MeshCommandBuildRequests,
	int32& NumMeshCommandBuildRequestElements)
{
	EMeshPass::Type CSMPass = EMeshPass::CSMShadowDepth;

	if (StaticMeshRelevance.bSupportsCachingMeshDrawCommands)
	{
		const int32 StaticMeshCommandInfoIndex = StaticMeshRelevance.GetStaticMeshCommandInfoIndex(CSMPass);
		if (StaticMeshCommandInfoIndex >= 0)
		{
			const FCachedMeshDrawCommandInfo& CachedMeshDrawCommand = InPrimitiveSceneInfo->StaticMeshCommandInfos[StaticMeshCommandInfoIndex];
			const FMeshDrawCommand* MeshDrawCommand = CachedMeshDrawCommand.StateBucketId >= 0
					? &Scene->CachedMeshDrawCommandStateBuckets[CSMPass].GetByElementId(CachedMeshDrawCommand.StateBucketId).Key
					: &Scene->CachedDrawLists[CSMPass].MeshDrawCommands[CachedMeshDrawCommand.CommandIndex];

			FVisibleMeshDrawCommand NewVisibleMeshDrawCommand;

			NewVisibleMeshDrawCommand.Setup(
				MeshDrawCommand,
				PrimitiveIndex,
				PrimitiveIndex,
				CachedMeshDrawCommand.StateBucketId,
				CachedMeshDrawCommand.MeshFillMode,
				CachedMeshDrawCommand.MeshCullMode,
				CachedMeshDrawCommand.SortKey);

			VisibleMeshCommands.Add(NewVisibleMeshDrawCommand);
		}
	}
	else
	{
		NumMeshCommandBuildRequestElements += StaticMeshRelevance.NumElements;
		MeshCommandBuildRequests.Add(&StaticMesh);
	}
}




void FProjectedShadowInfo::GatherDynamicMeshElementsArray(
	FViewInfo* ShadowView,
	FSceneRenderer& Renderer, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
	FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
	FGlobalDynamicReadBuffer& DynamicReadBuffer,
	const TArray<const FPrimitiveSceneInfo*>& DynamicSubjectPrimitives, ,
	TArray<FMeshBatchAndRelevance,SceneRenderingAllocator>& OutDynamicMeshElements,
	int32& OutNumDynamicSubjectMeshElements)
{
	Renderer.MeshCollector.ClearViewMeshArrays();
	Renderer.MeshCollector.AddViewMeshArrays(
		ShadowView, 
		&OutDynamicMeshElements, 
		&FSimpleElementCollector(), // Simple elements not supported in shadow passes
		&ShadowView->DynamicPrimitiveShaderData,,
		&DynamicIndexBuffer, &DynamicVertexBuffer, &DynamicReadBuffer);

	for (const FPrimitiveSceneInfo* PrimitiveSceneInfo : DynamicSubjectPrimitives)
	{
		FPrimitiveViewRelevance ViewRelevance = ShadowView->PrimitiveViewRelevanceMap[PrimitiveSceneInfo->GetIndex()];
		if (!ViewRelevance.bInitializedThisFrame)
		{
			ViewRelevance = PrimitiveSceneInfo->Proxy->GetViewRelevance(ShadowView);
		}

		if (ViewRelevance.bShadowRelevance && ViewRelevance.bDynamicRelevance)
		{
			Renderer.MeshCollector.SetPrimitive(PrimitiveSceneInfo->Proxy, PrimitiveSceneInfo->DefaultDynamicHitProxyId);
			PrimitiveSceneInfo->Proxy->GetDynamicMeshElements({ShadowView}, Renderer.ViewFamily, 0x1, Renderer.MeshCollector);
		}
	}

	OutNumDynamicSubjectMeshElements = Renderer.MeshCollector.GetMeshElementCount(0);
}



void FSceneRenderer::GatherShadowDynamicMeshElements(, , )
{
	for (FSortedShadowMapAtlas& Atlas : SortedShadowsForShadowDepthPass.ShadowMapAtlases)
	{
		for (FProjectedShadowInfo* ProjectedShadowInfo : Atlas.Shadows)
		{
			ProjectedShadowInfo->GatherDynamicMeshElements(*this, , , , , );
		}
	}
}

void FProjectedShadowInfo::GatherDynamicMeshElements(
	FSceneRenderer& Renderer, , , , , )
{
	if (DynamicSubjectPrimitives.Num() > 0 || ReceiverPrimitives.Num() > 0)
	{
		GatherDynamicMeshElementsArray(
			ShadowDepthView, 
			Renderer, , , , 
			/*In*/DynamicSubjectPrimitives, ,
			/*Out*/DynamicSubjectMeshElements, 
			/*Out*/NumDynamicSubjectMeshElements);
	}

	// Create a pass uniform buffer so we can build mesh commands now.  
	// This will be updated with the correct contents just before the actual pass.
	ShadowDepthPassUniformBuffer = TUniformBufferRef<FShadowDepthPassUniformParameters>::CreateUniformBufferImmediate(
		FShadowDepthPassUniformParameters(), UniformBuffer_MultiFrame, EUniformBufferValidation::None);
	
	SetupMeshDrawCommandsForShadowDepth(Renderer, ShadowDepthPassUniformBuffer);
	if (bPreShadow || bSelfShadowOnly)
	{
		SetupMeshDrawCommandsForProjectionStenciling(Renderer);
	}
}

void FProjectedShadowInfo::SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer)
{
	FShadowDepthPassMeshProcessor* MeshPassProcessor = new(FMemStack::Get()) FShadowDepthPassMeshProcessor(
		Renderer.Scene,
		ShadowDepthView,
		ShadowDepthView->ViewUniformBuffer,
		PassUniformBuffer,
		GetShadowDepthType(),
		nullptr);
	
	const uint32 InstanceFactor = bOnePassPointLightShadow ? 6 : 1;

	ShadowDepthPass.DispatchPassSetup(
		Renderer.Scene,
		*ShadowDepthView,
		EMeshPass::Num,
		FExclusiveDepthStencil::DepthNop_StencilNop,
		MeshPassProcessor,
		DynamicSubjectMeshElements,
		/* TArray<FMeshPassMask>* */nullptr,
		NumDynamicSubjectMeshElements * InstanceFactor,
		SubjectMeshCommandBuildRequests,
		NumSubjectMeshCommandBuildRequestElements * InstanceFactor,
		ShadowDepthPassVisibleCommands);

	Renderer.DispatchedShadowDepthPasses.Add(&ShadowDepthPass);
}
