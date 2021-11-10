//SceneRendering.cpp
static TAutoConsoleVariable<int32> CVarMaxShadowCascades(
	TEXT("r.Shadow.CSM.MaxCascades"),
	10,
	TEXT("The maximum number of cascades with which to render dynamic directional light shadows."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

//ConsoleManager.cpp
static TAutoConsoleVariable<float> CVarShadowDistanceScale(
	TEXT("r.Shadow.DistanceScale"),
	1.0f,
	TEXT("Scalability option to trade shadow distance versus performance for directional lights (clamped within a reasonable range).\n")
	TEXT("<1: shorter distance\n")
	TEXT(" 1: normal (default)\n")
	TEXT(">1: larger distance"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarShadowMaxCSMShadowResolution(
	TEXT("r.Shadow.MaxCSMResolution"),
	2048,
	TEXT("Max square dimensions (in texels) allowed for rendering Cascaded Shadow depths. Range 4 to hardware limit. Higher = better quality shadows but at a performance cost."),
	ECVF_Scalability | ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarShadowCSMTransitionScale(
	TEXT("r.Shadow.CSM.TransitionScale"),
	1.0f,
	TEXT("Allows to scale the cascaded shadow map transition region. Clamped within 0..2.\n")
	TEXT("0: no transition (fastest)\n")
	TEXT("1: as specific in the light settings (default)\n")
	TEXT("2: 2x larger than what was specified in the light"),
	ECVF_Scalability | ECVF_RenderThreadSafe);

//SceneCore.cpp
int32 GUnbuiltPreviewShadowsInGame = 1;
FAutoConsoleVariableRef CVarUnbuiltPreviewShadowsInGame(
	TEXT("r.Shadow.UnbuiltPreviewInGame"),
	GUnbuiltPreviewShadowsInGame,
	TEXT("Whether to render unbuilt preview shadows in game.  When enabled and lighting is not built, expensive preview shadows will be rendered in game.  When disabled, lighting in game and editor won't match which can appear to be a bug."),
	ECVF_Scalability | ECVF_RenderThreadSafe
	);

//DirectionalLightComponent.cpp
static TAutoConsoleVariable<float> CVarUnbuiltWholeSceneDynamicShadowRadius(
	TEXT("r.Shadow.UnbuiltWholeSceneDynamicShadowRadius"),
	200000.0f,
	TEXT("WholeSceneDynamicShadowRadius to use when using CSM to preview unbuilt lighting from a directional light")
	);

static TAutoConsoleVariable<int32> CVarUnbuiltNumWholeSceneDynamicShadowCascades(
	TEXT("r.Shadow.UnbuiltNumWholeSceneDynamicShadowCascades"),
	4,
	TEXT("DynamicShadowCascades to use when using CSM to preview unbuilt lighting from a directional light"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRTDFDistanceScale(
	TEXT("r.DFDistanceScale"),
	1.0f,
	TEXT("Factor to scale directional light property 'DistanceField Shadows Distance', clamped to [0.0001, 10000].\n")
	TEXT("I.e.: DistanceFieldShadowsDistance *= r.DFDistanceScale.\n")	
	TEXT("[0.0001,1): shorter distance\n")
	TEXT(" 1: normal (default)\n")
	TEXT("(1,10000]: larger distance.)"),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<float> CVarRtdfFarTransitionScale(
	TEXT("r.DFFarTransitionScale"),
	1.0f,
	TEXT("Use to modify the length of the far transition (fade out) of the distance field shadows.\n")
	TEXT("1.0: (default) Calculate in the same way as other cascades.")
	TEXT("0.0: Disable fade out."),
	ECVF_RenderThreadSafe);

static TAutoConsoleVariable<int32> CVarMaxNumFarShadowCascades(
	TEXT("r.Shadow.MaxNumFarShadowCascades"),
	10,
	TEXT("Max number of far shadow cascades that can be cast from a directional light"),
	ECVF_RenderThreadSafe | ECVF_Scalability );


class ENGINE_API UDirectionalLightComponent : public ULightComponent {
	float ShadowCascadeBiasDistribution = 1.0f;

	float DynamicShadowDistanceMovableLight = 20000.0f;/* How far Cascaded Shadow Map dynamic shadows will cover for a movable light */
	float DynamicShadowDistanceStationaryLight = 0.f;/* How far Cascaded Shadow Map dynamic shadows will cover for a stationary light */
	int32 DynamicShadowCascades = 3;
	float CascadeDistributionExponent = 3.0f;
	float CascadeTransitionFraction = 0.1f;
	float ShadowDistanceFadeoutFraction; = 0.1f;
	uint32 bUseInsetShadowsForMovableObjects = true;
	int32 FarShadowCascadeCount = 0;
	float FarShadowDistance = 300000.0f;
	float DistanceFieldShadowDistance = 30000.0f;/* Distance at which the ray traced shadow cascade should end.  Distance field shadows will cover the range between 'Dynamic Shadow Distance' this distance. */
}

class ENGINE_API FLightSceneProxy {	
	float FarShadowDistance;/** Only for whole scene directional lights, if FarShadowCascadeCount > 0 and FarShadowDistance >= WholeSceneDynamicShadowRadius, where far shadow cascade should end. */
	uint32 FarShadowCascadeCount;/** Only for whole scene directional lights, 0: no FarShadowCascades, otherwise the count of cascades between WholeSceneDynamicShadowRadius and FarShadowDistance that are covered by distant shadow cascades. */
}
class FDirectionalLightSceneProxy : public FLightSceneProxy {
	float ShadowCascadeBiasDistribution;
	bool bEnableLightShaftOcclusion;
	bool bUseInsetShadowsForMovableObjects;
	float OcclusionMaskDarkness;
	float OcclusionDepthRange;
	FVector LightShaftOverrideDirection;
	FLinearColor AtmosphereTransmittanceFactor;
	FLinearColor SunDiscOuterSpaceLuminance;
	float WholeSceneDynamicShadowRadius;
	uint32 DynamicShadowCascades;
	float CascadeDistributionExponent;
	float CascadeTransitionFraction;
	float ShadowDistanceFadeoutFraction;
	float DistanceFieldShadowDistance;
	float LightSourceAngle;
	float LightSourceSoftAngle;
	float TraceDistance;

	FDirectionalLightSceneProxy(const UDirectionalLightComponent* Component) : FLightSceneProxy(Component)
	{
		ShadowCascadeBiasDistribution = Component->ShadowCascadeBiasDistribution;

		WholeSceneDynamicShadowRadius = Component->Mobility == EComponentMobility::Movable ? 
			Component->DynamicShadowDistanceMovableLight : Component->DynamicShadowDistanceStationaryLight;

		DynamicShadowCascades = Component->DynamicShadowCascades;
		CascadeDistributionExponent = Component->CascadeDistributionExponent;
		CascadeTransitionFraction = Component->CascadeTransitionFraction;
		ShadowDistanceFadeoutFraction = Component->ShadowDistanceFadeoutFraction;

		if(Component->FarShadowCascadeCount && Component->FarShadowDistance - WholeSceneDynamicShadowRadius > 100.0f)
		{
			FarShadowDistance = Component->FarShadowDistance;
			FarShadowCascadeCount = Component->FarShadowCascadeCount;
		}

		DistanceFieldShadowDistance = Component->bUseRayTracedDistanceFieldShadows ?
			Component->DistanceFieldShadowDistance : 0;

		bUseInsetShadowsForMovableObjects = Component->bUseInsetShadowsForMovableObjects;
	}
};

FVector2D GetDirectionalLightDistanceFadeParameters(ERHIFeatureLevel::Type InFeatureLevel, bool bPrecomputedLightingIsValid, int32 MaxNearCascades) const override
{
	float FarDistance = GetCSMMaxDistance(bPrecomputedLightingIsValid, MaxNearCascades);
	if (ShouldCreateRayTracedCascade(InFeatureLevel, bPrecomputedLightingIsValid, MaxNearCascades))
	{
		FarDistance = GetDistanceFieldShadowDistance();
	}
	FarDistance = FMath::Max(FarDistance, FarShadowDistance);
    
	const float NearDistance = FarDistance * (1.0f - ShadowDistanceFadeoutFraction);// The far distance for the dynamic to static fade is the range of the directional light.
	return FVector2D( NearDistance, 1.0f / (FarDistance - NearDistance) );
}

bool FDirectionalLightSceneProxy::ShouldCreateRayTracedCascade(, bool bPrecomputedLightingIsValid, int32 MaxNearCascades) const override
{
	if ( TEXT("r.DistanceFieldShadowing") == 0 && TEXT("r.HeightFieldShadowing") == 0 ) // default : 1, 0
	{
		return false;
	}

	const uint32 NumCascades = GetNumShadowMappedCascades(MaxNearCascades, bPrecomputedLightingIsValid);
	const float RayTracedShadowDistance = GetDistanceFieldShadowDistance();
	const bool bCreateWithCSM = NumCascades > 0 && RayTracedShadowDistance > GetCSMMaxDistance(bPrecomputedLightingIsValid, MaxNearCascades);
	const bool bCreateWithoutCSM = NumCascades == 0 && RayTracedShadowDistance > 0;
	return bCreateWithCSM || bCreateWithoutCSM;
}

uint32 FDirectionalLightSceneProxy::GetNumShadowMappedCascades(uint32 MaxShadowCascade=10, bool bPrecomputedLightingIsValid) const
{
	int32 EffectiveNumDynamicShadowCascades = DynamicShadowCascades;
	if (!bPrecomputedLightingIsValid)
	{
		EffectiveNumDynamicShadowCascades = TEXT("r.Shadow.UnbuiltNumWholeSceneDynamicShadowCascades");
		
		if (!GetSceneInterface()->IsEditorScene() && TEXT("r.Shadow.UnbuiltPreviewInGame") == 0)
		{
			EffectiveNumDynamicShadowCascades = 0;
		}
	}

	const int32 NumCascades = GetCSMMaxDistance(bPrecomputedLightingIsValid, MaxShadowCascades) > 0.0f 
		? EffectiveNumDynamicShadowCascades : 0;
	return FMath::Min<int32>(NumCascades, MaxShadowCascades);
}

float FDirectionalLightSceneProxy::GetCSMMaxDistance(bool bPrecomputedLightingIsValid, int32 MaxShadowCascades) const
{
	return GetEffectiveWholeSceneDynamicShadowRadius(bPrecomputedLightingIsValid) 
		* TEXT("r.Shadow.DistanceScale");
}

float FDirectionalLightSceneProxy::GetEffectiveWholeSceneDynamicShadowRadius(bool bPrecomputedLightingIsValid) const
{
	return bPrecomputedLightingIsValid ? 
		WholeSceneDynamicShadowRadius : TEXT("r.Shadow.UnbuiltWholeSceneDynamicShadowRadius");
}

float FDirectionalLightSceneProxy::GetEffectiveCascadeDistributionExponent(bool bPrecomputedLightingIsValid) const
{
	return bPrecomputedLightingIsValid ? CascadeDistributionExponent : 4;
}


float FDirectionalLightSceneProxy::GetDistanceFieldShadowDistance() const
{
	return TEXT("r.GenerateMeshDistanceFields") != 0 ? 
		DistanceFieldShadowDistance * TEXT("r.DFDistanceScale") : 0.0f;
}

uint32 FDirectionalLightSceneProxy::GetNumViewDependentWholeSceneShadows(const FSceneView& View, bool bPrecomputedLightingIsValid) const override
{
	uint32 ClampedFarShadowCascadeCount = FMath::Min(FarShadowCascadeCount, TEXT("r.Shadow.MaxNumFarShadowCascades"));

	uint32 TotalCascades = GetNumShadowMappedCascades(View.MaxShadowCascades, bPrecomputedLightingIsValid) + ClampedFarShadowCascadeCount;

	return TotalCascades;
}

float FDirectionalLightSceneProxy::GetSplitDistance(const FSceneView& View, uint32 SplitIndex, bool bPrecomputedLightingIsValid, bool bDistanceFieldShadows) const
{
	uint32 NumNearCascades = GetNumShadowMappedCascades(View.MaxShadowCascades, bPrecomputedLightingIsValid);
	float CascadeDistanceWithoutFar = GetCSMMaxDistance(bPrecomputedLightingIsValid, View.MaxShadowCascades);
	float ShadowNear = View.NearClippingDistance;
	const float EffectiveCascadeDistributionExponent = GetEffectiveCascadeDistributionExponent(bPrecomputedLightingIsValid);

	if(SplitIndex > NumNearCascades)
	{
		if(bDistanceFieldShadows)
		{
			check(SplitIndex == NumNearCascades + 1);
			return GetDistanceFieldShadowDistance();
		}
		else
		{
			uint32 ClampedFarShadowCascadeCount = FMath::Min(FarShadowCascadeCount, TEXT("r.Shadow.MaxNumFarShadowCascades"));
			return CascadeDistanceWithoutFar + (FarShadowDistance - CascadeDistanceWithoutFar) *
				ComputeAccumulatedScale(EffectiveCascadeDistributionExponent, SplitIndex - NumNearCascades, ClampedFarShadowCascadeCount);
		}
	}
	else
	{
		return ShadowNear + (CascadeDistanceWithoutFar - ShadowNear) *
			ComputeAccumulatedScale(EffectiveCascadeDistributionExponent, SplitIndex, NumNearCascades);
	}
}


class FShadowCascadeSettings {
	float SplitNear = 0.0f;
	float SplitFar = WORLD_MAX;
	float SplitNearFadeRegion = 0.0f;
	float SplitFarFadeRegion = 0.0f;
	float FadePlaneOffset = SplitFar;
	float FadePlaneLength = SplitFar - FadePlaneOffset; // == SplitFarFadeRegion
	
	FConvexVolume ShadowBoundsAccurate;// The accurate bounds of the cascade used for primitive culling.
	FPlane NearFrustumPlane;
	FPlane FarFrustumPlane;

	int32 ShadowSplitIndex = INDEX_NONE;/* Index of the split if this is a whole scene shadow from a directional light, Or index of the direction if this is a whole scene shadow from a point light, otherwise INDEX_NONE. */
	float CascadeBiasDistribution = 1;/** Strength of depth bias across cascades. */	
	bool bFarShadowCascade = false;/** When enabled, the cascade only renders objects marked with bCastFarShadows enabled (e.g. Landscape). */
};

bool FDirectionalLightSceneProxy::GetViewDependentWholeSceneProjectedShadowInitializer(const FSceneView& View, int32 InCascadeIndex, bool bPrecomputedLightingIsValid, FWholeSceneProjectedShadowInitializer& OutInitializer) const override
{
	FSphere Bounds = GetShadowSplitBounds(View, InCascadeIndex, bPrecomputedLightingIsValid, &OutInitializer.CascadeSettings);

	OutInitializer.PreShadowTranslation = -Bounds.Center;
	OutInitializer.WorldToLight = FInverseRotationMatrix(GetDirection().GetSafeNormal().Rotation());
	OutInitializer.Scales = FVector(1.0f, 1.0f/Bounds.W, 1.0f/Bounds.W);
	OutInitializer.FaceDirection = FVector(1,0,0);
	OutInitializer.SubjectBounds = FBoxSphereBounds(
		FVector::ZeroVector, 
		FVector(Bounds.W/FMath::Sqrt(3.0f)), 
		Bounds.W );
	OutInitializer.WAxis = FVector4(0,0,0,1);
	OutInitializer.MinLightW = -HALF_WORLD_MAX;
	OutInitializer.MaxDistanceToCastInLightW = HALF_WORLD_MAX / 32.0f;// Reduce casting distance on a directional light. This is necessary to improve floating point precision in several places, especially when deriving frustum verts from InvReceiverMatrix
	OutInitializer.bRayTracedDistanceField = InCascadeIndex == INDEX_NONE;
	
	return true;
}

FSphere FDirectionalLightSceneProxy::GetShadowSplitBounds(const FSceneView& View, int32 InCascadeIndex, bool bPrecomputedLightingIsValid, FShadowCascadeSettings* OutCascadeSettings) const override
{
	uint32 NumNearCascades = GetNumShadowMappedCascades(View.MaxShadowCascades, bPrecomputedLightingIsValid);
	const bool bHasRayTracedCascade = ShouldCreateRayTracedCascade(View.GetFeatureLevel(), bPrecomputedLightingIsValid, View.MaxShadowCascades);
	uint32 NumNearAndFarCascades = GetNumViewDependentWholeSceneShadows(View, bPrecomputedLightingIsValid);
	uint32 NumTotalCascades = FMath::Max(NumNearAndFarCascades, NumNearCascades + (bHasRayTracedCascade ? 1 : 0));

	const bool bIsRayTracedCascade = InCascadeIndex == INDEX_NONE;
	const uint32 ShadowSplitIndex = bIsRayTracedCascade ? NumNearCascades : InCascadeIndex;

	float SplitNear = GetSplitDistance(View, ShadowSplitIndex, bPrecomputedLightingIsValid, bIsRayTracedCascade);
	float SplitFar = GetSplitDistance(View, ShadowSplitIndex + 1, bPrecomputedLightingIsValid, bIsRayTracedCascade);
	float FadePlane = SplitFar;

	float LocalCascadeTransitionFraction = CascadeTransitionFraction * TEXT("r.Shadow.CSM.TransitionScale");
	float FadeExtension = (SplitFar - SplitNear) * LocalCascadeTransitionFraction;

	if ((int32)ShadowSplitIndex < (int32)NumTotalCascades - 1)
	{
		SplitFar += FadeExtension;
	}
	else if (!(bPrecomputedLightingIsValid && bStaticShadowing))// Only do this if there is no static shadowing taking over after the fade
	{
		FadePlane -= FadeExtension;
	}

	if(OutCascadeSettings)
	{
		OutCascadeSettings->SplitFarFadeRegion = FadeExtension;
		OutCascadeSettings->SplitNearFadeRegion = 0.0f;
		if(ShadowSplitIndex >= 1)
		{
			float BeforeSplitNear = GetSplitDistance(View, ShadowSplitIndex - 1, bPrecomputedLightingIsValid, bIsRayTracedCascade);
			float BeforeSplitFar = GetSplitDistance(View, ShadowSplitIndex, bPrecomputedLightingIsValid, bIsRayTracedCascade);
			OutCascadeSettings->SplitNearFadeRegion = (BeforeSplitFar - BeforeSplitNear) * LocalCascadeTransitionFraction;
		}

		// Pass out the split settings
		OutCascadeSettings->SplitFar = SplitFar;
		OutCascadeSettings->SplitNear = SplitNear;
		OutCascadeSettings->FadePlaneOffset = FadePlane;
		OutCascadeSettings->FadePlaneLength = SplitFar - FadePlane;
		OutCascadeSettings->CascadeBiasDistribution = ShadowCascadeBiasDistribution;
		OutCascadeSettings->ShadowSplitIndex = (int32)ShadowSplitIndex;
		OutCascadeSettings->bFarShadowCascade = !bRayTracedCascade && ShadowSplitIndex >= (int32)NumNearCascades;
	}

	return GetShadowSplitBoundsDepthRange(View, View.ViewMatrices.GetViewOrigin(), SplitNear, SplitFar, OutCascadeSettings);
}

FSphere FDirectionalLightSceneProxy::GetShadowSplitBoundsDepthRange(const FSceneView& View, FVector ViewOrigin, float SplitNear, float SplitFar, FShadowCascadeSettings* OutCascadeSettings) const override
{
	// Get the 8 corners of the cascade's camera frustum, in world space
	FVector CascadeFrustumVerts[8];
	...
	
	// Fit a bounding sphere around the world space camera cascade frustum.
	// Solve CentreZ for the equation [dist(CentreZ, NearTopRightVertex) = dist(CentreZ, FarTopRightVertex)]
	...
	float OptimalOffset = (DiagonalBSq - DiagonalASq) / (2.0f * FrustumLength) + FrustumLength * 0.5f;
	float CentreZ = SplitFar - OptimalOffset;

	// Clamp for nonusual cases
	CentreZ = FMath::Clamp( CentreZ, SplitNear, SplitFar );

	FSphere CascadeSphere(ViewOrigin + CameraDirection * CentreZ, 0);
	for (int32 Index = 0; Index < 8; Index++)
	{
		CascadeSphere.W = FMath::Max(CascadeSphere.W, FVector::DistSquared(CascadeFrustumVerts[Index], CascadeSphere.Center));
	}

	CascadeSphere.W = FMath::Sqrt(CascadeSphere.W); 

	if (OutCascadeSettings)
	{
		// this function is needed, since it's also called by the LPV code.
		ComputeShadowCullingVolume(
			View.bReverseCulling, 
			CascadeFrustumVerts, 
			LightDirection, 
			OutCascadeSettings->ShadowBoundsAccurate, 
			OutCascadeSettings->NearFrustumPlane, 
			OutCascadeSettings->FarFrustumPlane);
	}

	return CascadeSphere;
}

void FDirectionalLightSceneProxy::ComputeShadowCullingVolume(, 
	const FVector* CascadeFrustumVerts, 
	const FVector& LightDirection, 
	FConvexVolume& ConvexVolumeOut, 
	FPlane& NearPlaneOut, 
	FPlane& FarPlaneOut) const
{
	check(!bReverseCulling);

	// Pairs of plane indices from SubFrustumPlanes whose intersections form the edges of the frustum.
	static const int32 AdjacentPlanePairs[12][2] =
	{
		{0,2}, {0,4}, {0,1}, {0,3},
		{2,3}, {4,2}, {1,4}, {3,1},
		{2,5}, {4,5}, {1,5}, {3,5}
	};
	// Maps a plane pair index to the index of the two frustum corners which form the end points of the plane intersection.
	static const int32 LineVertexIndices[12][2] =
	{
		{0,1}, {1,3}, {3,2}, {2,0},
		{0,4}, {1,5}, {3,7}, {2,6},
		{4,5}, {5,7}, {7,6}, {6,4}
	};

	TArray<FPlane, TInlineAllocator<6>> Planes;

	FPlane SubFrustumPlanes[6];
	SubFrustumPlanes[0] = FPlane(CascadeFrustumVerts[3], CascadeFrustumVerts[2], CascadeFrustumVerts[0]); // Near
	SubFrustumPlanes[1] = FPlane(CascadeFrustumVerts[7], CascadeFrustumVerts[6], CascadeFrustumVerts[2]); // Left
	SubFrustumPlanes[2] = FPlane(CascadeFrustumVerts[0], CascadeFrustumVerts[4], CascadeFrustumVerts[5]); // Right
	SubFrustumPlanes[3] = FPlane(CascadeFrustumVerts[2], CascadeFrustumVerts[6], CascadeFrustumVerts[4]); // Top
	SubFrustumPlanes[4] = FPlane(CascadeFrustumVerts[5], CascadeFrustumVerts[7], CascadeFrustumVerts[3]); // Bottom
	SubFrustumPlanes[5] = FPlane(CascadeFrustumVerts[4], CascadeFrustumVerts[6], CascadeFrustumVerts[7]); // Far

	// Add the planes from the camera's frustum which form the back face of the frustum when in light space.
	for (int32 i = 0; i < 6; i++)
	{
		FVector Normal(SubFrustumPlanes[i]);
		if ( Normal | LightDirection < 0.0f )
		{
			Planes.Add(SubFrustumPlanes[i]);
		}
	}

	// Now add the planes which form the silhouette edges of the camera frustum in light space.
	for (int32 i = 0; i < 12; i++)
	{
		FVector NormalA(SubFrustumPlanes[AdjacentPlanePairs[i][0]]);
		FVector NormalB(SubFrustumPlanes[AdjacentPlanePairs[i][1]]);
		float DotA = NormalA | LightDirection;
		float DotB = NormalB | LightDirection;

		if ( DotA * DotB < 0.0f ) // If the signs of the dot product are different
		{
			FVector A = CascadeFrustumVerts[LineVertexIndices[i][0]];
			FVector B = CascadeFrustumVerts[LineVertexIndices[i][1]];
			FVector C = A + LightDirection; // Extrude the plane along the light direction, and add it to the array.

			if (DotA >= 0.0f) // Account for winding
			{
				Planes.Add(FPlane(A, B, C));
			}
			else
			{
				Planes.Add(FPlane(B, A, C));
			}
		}
	}

	ConvexVolumeOut = FConvexVolume(Planes);
	NearPlaneOut = SubFrustumPlanes[0];
	FarPlaneOut = SubFrustumPlanes[5];
}