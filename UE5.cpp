/*
For performance, UE5 add features :
TEXT("r.VolumetricRenderTarget.Mode") <- 3
UVolumetricCloudComponent::StopTracingTransmittanceThreshold
TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity")
TEXT("r.VolumetricCloud.DisableCompute")
*/


// VolumetricRenderTarget.cpp
TEXT("r.VolumetricRenderTarget.Mode"), <- 3
TEXT("r.VolumetricRenderTarget.PreferAsyncCompute")
TEXT("r.VolumetricRenderTarget.ReprojectionBoxConstraint")

bool IsVolumetricRenderTargetAsyncCompute()
{
	return GSupportsEfficientAsyncCompute && TEXT("r.VolumetricRenderTarget.PreferAsyncCompute");
}


void FDeferredShadingSceneRenderer::Render(FRDGBuilder& GraphBuilder)
{
	const bool bShouldRenderSkyAtmosphere = ShouldRenderSkyAtmosphere(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloudBase = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);
	const bool bShouldRenderVolumetricCloud = bShouldRenderVolumetricCloudBase && !ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity;
	const bool bShouldVisualizeVolumetricCloud = bShouldRenderVolumetricCloudBase && !!ViewFamily.EngineShowFlags.VisualizeVolumetricCloudConservativeDensity;
	bool bAsyncComputeVolumetricCloud = IsVolumetricRenderTargetEnabled() && IsVolumetricRenderTargetAsyncCompute();
	bool bHasHalfResCheckerboardMinMaxDepth = false;
	bool bVolumetricRenderTargetRequired = bShouldRenderVolumetricCloud;

	if (bShouldRenderVolumetricCloudBase)
	{
		InitVolumetricRenderTargetForViews(GraphBuilder, Views);
	}

	InitVolumetricCloudsForViews(GraphBuilder, bShouldRenderVolumetricCloudBase, InstanceCullingManager);

	FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture = nullptr;

	// Kick off async compute cloud eraly if all depth has been written in the prepass
	if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud && DepthPass.EarlyZPassMode == DDM_AllOpaque)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
		bHasHalfResCheckerboardMinMaxDepth = true;

		bool bSkipVolumetricRenderTarget = false;
		bool bSkipPerPixelTracing = true;
		bAsyncComputeVolumetricCloud = RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, true, InstanceCullingManager);
	}

	// If not all depth is written during the prepass, kick off async compute cloud after basepass
	if (bShouldRenderVolumetricCloud && bAsyncComputeVolumetricCloud && DepthPass.EarlyZPassMode != DDM_AllOpaque)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
		bHasHalfResCheckerboardMinMaxDepth = true;

		bool bSkipVolumetricRenderTarget = false;
		bool bSkipPerPixelTracing = true;
		bAsyncComputeVolumetricCloud = RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, true, InstanceCullingManager);
	}

	if (bShouldRenderVolumetricCloud && IsVolumetricRenderTargetEnabled() && !bHasHalfResCheckerboardMinMaxDepth)
	{
		HalfResolutionDepthCheckerboardMinMaxTexture = CreateHalfResolutionDepthCheckerboardMinMax(GraphBuilder, Views, SceneTextures.Depth.Resolve);
	}

	// This is default clourd render
	if (bShouldRenderVolumetricCloud)
	{
		if (!bAsyncComputeVolumetricCloud)
		{
			// Generate the volumetric cloud render target
			bool bSkipVolumetricRenderTarget = false;
			bool bSkipPerPixelTracing = true;
			RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		}
		// Reconstruct the volumetric cloud render target to be ready to compose it over the scene
		ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, bAsyncComputeVolumetricCloud);
	}

	// After the height fog, Draw volumetric clouds (having fog applied on them already) when using per pixel tracing,
	if (bShouldRenderVolumetricCloud)
	{
		bool bSkipVolumetricRenderTarget = true;
		bool bSkipPerPixelTracing = false;
		RenderVolumetricCloud(GraphBuilder, SceneTextures, bSkipVolumetricRenderTarget, bSkipPerPixelTracing, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
	}

	if (bShouldVisualizeVolumetricCloud)
	{
		RenderVolumetricCloud(GraphBuilder, SceneTextures, false, true, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		ReconstructVolumetricRenderTarget(GraphBuilder, Views, SceneTextures.Depth.Resolve, HalfResolutionDepthCheckerboardMinMaxTexture, false);
		ComposeVolumetricRenderTargetOverSceneForVisualization(GraphBuilder, Views, SceneTextures.Color.Target);
		RenderVolumetricCloud(GraphBuilder, SceneTextures, true, false, HalfResolutionDepthCheckerboardMinMaxTexture, false, InstanceCullingManager);
		AddServiceLocalQueuePass(GraphBuilder);
	}
}

bool FSceneRenderer::RenderVolumetricCloud(
	FRDGBuilder& GraphBuilder,
	const FMinimalSceneTextures& SceneTextures,
	bool bSkipVolumetricRenderTarget,
	bool bSkipPerPixelTracing,
	FRDGTextureRef HalfResolutionDepthCheckerboardMinMaxTexture,
	bool bAsyncCompute,
	FInstanceCullingManager& InstanceCullingManager)
{

}

// VolumetricCloudRendering.cpp
TEXT("r.VolumetricCloud.SampleMinCount"), 2
TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity"), 1
TEXT("r.VolumetricCloud.DisableCompute"), 0

UVolumetricCloudComponent::StopTracingTransmittanceThreshold = 0.005f;

BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonShaderParameters, )
	SHADER_PARAMETER(int32, SampleCountMin)
	SHADER_PARAMETER(float, StopTracingTransmittanceThreshold)
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudGlobalParameters, )
	SHADER_PARAMETER(int32, VirtualShadowMapId0)
	SHADER_PARAMETER(FUintVector4, SceneDepthTextureMinMaxCoord)
	SHADER_PARAMETER(int32, StepSizeOnZeroConservativeDensity)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudRenderViewParametersPS, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRenderVolumetricCloudGlobalParameters, VolumetricCloudRenderViewParamsUB)
	SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams, InstanceCullingDrawParams)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVirtualShadowMapSamplingParameters, VirtualShadowMap)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

