class FSceneViewState : public FSceneViewStateInterface, public FRenderResource {
	FVolumetricRenderTargetViewStateData VolumetricCloudRenderTarget;
}

class FVolumetricRenderTargetViewStateData {
	bool bFirstTimeUsed;
	uint32 CurrentRT;
	bool bHistoryValid;
	
	int32 FrameId;
	FIntPoint CurrentPixelOffset;
	
	uint32 NoiseFrameIndex;
	uint32 NoiseFrameIndexModPattern;

	float UvNoiseScale;
	int32 Mode;
	int32 UpsamplingMode;

	FIntPoint FullResolution;

	uint32 VolumetricReconstructRTDownsampleFactor;
	FIntPoint VolumetricReconstructRTResolution;
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRT[2];
	TRefCountPtr<IPooledRenderTarget> VolumetricReconstructRTDepth[2];

	uint32 VolumetricTracingRTDownsampleFactor;
	FIntPoint VolumetricTracingRTResolution;
	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRT;
	TRefCountPtr<IPooledRenderTarget> VolumetricTracingRTDepth;
}

FVolumetricRenderTargetViewStateData::FVolumetricRenderTargetViewStateData()
	: CurrentRT(1)
	, bFirstTimeUsed(true)
	, bHistoryValid(false)
	, FullResolution(FIntPoint::ZeroValue)
	, VolumetricReconstructRTResolution(FIntPoint::ZeroValue)
	, VolumetricTracingRTResolution(FIntPoint::ZeroValue)
{
	VolumetricReconstructRTDownsampleFactor = 0;
	VolumetricTracingRTDownsampleFactor = 0;
	FrameId = 0;
	NoiseFrameIndex = 0;
	NoiseFrameIndexModPattern = 0;
	CurrentPixelOffset = FIntPoint::ZeroValue;
}

static uint32 GetMainDownsampleFactor(int32 Mode)
{
	return Mode == 0 ? 2 : 1;
}

static uint32 GetTraceDownsampleFactor(int32 Mode)
{
	return Mode < 2 ? 2 : 4;
}

void FVolumetricRenderTargetViewStateData::Initialise(
	FIntPoint& ViewRectResolutionIn,
	float InUvNoiseScale,
	int32 InMode,
	int32 InUpsamplingMode)
{
	UvNoiseScale = InUvNoiseScale;
	Mode = FMath::Clamp(InMode, 0, 2);
	UpsamplingMode = Mode == 2 ? 2 : FMath::Clamp(InUpsamplingMode, 0, 4); // if we are using mode 2 then we cannot intersect with depth and upsampling should be 2 (simple on/off intersection)

	const uint32 PreviousRT = CurrentRT;
	CurrentRT = 1 - CurrentRT;

	if (FullResolution != ViewRectResolutionIn || GetMainDownsampleFactor(Mode) != VolumetricReconstructRTDownsampleFactor || GetTraceDownsampleFactor(Mode) != VolumetricTracingRTDownsampleFactor)
	{
		VolumetricReconstructRTDownsampleFactor = GetMainDownsampleFactor(Mode);
		VolumetricTracingRTDownsampleFactor = GetTraceDownsampleFactor(Mode);
		FullResolution = ViewRectResolutionIn;
		VolumetricReconstructRTResolution = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);							// Half resolution
		VolumetricTracingRTResolution = FIntPoint::DivideAndRoundUp(VolumetricReconstructRTResolution, VolumetricTracingRTDownsampleFactor);	// Half resolution of the volumetric buffer

		VolumetricTracingRT.SafeRelease();
		VolumetricTracingRTDepth.SafeRelease();
	}

	FIntVector CurrentTargetResVec = VolumetricReconstructRT[CurrentRT].IsValid() ? VolumetricReconstructRT[CurrentRT]->GetDesc().GetSize() : FIntVector::ZeroValue;
	FIntPoint CurrentTargetRes = FIntPoint::DivideAndRoundUp(FullResolution, VolumetricReconstructRTDownsampleFactor);
	if (VolumetricReconstructRT[CurrentRT].IsValid() && FIntPoint(CurrentTargetResVec) != CurrentTargetRes)
	{
		VolumetricReconstructRT[CurrentRT].SafeRelease();
		VolumetricReconstructRTDepth[CurrentRT].SafeRelease();
	}

	// Regular every frame update
	{
		bHistoryValid = VolumetricReconstructRT[PreviousRT].IsValid(); // true except at very first

		NoiseFrameIndex += FrameId == 0 ? 1 : 0;
		NoiseFrameIndexModPattern = NoiseFrameIndex % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

		FrameId++;
		FrameId = FrameId % (VolumetricTracingRTDownsampleFactor * VolumetricTracingRTDownsampleFactor);

		if (VolumetricTracingRTDownsampleFactor == 2)
		{
			static int32 OrderDithering2x2[4] = { 0, 2, 3, 1 };
			int32 LocalFrameId = OrderDithering2x2[FrameId];
			CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
		}
		else if (VolumetricTracingRTDownsampleFactor == 4)
		{
			static int32 OrderDithering4x4[16] = { 0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5 };
			int32 LocalFrameId = OrderDithering4x4[FrameId];
			CurrentPixelOffset = FIntPoint(LocalFrameId % VolumetricTracingRTDownsampleFactor, LocalFrameId / VolumetricTracingRTDownsampleFactor);
		}
	}
}


