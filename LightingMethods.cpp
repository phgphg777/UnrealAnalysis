// LightGridInjection.cpp
int32 GLightCullingQuality = 1;
FAutoConsoleVariableRef CVarLightCullingQuality(
	TEXT("r.LightCulling.Quality"),
	GLightCullingQuality,
	TEXT("Whether to run compute light culling pass.\n")
	TEXT(" 0: off \n")
	TEXT(" 1: on (default)\n"),
	ECVF_RenderThreadSafe
);

// ClusteredDeferredShadingPass.cpp
int32 GUseClusteredDeferredShading = 0;
static FAutoConsoleVariableRef CVarUseClusteredDeferredShading(
	TEXT("r.UseClusteredDeferredShading"),
	GUseClusteredDeferredShading,
	TEXT("Toggle use of clustered deferred shading for lights that support it. 0 is off (default), 1 is on (also required is SM5 to actually turn on)."),
	ECVF_RenderThreadSafe
);

// TiledDeferredLightRendering.cpp
int32 GUseTiledDeferredShading = 1;
static FAutoConsoleVariableRef CVarUseTiledDeferredShading(
	TEXT("r.TiledDeferredShading"),
	GUseTiledDeferredShading,
	TEXT("Whether to use tiled deferred shading.  0 is off, 1 is on (default)"),
	ECVF_RenderThreadSafe
	);

int32 GNumLightsBeforeUsingTiledDeferred = 80;
static FAutoConsoleVariableRef CVarNumLightsBeforeUsingTiledDeferred(
	TEXT("r.TiledDeferredShading.MinimumCount"),
	GNumLightsBeforeUsingTiledDeferred,
	TEXT("Number of applicable lights that must be on screen before switching to tiled deferred.\n")
	TEXT("0 means all lights that qualify (e.g. no shadows, ...) are rendered tiled deferred. Default: 80"),
	ECVF_RenderThreadSafe
	);

void FDeferredShadingSceneRenderer::ComputeLightGrid()
{
	const bool bCullLightsToGrid = GLightCullingQuality 
		&& (Views[i].bTranslucentSurfaceLighting || ShouldRenderVolumetricFog()  || IsRayTracingEnabled() || ShouldUseClusteredDeferredShading());

	bClusteredShadingLightsInLightGrid = bCullLightsToGrid;
}

bool FDeferredShadingSceneRenderer::AreClusteredLightsInLightGrid() const
{
	return bClusteredShadingLightsInLightGrid;
}

bool FDeferredShadingSceneRenderer::ShouldUseClusteredDeferredShading() const
{
	return GUseClusteredDeferredShading != 0;
}

bool FDeferredShadingSceneRenderer::CanUseTiledDeferred() const
{
	return GUseTiledDeferredShading != 0;
}

bool FDeferredShadingSceneRenderer::ShouldUseTiledDeferred(int32 NumTiledDeferredLights) const
{
	return (NumTiledDeferredLights >= GNumLightsBeforeUsingTiledDeferred); // Only use tiled deferred if there are enough unshadowed lights to justify the fixed cost, 
}

void FDeferredShadingSceneRenderer::RenderLights()
{
	if (ShouldUseClusteredDeferredShading() && AreClusteredLightsInLightGrid())
	{
		AddClusteredDeferredShadingPass(RHICmdList, SortedLightSet);
	}
	else if (CanUseTiledDeferred() && ShouldUseTiledDeferred(SortedLightSet.TiledSupportedEnd))
	{
		RenderTiledDeferredLighting(RHICmdList, SortedLights, SortedLightSet.SimpleLightsEnd, SortedLightSet.TiledSupportedEnd, SimpleLights);
	}
	else
	{
		RenderSimpleLightsStandardDeferred(RHICmdList, SortedLightSet.SimpleLights);
	}
}





void FSceneRenderer::GatherSimpleLights(const FSceneViewFamily& ViewFamily, const TArray<FViewInfo>& Views, FSimpleLightArray& OutSimpleLights)
{
	TArray<const FPrimitiveSceneInfo*, SceneRenderingAllocator> PrimitivesWithSimpleLights;

	for(const FViewInfo& View : Views)// Gather visible primitives from all views that might have simple lights
	{
		for(const FPrimitiveSceneInfo* PSI : View.VisibleDynamicPrimitivesWithSimpleLights)
		{
			PrimitivesWithSimpleLights.AddUnique(PSI);
		}
	}

	for(const FPrimitiveSceneInfo* Primitive : PrimitivesWithSimpleLights)// Gather simple lights from the primitives
	{
		Primitive->Proxy->GatherSimpleLights(ViewFamily, OutSimpleLights);
	}
}