uint32 GetVolumetricCloudDebugSampleCountMode(const FEngineShowFlags& ShowFlags)
{
	if (ShowFlags.VisualizeVolumetricCloudConservativeDensity)
	{
		return 6;
	}
	// Add view modes for visualize other kinds of sample count
	return FMath::Clamp(TEXT("r.VolumetricCloud.Debug.SampleCountMode"), 0, 5);
}

void SetupDefaultRenderVolumetricCloudGlobalParameters(FRDGBuilder& GraphBuilder, FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams, FVolumetricCloudRenderSceneInfo& CloudInfo, FViewInfo& ViewInfo)
{
	VolumetricCloudParams.VirtualShadowMapId0 = INDEX_NONE;
	VolumetricCloudParams.SampleCountDebugMode = GetVolumetricCloudDebugSampleCountMode(ViewInfo.Family->EngineShowFlags);
	VolumetricCloudParams.StepSizeOnZeroConservativeDensity = FMath::Max(TEXT("r.VolumetricCloud.StepSizeOnZeroConservativeDensity"), 1);
}

TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> CreateCloudPassUniformBuffer(FRDGBuilder& GraphBuilder, FCloudRenderContext& CloudRC)
{
	FRenderVolumetricCloudGlobalParameters& VolumetricCloudParams = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
	SetupDefaultRenderVolumetricCloudGlobalParameters(GraphBuilder, VolumetricCloudParams, CloudInfo, MainView);

	const uint32 VolumetricReconstructRTDownsampleFactor = MainView.ViewState->VolumetricCloudRenderTarget.GetVolumetricReconstructRTDownsampleFactor();

	// Use the main view to get the target rect. The clouds are reconstructed at the same resolution as the depth buffer read when tracing.
	VolumetricCloudParams.SceneDepthTextureMinMaxCoord = FUintVector4(
		MainView.ViewRect.Min.X / VolumetricReconstructRTDownsampleFactor,
		MainView.ViewRect.Min.Y / VolumetricReconstructRTDownsampleFactor,
		(MainView.ViewRect.Max.X - 1) / VolumetricReconstructRTDownsampleFactor,
		(MainView.ViewRect.Max.Y - 1) / VolumetricReconstructRTDownsampleFactor);

	return GraphBuilder.CreateUniformBuffer(&VolumetricCloudParams);
}

void FSceneRenderer::InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder, bool bShouldRenderVolumetricCloud, FInstanceCullingManager& InstanceCullingManager)
{
	CloudGlobalShaderParams.SampleCountMin		= FMath::Max(0, TEXT("r.VolumetricCloud.SampleMinCount"));
	CloudGlobalShaderParams.StopTracingTransmittanceThreshold = FMath::Clamp(CloudProxy.StopTracingTransmittanceThreshold, 0.0f, 1.0f);
}




// invalid shadow map, instead of shadow ray