void FSceneRenderer::InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder)
{
	FViewInfo& ViewInfo = Views[0];
	FVolumetricRenderTargetViewStateData& VRTData = ViewInfo.ViewState->VolumetricCloudRenderTarget;

	VRTData.Initialise(	// TODO this is going to reallocate a buffer each time dynamic resolution scaling is applied 
		ViewInfo.ViewRect.Size(),
		TEXT("r.VolumetricRenderTarget.UvNoiseScale"), 
		TEXT("r.VolumetricRenderTarget.Mode"),
		TEXT("r.VolumetricRenderTarget.UpsamplingMode"));

	FViewUniformShaderParameters ViewVolumetricCloudRTParameters = *ViewInfo.CachedViewUniformShaderParameters;
	{
		FViewMatrices ViewMatrices = ViewInfo.ViewMatrices;
		{
			const uint32 ReconstructDownSample = VRTData.VolumetricReconstructRTDownsampleFactor;
			const FIntPoint& CurrentPixelOffset = VRTData.CurrentPixelOffset;
			const FIntPoint& ReconstructResolution = VRTData.VolumetricReconstructRTResolution;
			FVector2D CenterCoord = FVector2D(ReconstructDownSample / 2.0f);
			FVector2D TargetCoord = FVector2D(CurrentPixelOffset) + FVector2D(0.5f, 0.5f);
			FVector2D OffsetCoord = (TargetCoord - CenterCoord) * (FVector2D(-2.0f, 2.0f) / FVector2D(ReconstructResolution));
			
			ViewMatrices.HackRemoveTemporalAAProjectionJitter();
			ViewMatrices.HackAddTemporalAAProjectionJitter(OffsetCoord);
		}

		const FIntPoint& TracingResolution = VRTData.VolumetricTracingRTResolution;

		ViewInfo.SetupViewRectUniformBufferParameters(
			ViewVolumetricCloudRTParameters,
			TracingResolution,
			FIntRect(0, 0, TracingResolution.X, TracingResolution.Y),
			ViewMatrices, );
	}
	ViewInfo.VolumetricRenderTargetViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
		ViewVolumetricCloudRTParameters, UniformBuffer_SingleFrame);
}

class FScene : public FSceneInterface {
	FVolumetricCloudRenderSceneInfo* VolumetricCloud;
}

class FVolumetricCloudRenderSceneInfo {
	FVolumetricCloudSceneProxy& VolumetricCloudSceneProxy;
	FVolumetricCloudCommonShaderParameters VolumetricCloudCommonShaderParameters;
	TUniformBufferRef<FVolumetricCloudCommonGlobalShaderParameters> VolumetricCloudCommonShaderParametersUB;
};

BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonShaderParameters, )
	SHADER_PARAMETER(FLinearColor, GroundAlbedo)
	SHADER_PARAMETER(FVector, CloudLayerCenterKm)
	SHADER_PARAMETER(float, PlanetRadiusKm)
	SHADER_PARAMETER(float, BottomRadiusKm)
	SHADER_PARAMETER(float, TopRadiusKm)
	SHADER_PARAMETER(float, TracingStartMaxDistance)
	SHADER_PARAMETER(float, TracingMaxDistance)
	SHADER_PARAMETER(int32, SampleCountMax)
	SHADER_PARAMETER(float, InvDistanceToSampleCountMax)
	SHADER_PARAMETER(int32, ShadowSampleCountMax)
	SHADER_PARAMETER(float, ShadowTracingMaxDistance)
	SHADER_PARAMETER(float, SkyLightCloudBottomVisibility)
	SHADER_PARAMETER_ARRAY(FLinearColor, AtmosphericLightCloudScatteredLuminanceScale, [2])
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapFarDepthKm, [2])
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapStrength, [2])
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapDepthBias, [2])
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapSampleCount, [2])
	SHADER_PARAMETER_ARRAY(FVector4,CloudShadowmapSizeInvSize, [2])
	SHADER_PARAMETER_ARRAY(FVector4,CloudShadowmapTracingSizeInvSize, [2])
	SHADER_PARAMETER_ARRAY(FMatrix,	CloudShadowmapWorldToLightClipMatrix, [2])
	SHADER_PARAMETER_ARRAY(FMatrix,	CloudShadowmapWorldToLightClipMatrixInv, [2])
	SHADER_PARAMETER_ARRAY(FVector4, CloudShadowmapTracingPixelScaleOffset, [2])
	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightDir, [2])
	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightPos, [2])
	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightAnchorPos, [2])	// Snapped position on the planet the shadow map rotate around 
	SHADER_PARAMETER(float,		CloudSkyAOFarDepthKm)
	SHADER_PARAMETER(float,		CloudSkyAOStrength)
	SHADER_PARAMETER(float,		CloudSkyAOSampleCount)
	SHADER_PARAMETER(FVector4,	CloudSkyAOSizeInvSize)
	SHADER_PARAMETER(FMatrix,	CloudSkyAOWorldToLightClipMatrix)
	SHADER_PARAMETER(FMatrix,	CloudSkyAOWorldToLightClipMatrixInv)
	SHADER_PARAMETER(FVector,	CloudSkyAOTraceDir)
END_SHADER_PARAMETER_STRUCT()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonGlobalShaderParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumetricCloudCommonShaderParameters, VolumetricCloudCommonParams)
END_GLOBAL_SHADER_PARAMETER_STRUCT()


class UVolumetricCloudComponent : public USceneComponent {
	float PlanetRadius = 6360.0f;/** The planet radius used when there is not SkyAtmosphere component present in the scene. */
	float LayerBottomAltitude = 5.0f;/** The altitude at which the cloud layer starts. (kilometers above the ground) */
	float LayerHeight = 10.0f;/** The altitude at which the cloud layer ends. (kilometers above the ground) */
	float TracingStartMaxDistance = 350.0f;/** The maximum distance of the volumetric surface before which we will accept to start tracing. (kilometers) */
	float TracingMaxDistance = 50.0f;/** The maximum distance that will be traced inside the cloud layer. (kilometers) */
};

class ENGINE_API FVolumetricCloudSceneProxy {
	FVolumetricCloudRenderSceneInfo* RenderSceneInfo;

	float LayerBottomAltitudeKm;//InComponent->LayerBottomAltitude
	float LayerHeightKm;//InComponent->LayerHeight

	float TracingStartMaxDistance;//InComponent->TracingStartMaxDistance
	float TracingMaxDistance;//InComponent->TracingMaxDistance

	float PlanetRadiusKm;//InComponent->PlanetRadius
	FColor GroundAlbedo;//InComponent->GroundAlbedo
	bool bUsePerSampleAtmosphericLightTransmittance;//InComponent->bUsePerSampleAtmosphericLightTransmittance
	float SkyLightCloudBottomOcclusion;//InComponent->SkyLightCloudBottomOcclusion
	