void FParticleSystemSceneProxy::GatherSimpleLights(const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	if (DynamicData != NULL)
	{
		for (int32 EmitterIndex = 0; EmitterIndex < DynamicData->DynamicEmitterDataArray.Num(); EmitterIndex++)
		{
			const FDynamicEmitterDataBase* DynamicEmitterData = DynamicData->DynamicEmitterDataArray[EmitterIndex];
			if (DynamicEmitterData)
			{
				DynamicEmitterData->GatherSimpleLights(this, ViewFamily, OutParticleLights);
			}
		}
	}
}

class FParticleSystemSceneProxy final : public FPrimitiveSceneProxy {
	FParticleDynamicData* DynamicData;
}

class FParticleDynamicData {
	uint32 EmitterIndex;/** The Current Emmitter we are rendering **/
	TArray<FDynamicEmitterDataBase*>	DynamicEmitterDataArray;// Variables
	FVector SystemPositionForMacroUVs;/** World space position that UVs generated with the ParticleMacroUV material node will be centered on. */
	float SystemRadiusForMacroUVs;/** World space radius that UVs generated with the ParticleMacroUV material node will tile based on. */
}

struct FDynamicEmitterDataBase {}
struct FDynamicSpriteEmitterDataBase : public FDynamicEmitterDataBase {}
struct FDynamicSpriteEmitterData : public FDynamicSpriteEmitterDataBase {}/** Dynamic emitter data for sprite emitters */
struct FDynamicMeshEmitterData : public FDynamicSpriteEmitterDataBase {}/** Dynamic emitter data for Mesh emitters */
struct FDynamicBeam2EmitterData : public FDynamicSpriteEmitterDataBase {}/** Dynamic emitter data for Beam emitters */
struct FDynamicTrailsEmitterData : public FDynamicSpriteEmitterDataBase {}/** Dynamic emitter data for Ribbon emitters */



class FSimpleLightArray
{
	TArray<FSimpleLightEntry> InstanceData;/** Data per simple dynamic light instance, independent of view */
	TArray<FSimpleLightPerViewEntry> PerViewData;/** Per-view data for each light */
	TArray<FSimpleLightInstacePerViewIndexData> InstancePerViewDataIndices;/** Indices into the per-view data for each light. */
};

void FDynamicSpriteEmitterData::GatherSimpleLights(const FParticleSystemSceneProxy* Proxy, const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	GatherParticleLightData(Source, Proxy->GetLocalToWorld(), ViewFamily, OutParticleLights);
}

void FDynamicMeshEmitterData::GatherSimpleLights(const FParticleSystemSceneProxy* Proxy, const FSceneViewFamily& ViewFamily, FSimpleLightArray& OutParticleLights) const
{
	GatherParticleLightData(Source, Proxy->GetLocalToWorld(), ViewFamily, OutParticleLights);
}

void GatherParticleLightData(const FDynamicSpriteEmitterReplayDataBase& Source, const FMatrix& InLocalToWorld, const FSceneViewFamily& InViewFamily, FSimpleLightArray& OutParticleLights)
{
	...
}


static int32 bAllowSimpleLights = 1;
static FAutoConsoleVariableRef CVarAllowSimpleLights(
	TEXT("r.AllowSimpleLights"),
	bAllowSimpleLights,
	TEXT("If true, we allow simple (ie particle) lights")
);

static TAutoConsoleVariable<int32> CVarShadowQuality(
	TEXT("r.ShadowQuality"),
	5,
	TEXT("Defines the shadow method which allows to adjust for quality or performance.\n")
	TEXT(" 0:off, 1:low(unfiltered), 2:low .. 5:max (default)"),
	ECVF_Scalability | ECVF_RenderThreadSafe);



struct FSortedLightSceneInfo
{
	union {
		struct {
			// Note: the order of these members controls the light sort order!
			// Currently bTiledDeferredNotSupported is the MSB and LightType is LSB
			uint32 LightType : 2;	// (LightType_Directional=0, LightType_Point=1, LightType_Spot=2, LightType_Rect=3)			
			uint32 bTextureProfile : 1;
			uint32 bLightFunction : 1;
			uint32 bShadowed : 1;			
			uint32 bUsesLightingChannels : 1;

			uint32 bIsNotSimpleLight : 1;
			uint32 bTiledDeferredNotSupported : 1;
			uint32 bClusteredDeferredNotSupported : 1;
		} Fields;
		int32 Packed;/** Sort key bits packed into an integer. */
	} SortKey;

	const FLightSceneInfo* LightSceneInfo;
	int32 SimpleLightIndex;

	FSortedLightSceneInfo(const FLightSceneInfo* InLightSceneInfo) : SortKey.Packed(0)
	{
		LightSceneInfo = InLightSceneInfo;
		SimpleLightIndex = -1;
		SortKey.Fields.bIsNotSimpleLight = 1;
	}
	FSortedLightSceneInfo(int32 InSimpleLightIndex) : SortKey.Packed(0)
	{
		LightSceneInfo = nullptr;
		SimpleLightIndex = InSimpleLightIndex;
		SortKey.Fields.bIsNotSimpleLight = 0;
	}
};