TEXT("ShowFlag.VisualizeVolumetricCloudConservativeDensity")
TEXT("r.VolumetricCloud.Debug.SampleCountMode"), 0,
TEXT("Only for developers. 
	[0] Disabled 
	[1] Primary material sample count 
	[2] Advanced:raymarched shadow sample count 
	[3] Shadow material sample count")


bool TraceClouds(...)
{
	float t = TMin + 0.5 * StepT;

	for (uint i = 0; i < IStepCount; ++i)
	{
		float3 SampleWorldPosition = RayOrigin + t * Raydir;

		UpdateMaterialCloudParam(MaterialParameters, SampleWorldPosition, ResolvedView, CloudLayerParams);

	#if MATERIAL_VOLUMETRIC_ADVANCED_CONSERVATIVE_DENSITY
		if (MaterialParameters.VolumeSampleConservativeDensity.x <= 0.0f)
		{
			++DebugZeroConservativeDensitySampleCount;
			t += StepT;
			continue; // Conservative density is 0 so skip and go to the next sample
		}
	#endif

		DebugPrimaryMaterialSampleCount++;
		CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

		if (any(PMC.ScatteringCoefficients[0] > 0.0f))
		{
		#if MATERIAL_VOLUMETRIC_ADVANCED_RAYMARCH_VOLUME_SHADOW==1
			DebugShadowSampleCount++;

			const float ShadowLengthTest = RenderVolumetricCloudParameters.ShadowTracingMaxDistance;
			const float ShadowStepCount = float(RenderVolumetricCloudParameters.ShadowSampleCountMax);
			const float InvShadowStepCount = 1.0f / ShadowStepCount;

			for (float ShadowT = InvShadowStepCount; ShadowT <= 1.0; ShadowT += InvShadowStepCount)
			{
				UpdateMaterialCloudParam(MaterialParameters, SampleWorldPosition + Light0Direction * ShadowLengthTest * (PreviousNormT + DetlaNormT * ShadowJitterNorm), ResolvedView, CloudLayerParams);

				if (MaterialParameters.VolumeSampleConservativeDensity.x <= 0.0f)
				{
					continue; // Conservative density is 0 so skip and go to the next sample
				}
				if (MaterialParameters.CloudSampleNormAltitudeInLayer < 0.0f || MaterialParameters.CloudSampleNormAltitudeInLayer > 1.0f)
				{
					break; // Ignore remaining samples since we have just traveld out of the cloud layer.
				}

				DebugShadowMaterialSampleCount++;
				CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);
				...
			}
		#endif
		}

		if (all(TransmittanceToView < RenderVolumetricCloudParameters.StopTracingTransmittanceThreshold)
			&& (!CLOUD_SAMPLE_COUNT_DEBUG_MODE || RenderVolumetricCloudParameters.SampleCountDebugMode < 6))
		{
			break;
		}

		t += StepT;
	}

#if CLOUD_SAMPLE_COUNT_DEBUG_MODE
	if (RenderVolumetricCloudParameters.SampleCountDebugMode == 6)
	{
		OutColor0.r = 1.0 - DebugZeroConservativeDensitySampleCount / float(IStepCount);
	}
	else if (RenderVolumetricCloudParameters.SampleCountDebugMode > 0)
	{
		uint CountToDebug = 0;
		switch (RenderVolumetricCloudParameters.SampleCountDebugMode)
		{
		case 1:
			CountToDebug = DebugPrimaryMaterialSampleCount;
			break;
		case 2:
			CountToDebug = DebugShadowSampleCount;
			break;
		case 3:
			CountToDebug = DebugShadowMaterialSampleCount;
			break;
		}

		OutColor0 = float4( GetColorCode( saturate(CountToDebug/64.0f) ), 0.0f );
	}
#endif
}

void MainCommon(in FMaterialPixelParameters MaterialParameters, in float4 SvPosition, out float4 OutColor0, out float2 OutColor1)
{
	if (!TraceClouds(...))
	{
		return;
	}

	float GrayScaleTransmittance = MeanTransmittance < RenderVolumetricCloudParameters.StopTracingTransmittanceThreshold ? 0.0f : MeanTransmittance;
#if CLOUD_SAMPLE_COUNT_DEBUG_MODE
	OutColor0 = float4(OutColor0.r * OutputPreExposure, Luminance.gb * OutputPreExposure, GrayScaleTransmittance);
#else
	OutColor0 = float4(Luminance * OutputPreExposure, GrayScaleTransmittance);
#endif

	OutColor1.x = MaxHalfFloat; // Default to far away depth to be flat and not intersect with any geometry.
	if (RenderVolumetricCloudParameters.OpaqueIntersectionMode >= 1)
	{
		OutColor1.x = ((GrayScaleTransmittance > 0.99) ? NoCloudDepth : tAP) * CENTIMETER_TO_KILOMETER; // using a small threshold on transmittance
	}
}