	float ViewSampleCountScale;//InComponent->ViewSampleCountScale
	float ReflectionSampleCountScale;//InComponent->ReflectionSampleCountScale
	float ShadowViewSampleCountScale;//InComponent->ShadowViewSampleCountScale
	float ShadowReflectionSampleCountScale;//InComponent->ShadowReflectionSampleCountScale
	float ShadowTracingDistance;//InComponent->ShadowTracingDistance

	UMaterialInterface* CloudVolumeMaterial;//InComponent->Material
};

void FSceneRenderer::InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder)
{
	const FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo();
	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
	const FVolumetricCloudSceneProxy& CloudProxy = CloudInfo.GetVolumetricCloudSceneProxy();
	FLightSceneProxy* AtmosphericLight0 = Scene->AtmosphereLights[0] ? Scene->AtmosphereLights[0]->Proxy : nullptr;
	FLightSceneProxy* AtmosphericLight1 = Scene->AtmosphereLights[1] ? Scene->AtmosphereLights[1]->Proxy : nullptr;
	FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

	const float KilometersToCentimeters = 100000.0f;
	const float CentimetersToKilometers = 1.0f / KilometersToCentimeters;
	const float KilometersToMeters = 1000.0f;
	const float MetersToKilometers = 1.0f / KilometersToMeters;
	const float CloudShadowTemporalWeight = FMath::Min(FMath::Max(CVarVolumetricCloudShadowTemporalFilteringNewFrameWeight.GetValueOnRenderThread(), 0.0f), 1.0f);
	const bool CloudShadowTemporalEnabled = CloudShadowTemporalWeight < 1.0f;

	FVolumetricCloudCommonShaderParameters& Params = CloudInfo.VolumetricCloudCommonShaderParameters;
	float PlanetRadiusKm;
	
	if (SkyInfo)
	{
		const FAtmosphereSetup& AtmosphereSetup = SkyInfo->GetSkyAtmosphereSceneProxy().GetAtmosphereSetup();
		PlanetRadiusKm = AtmosphereSetup.BottomRadiusKm;
		Params.CloudLayerCenterKm = AtmosphereSetup.PlanetCenterKm;
	}
	else
	{
		PlanetRadiusKm = CloudProxy.PlanetRadiusKm;
		Params.CloudLayerCenterKm = FVector(0.0f, 0.0f, -PlanetRadiusKm);
	}

	Params.PlanetRadiusKm = PlanetRadiusKm;
	Params.BottomRadiusKm = PlanetRadiusKm + CloudProxy.LayerBottomAltitudeKm;
	Params.TopRadiusKm = Params.BottomRadiusKm + CloudProxy.LayerHeightKm;
	Params.GroundAlbedo = FLinearColor(CloudProxy.GroundAlbedo);
	Params.SkyLightCloudBottomVisibility = 1.0f - CloudProxy.SkyLightCloudBottomOcclusion;

	Params.TracingStartMaxDistance = KilometersToCentimeters * CloudProxy.TracingStartMaxDistance;
	Params.TracingMaxDistance		= KilometersToCentimeters * CloudProxy.TracingMaxDistance;
	...
}

void FSceneRenderer::RenderVolumetricCloudsInternal(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC)
{
	const bool bShouldViewRenderVolumetricRenderTarget = CloudRC.bShouldViewRenderVolumetricRenderTarget;
	const bool bIsReflectionRendering = CloudRC.bIsReflectionRendering;
	const bool bIsSkyRealTimeReflectionRendering = CloudRC.bIsSkyRealTimeReflectionRendering;
	const bool bSkipAtmosphericLightShadowmap = CloudRC.bSkipAtmosphericLightShadowmap;
	const bool bSecondAtmosphereLightEnabled = CloudRC.bSecondAtmosphereLightEnabled;

	if (bShouldViewRenderVolumetricRenderTarget && MainView.ViewState)
	{
		FVolumetricRenderTargetViewStateData& VRT = MainView.ViewState->VolumetricCloudRenderTarget;
		VolumetricCloudParams.OpaqueIntersectionMode = VRT.GetMode()== 2 ? 0 : 2;	// intersect with opaque only if not using mode 2 (full res distant cloud updating 1 out 4x4 pixels)
	}
	else
	{
		VolumetricCloudParams.OpaqueIntersectionMode = 2;	// always intersect with opaque
	}
}