struct FSortedLightSetSceneInfo {
	FSimpleLightArray SimpleLights;
	TArray<FSortedLightSceneInfo> SortedLights;
	int SimpleLightsEnd;
	int TiledSupportedEnd;
	int ClusteredSupportedEnd;
	int AttenuationLightStart;/** First light with shadow map or */
};

void FDeferredShadingSceneRenderer::GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights)
{
	if (bAllowSimpleLights)
	{
		GatherSimpleLights(ViewFamily, Views, OutSortedLights.SimpleLights);
	}
	const auto& SimpleLights = OutSortedLights.SimpleLights;
	
	auto& SortedLights = OutSortedLights.SortedLights;
	SortedLights.Empty(Scene->Lights.Num() + SimpleLights.InstanceData.Num());// NOTE: we allocate space also for simple lights such that they can be referenced in the same sorted range

	// Build a list of visible lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		if (LightSceneInfo->ShouldRenderLightViewIndependent())
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (LightSceneInfo->ShouldRenderLight(Views[ViewIndex]))
				{
					FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.LightType = LightSceneInfoCompact.LightType;
					SortedLightInfo->SortKey.Fields.bTextureProfile = ViewFamily.EngineShowFlags.TexturedLightProfiles && LightSceneInfo->Proxy->GetIESTextureResource();
					SortedLightInfo->SortKey.Fields.bShadowed = CheckForProjectedShadows(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bLightFunction = ViewFamily.EngineShowFlags.LightFunctions && CheckForLightFunction(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bUsesLightingChannels = Views[ViewIndex].bUsesLightingChannels && LightSceneInfo->Proxy->GetLightingChannelMask() != GetDefaultLightingChannelMask();

					const bool bTiledOrClusteredDeferredSupported =
						!SortedLightInfo->SortKey.Fields.bTextureProfile &&
						!SortedLightInfo->SortKey.Fields.bShadowed &&
						!SortedLightInfo->SortKey.Fields.bLightFunction &&
						!SortedLightInfo->SortKey.Fields.bUsesLightingChannels
						&& LightSceneInfoCompact.LightType != LightType_Directional
						&& LightSceneInfoCompact.LightType != LightType_Rect;

					SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 1;
					SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = !(bTiledOrClusteredDeferredSupported && LightSceneInfo->Proxy->IsTiledDeferredLightingSupported());
					SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = !bTiledOrClusteredDeferredSupported;
					break;
				}
			}
		}
	}

	for (int32 SimpleLightIndex = 0; SimpleLightIndex < SimpleLights.InstanceData.Num(); SimpleLightIndex++)
	{
		FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo(SimpleLightIndex);
		SortedLightInfo->SortKey.Fields.LightType = LightType_Point;
		SortedLightInfo->SortKey.Fields.bTextureProfile = 0;
		SortedLightInfo->SortKey.Fields.bShadowed = 0;
		SortedLightInfo->SortKey.Fields.bLightFunction = 0;
		SortedLightInfo->SortKey.Fields.bUsesLightingChannels = 0;

		SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 0;
		SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = 0;
		SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = 0;
	}

	SortedLights.Sort([](const FSortedLightSceneInfo& A, const FSortedLightSceneInfo& B){return A.SortKey.Packed < B.SortKey.Packed;});

	OutSortedLights.SimpleLightsEnd = SortedLights.Num();
	OutSortedLights.TiledSupportedEnd = SortedLights.Num();
	OutSortedLights.ClusteredSupportedEnd = SortedLights.Num();
	OutSortedLights.AttenuationLightStart = SortedLights.Num();

	for (int32 LightIndex = 0; LightIndex < SortedLights.Num(); LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
		const bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
		const bool bTextureLightProfile = SortedLightInfo.SortKey.Fields.bTextureProfile;
		const bool bLightingChannels = SortedLightInfo.SortKey.Fields.bUsesLightingChannels;

		if (SortedLightInfo.SortKey.Fields.bIsNotSimpleLight && OutSortedLights.SimpleLightsEnd == SortedLights.Num())
		{
			OutSortedLights.SimpleLightsEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bTiledDeferredNotSupported && OutSortedLights.TiledSupportedEnd == SortedLights.Num())
		{
			OutSortedLights.TiledSupportedEnd = LightIndex;
		}

		if (SortedLightInfo.SortKey.Fields.bClusteredDeferredNotSupported && OutSortedLights.ClusteredSupportedEnd == SortedLights.Num())
		{
			OutSortedLights.ClusteredSupportedEnd = LightIndex;
		}

		if (bDrawShadows || bDrawLightFunction || bLightingChannels)
		{
			OutSortedLights.AttenuationLightStart = LightIndex;
			break;
		}
	}

	check(
		OutSortedLights.SimpleLightsEnd <= 
		OutSortedLights.TiledSupportedEnd <= 
		OutSortedLights.ClusteredSupportedEnd <=
		OutSortedLights.AttenuationLightStart
	);
}