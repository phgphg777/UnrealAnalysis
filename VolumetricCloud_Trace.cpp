///////////////////////////////////////////////////////////////////////////////
// To do for performance
// 1. Reduce tracing resolution (UE5 implements)
// 2. Modify & Utilize conservative density output
// 3. Reduce view/shadow ray samples
// 4. Reduce TracingStartMaxDistance
// 5. Add "if(SampleExtinctionCoefficients(PixelMaterialInputs) == 0.0) continue;" to view ray for-loop
///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// To analysis
// 1. atmosphere transmittance
// 2. cloud shadow (ok)
// 3. sky ao & TEXT("r.VolumetricCloud.HighQualityAerialPerspective")
// 4. pespective aerial
// 5. realtime capture
///////////////////////////////////////////////////////////////////////////////

class FScene : public FSceneInterface {
	FVolumetricCloudRenderSceneInfo* VolumetricCloud;
}

class FVolumetricCloudRenderSceneInfo {
	FVolumetricCloudSceneProxy& VolumetricCloudSceneProxy;
	FVolumetricCloudCommonShaderParameters VolumetricCloudCommonShaderParameters;
	TUniformBufferRef<FVolumetricCloudCommonGlobalShaderParameters> VolumetricCloudCommonShaderParametersUB;
};

class ENGINE_API UDirectionalLightComponent : public ULightComponent {
	uint32 bUsedAsAtmosphereSunLight = false;
	int32 AtmosphereSunLightIndex = 0;
	FLinearColor CloudScatteredLuminanceScale = FLinearColor::White;
	uint32 bCastShadowsOnClouds = false;		// Shadow-On-Cloud option
	uint32 bCastCloudShadows = false;			// Cloud-Shadow option
	float CloudShadowExtent = 150.0f;			// Cloud-Shadow option
	float CloudShadowStrength = 1.0f;			// Cloud-Shadow option
	float CloudShadowMapResolutionScale = 1.0f;	// Cloud-Shadow option
	float CloudShadowRaySampleCountScale = 1.0f;// Cloud-Shadow option
	float CloudShadowDepthBias = 0.0f;			// Cloud-Shadow option
	//FLinearColor AtmosphereSunDiskColorScale = FLinearColor::White;
	//uint32 bPerPixelAtmosphereTransmittance = false;
	//uint32 bCastShadowsOnAtmosphere = false;
	//float CloudShadowOnAtmosphereStrength = 1.0f;
	//float CloudShadowOnSurfaceStrength = 1.0f;
}

class UVolumetricCloudComponent : public USceneComponent {
	FColor GroundAlbedo = FColor(170, 170, 170);
	uint32 bUsePerSampleAtmosphericLightTransmittance = false; 
	float SkyLightCloudBottomOcclusion = 0.5f;
	float PlanetRadius = 6360.0f;
	float LayerBottomAltitude = 5.0f;
	float LayerHeight = 10.0f;
	float TracingStartMaxDistance = 350.0f;		// View-Ray option
	float TracingMaxDistance = 50.0f;			// View-Ray option
	float ViewSampleCountScale = 1.0f;			// View-Ray option
	float ShadowViewSampleCountScale = 1.0f;	// Shadow-Ray option
	float ShadowTracingDistance = 15.0f;		// Shadow-Ray option
	//float ReflectionSampleCountScale = 1.0f;
	//float ShadowReflectionSampleCountScale = 1.0f;
	UMaterialInterface* Material = TEXT("/Engine/EngineSky/VolumetricClouds/m_SimpleVolumetricCloud_Inst.m_SimpleVolumetricCloud_Inst");
	FVolumetricCloudSceneProxy* VolumetricCloudSceneProxy = nullptr;
}

TEXT("r.VolumetricCloud"), 1,
TEXT("r.VolumetricCloud.DistanceToSampleMaxCount"), 15.0f,				// View-Ray option
TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"), 768, 					// View-Ray option
TEXT("r.VolumetricCloud.EnableDistantSkyLightSampling"), 1,
TEXT("r.VolumetricCloud.EnableAtmosphericLightsSampling"), 1,
TEXT("r.VolumetricCloud.EnableAerialPerspectiveSampling"), 1,
TEXT("r.VolumetricCloud.HighQualityAerialPerspective"), 0,	
TEXT("r.VolumetricCloud.Shadow.ViewRaySampleMaxCount"), 80,				// Shadow-Ray option
TEXT("r.VolumetricCloud.Shadow.SampleAtmosphericLightShadowmap"), 1,	// Shadow-On-Cloud option
//TEXT("r.VolumetricCloud.ReflectionRaySampleMaxCount"), 80,
//TEXT("r.VolumetricCloud.Shadow.ReflectionRaySampleMaxCount"), 24,	
//TEXT("r.VolumetricCloud.HzbCulling"), 1,			
//TEXT("r.VolumetricCloud.SkyAO"), 1,		
//TEXT("r.VolumetricCloud.SkyAO.Debug"), 0,
//TEXT("r.VolumetricCloud.SkyAO.SnapLength"), 20.0f,
//TEXT("r.VolumetricCloud.SkyAO.MaxResolution"), 2048,
//TEXT("r.VolumetricCloud.SkyAO.TraceSampleCount"), 10,
//TEXT("r.VolumetricCloud.SkyAO.Filtering"), 1,
TEXT("r.VolumetricCloud.ShadowMap"), 1,										// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.MaxResolution"), 2048,					// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.SpatialFiltering"), 1,					// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight"), 1.0f,	// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.SnapLength"), 20.0f,						// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.RaySampleMaxCount"), 128.0f,				// Cloud-Shadow option
TEXT("r.VolumetricCloud.ShadowMap.RaySampleHorizonMultiplier"), 2.0f,		// Cloud-Shadow option
//TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.LightRotationCutHistory"), 10.0f,
//TEXT("r.VolumetricCloud.ShadowMap.Debug"), 0,
//TEXT("r.VolumetricCloud.Debug.SampleCountMode"), 0,

BEGIN_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonShaderParameters, )
	SHADER_PARAMETER(FLinearColor, GroundAlbedo)			// FLinearColor(Component->GroundAlbedo);
	SHADER_PARAMETER(FVector, CloudLayerCenterKm)			//
	SHADER_PARAMETER(float, PlanetRadiusKm)					// 
	SHADER_PARAMETER(float, BottomRadiusKm)					// PlanetRadiusKm + Component->LayerBottomAltitude
	SHADER_PARAMETER(float, TopRadiusKm)					// BottomRadiusKm + Component->LayerHeight;
	SHADER_PARAMETER(float, TracingStartMaxDistance)		// KmToCm * Component->TracingStartMaxDistance
	SHADER_PARAMETER(float, TracingMaxDistance)				// KmToCm * Component->TracingMaxDistance
	SHADER_PARAMETER(float, InvDistanceToSampleCountMax)	// 1.0f / (KmToCm * TEXT("r.VolumetricCloud.DistanceToSampleMaxCount"))
	SHADER_PARAMETER(int32, SampleCountMax)					// Clamp(96.0 * Component->ViewSampleCountScale, 2, TEXT("r.VolumetricCloud.ViewRaySampleMaxCount"))
	SHADER_PARAMETER(int32, ShadowSampleCountMax)			// Clamp(10.0 * Component->ShadowViewSampleCountScale, 2, TEXT("r.VolumetricCloud.Shadow.ViewRaySampleMaxCount"))
	SHADER_PARAMETER(float, ShadowTracingMaxDistance)		// KmToCm * Component->ShadowTracingDistance
	SHADER_PARAMETER(float, SkyLightCloudBottomVisibility)	// 1.0f - Component->SkyLightCloudBottomOcclusion
	SHADER_PARAMETER_ARRAY(FLinearColor, AtmosphericLightCloudScatteredLuminanceScale, [2]) // UDirectionalLightComponent::CloudScatteredLuminanceScale
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapFarDepthKm, [2])	// 2.0 * UDirectionalLightComponent::CloudShadowExtent
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapStrength, [2])	// UDirectionalLightComponent::CloudShadowStrength
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapDepthBias, [2])	// UDirectionalLightComponent::CloudShadowDepthBias
	SHADER_PARAMETER_ARRAY(float,	CloudShadowmapSampleCount, [2])	//
	SHADER_PARAMETER_ARRAY(FVector4,CloudShadowmapSizeInvSize, [2])	// Min(512.0 * UDirectionalLightComponent::CloudShadowMapResolutionScale, TEXT("r.VolumetricCloud.ShadowMap.MaxResolution"))
	SHADER_PARAMETER_ARRAY(FMatrix,	CloudShadowmapWorldToLightClipMatrix, [2])		//
	SHADER_PARAMETER_ARRAY(FMatrix,	CloudShadowmapWorldToLightClipMatrixInv, [2])	//
	SHADER_PARAMETER_ARRAY(FVector4, CloudShadowmapTracingPixelScaleOffset, [2])	//
	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightDir, [2])	// UDirectionalLightComponent::GetDirection()
//	SHADER_PARAMETER_ARRAY(FVector4,CloudShadowmapTracingSizeInvSize, [2])
//	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightPos, [2])
//	SHADER_PARAMETER_ARRAY(FVector, CloudShadowmapLightAnchorPos, [2])
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

void FSceneRenderer::InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder)
{
	const FSkyAtmosphereRenderSceneInfo* SkyInfo = Scene->GetSkyAtmosphereSceneInfo();
	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
	const FVolumetricCloudSceneProxy& CloudProxy = CloudInfo.GetVolumetricCloudSceneProxy();
	FLightSceneProxy* AtmosphericLight0 = Scene->AtmosphereLights[0] ? Scene->AtmosphereLights[0]->Proxy : nullptr;
	FLightSceneProxy* AtmosphericLight1 = Scene->AtmosphereLights[1] ? Scene->AtmosphereLights[1]->Proxy : nullptr;
	FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

	FVolumetricCloudCommonShaderParameters& GlobalParams = CloudInfo.VolumetricCloudCommonShaderParameters;
	float PlanetRadiusKm = CloudProxy.PlanetRadiusKm;
	if (SkyInfo)
	{
		const FAtmosphereSetup& AtmosphereSetup = SkyInfo->GetSkyAtmosphereSceneProxy().GetAtmosphereSetup();
		PlanetRadiusKm = AtmosphereSetup.BottomRadiusKm;
		GlobalParams.CloudLayerCenterKm = AtmosphereSetup.PlanetCenterKm;
	}
	else
	{
		GlobalParams.CloudLayerCenterKm = FVector(0.0f, 0.0f, -PlanetRadiusKm);
	}

	GlobalParams.PlanetRadiusKm = PlanetRadiusKm;

	...

	GlobalParams.AtmosphericLightCloudScatteredLuminanceScale[0] = !AtmosphericLight0 ? FLinearColor::White : AtmosphericLight0->GetCloudScatteredLuminanceScale();
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudGlobalParameters, )
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumetricCloudCommonShaderParameters, VolumetricCloud)
	SHADER_PARAMETER_STRUCT(FSceneTextureUniformParameters, SceneTextures)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SceneDepthTexture)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, CloudShadowTexture0)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float3>, CloudShadowTexture1)
	SHADER_PARAMETER_SAMPLER(SamplerState, CloudBilinearTextureSampler)
	SHADER_PARAMETER_STRUCT_INCLUDE(FVolumeShadowingShaderParametersGlobal0, Light0Shadow) // Shadow-On-Cloud option
	SHADER_PARAMETER(FUintVector4, TracingCoordToZbufferCoordScaleBias)
	SHADER_PARAMETER(int32, OpaqueIntersectionMode)
	SHADER_PARAMETER(uint32, IsReflectionRendering)
	SHADER_PARAMETER(int32, EnableAerialPerspectiveSampling)
	SHADER_PARAMETER(int32, EnableDistantSkyLightSampling)
	SHADER_PARAMETER(int32, EnableAtmosphericLightsSampling)
	SHADER_PARAMETER(int32, EnableHeightFog)
	SHADER_PARAMETER_STRUCT_INCLUDE(FFogUniformParameters, FogStruct)
	SHADER_PARAMETER(uint32, TraceShadowmap)
/*	Not used in UE4.26.1
	SHADER_PARAMETER(uint32, NoiseFrameIndexModPattern)
	SHADER_PARAMETER(uint32, VolumetricRenderTargetMode)
	SHADER_PARAMETER(uint32, SampleCountDebugMode)
	SHADER_PARAMETER(FVector4, OutputSizeInvSize) 
	SHADER_PARAMETER(uint32, HasValidHZB)
	SHADER_PARAMETER(FVector, HZBUvFactor)
	SHADER_PARAMETER(FVector4, HZBSize)
	SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>, HZBTexture)
	SHADER_PARAMETER_SAMPLER(SamplerState, HZBSampler)
	SHADER_PARAMETER(uint32, ClampRayTToDepthBufferPostHZB)
*/
END_GLOBAL_SHADER_PARAMETER_STRUCT()
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
IMPLEMENT_STATIC_UNIFORM_BUFFER_STRUCT(FRenderVolumetricCloudGlobalParameters, "RenderVolumetricCloudParameters", SceneTextures);
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FVolumetricCloudCommonGlobalShaderParameters, "VolumetricCloudCommonParameters");
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
BEGIN_SHADER_PARAMETER_STRUCT(FRenderVolumetricCloudRenderViewParametersPS, )
	SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FRenderVolumetricCloudGlobalParameters, VolumetricCloudRenderViewParamsUB)
	RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()


void FSceneRenderer::RenderVolumetricCloud(
	FRDGBuilder& GraphBuilder,
	const FSceneTextureShaderParameters& SceneTextures, , ,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureMSAA SceneDepthTexture)
{
	FViewInfo& ViewInfo = Views[0];
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->VolumetricCloud;
	FVolumetricCloudSceneProxy& CloudSceneProxy = CloudInfo.VolumetricCloudSceneProxy;
	FLightSceneInfo* AtmosphericLight0Info = Scene->AtmosphereLights[0];
	FLightSceneProxy* AtmosphericLight0 = AtmosphericLight0Info ? AtmosphericLight0Info->Proxy : nullptr;
	FSkyLightSceneProxy* SkyLight = Scene->SkyLight;

	assume(CloudSceneProxy.CloudVolumeMaterial);
	FMaterialRenderProxy* CloudVolumeMaterialProxy = CloudSceneProxy.CloudVolumeMaterial->GetRenderProxy();
	if (CloudVolumeMaterialProxy->GetMaterial()->GetMaterialDomain() != MD_Volume)
		return;
	
	assume(TEXT("r.VolumetricRenderTarget") == 1);
	assume(TEXT("r.VolumetricCloud.HighQualityAerialPerspective") == 0);
	assume(!ViewInfo.bIsReflectionCapture);
	bool bSkipAtmosphericLightShadowmap = !GetVolumetricCloudReceiveAtmosphericLightShadowmap(AtmosphericLight0); // UDirectionalLightComponent::bCastShadowsOnClouds
	FVolumetricRenderTargetViewStateData& VCRT = ViewInfo.ViewState->VolumetricCloudRenderTarget;

	FVolumeShadowingShaderParametersGlobal0 LightShadowParams0;
	{
		const FProjectedShadowInfo* ProjectedShadowInfo = AtmosphericLight0Info ? GetLastCascadeShadowInfo(VisibleLightInfos[AtmosphericLight0Info->Id]) : nullptr;
		(!bSkipAtmosphericLightShadowmap && ProjectedShadowInfo) ? 
			SetVolumeShadowingShaderParameters(LightShadowParams0, ViewInfo, AtmosphericLight0Info, ProjectedShadowInfo, INDEX_NONE)
			: SetVolumeShadowingDefaultShaderParameters(LightShadowParams0);
	}

	FRDGTextureRef CloudShadowMap[2];
	{
		CloudShadowMap[0] = View.VolumetricCloudShadowRenderTarget[0].IsValid() ? 
			GraphBuilder.RegisterExternalTexture(View.VolumetricCloudShadowRenderTarget[0]) 
			: GSystemTextures.GetBlackDummy(GraphBuilder);
		CloudShadowMap[1] = View.VolumetricCloudShadowRenderTarget[1].IsValid() ? 
			GraphBuilder.RegisterExternalTexture(View.VolumetricCloudShadowRenderTarget[1]) 
			: GSystemTextures.GetBlackDummy(GraphBuilder);
	}

	FRenderVolumetricCloudGlobalParameters& TraceParams = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
	{
		TraceParams.VolumetricCloud = CloudInfo.GetVolumetricCloudCommonShaderParameters();
		TraceParams.CloudBilinearTextureSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
		TraceParams.EnableHeightFog = ViewInfo.Family->Scene->HasAnyExponentialHeightFog();
		SetupFogUniformParameters(GraphBuilder, ViewInfo, TraceParams.FogStruct);
		SetupSceneTextureUniformParameters(GraphBuilder, ESceneTextureSetupMode::CustomDepth, TraceParams.SceneTextures);
		TraceParams.SceneDepthTexture = GraphBuilder.RegisterExternalTexture(VCRT.GetMode() == 0 ? ViewInfo.HalfResDepthSurfaceCheckerboardMinMax : SceneContext.SceneDepthZ);
		TraceParams.TracingCoordToZbufferCoordScaleBias = FUintVector4(VCRT.VolumetricTracingRTDownsampleFactor, VCRT.VolumetricTracingRTDownsampleFactor, VCRT.CurrentPixelOffset.X , VCRT.CurrentPixelOffset.Y);
		TraceParams.IsReflectionRendering = 0;
		TraceParams.OpaqueIntersectionMode = VCRT.GetMode()== 2 ? 0 : 2;
		TraceParams.EnableAerialPerspectiveSampling = TEXT("r.VolumetricCloud.EnableAerialPerspectiveSampling");
		TraceParams.EnableDistantSkyLightSampling = TEXT("r.VolumetricCloud.EnableDistantSkyLightSampling");
		TraceParams.EnableAtmosphericLightsSampling = TEXT("r.VolumetricCloud.EnableAtmosphericLightsSampling");
		TraceParams.Light0Shadow = LightShadowParams0;
		TraceParams.CloudShadowTexture0 = CloudShadowMap[0];
		TraceParams.CloudShadowTexture1 = CloudShadowMap[1];
		//SetupRenderVolumetricCloudGlobalParametersHZB(GraphBuilder, ViewInfo, TraceParams);
	}

	{
		FRenderVolumetricCloudRenderViewParametersPS* PassParams = GraphBuilder.AllocParameters<FRenderVolumetricCloudRenderViewParametersPS>();
		PassParams->RenderTargets[0] = FRenderTargetBinding(VCRT.GetOrCreateVolumetricTracingRT(GraphBuilder);, ERenderTargetLoadAction::ENoAction);
		PassParams->RenderTargets[1] = FRenderTargetBinding(VCRT.GetOrCreateVolumetricTracingRTDepth(GraphBuilder), ERenderTargetLoadAction::ENoAction);
		PassParams->VolumetricCloudRenderViewParamsUB = GraphBuilder.CreateUniformBuffer(&TraceParams);

		FViewInfo& MainView = ViewInfo;
		TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer = ViewInfo.VolumetricRenderTargetViewUniformBuffer;
		bool bShouldViewRenderVolumetricRenderTarget = true;
		bool bSecondAtmosphereLightEnabled = Scene->IsSecondAtmosphereLightEnabled();

		GraphBuilder.AddPass(
			RDG_EVENT_NAME("CloudView"),
			PassParams,
			ERDGPassFlags::Raster,
			VCPass);
	}
}

static bool ShouldUsePerSampleAtmosphereTransmittance(const FScene* Scene, const FViewInfo* View)
{
	return Scene->VolumetricCloud 
		&& Scene->VolumetricCloud->VolumetricCloudSceneProxy.bUsePerSampleAtmosphericLightTransmittance 
		&& Scene->SkyAtmosphere && TEXT("r.SupportSkyAtmosphere") && TEXT("r.SkyAtmosphere") 
		&& View->Family->EngineShowFlags.Atmosphere;
}

VCPass = [this, &MainView, ViewUniformBuffer, bShouldViewRenderVolumetricRenderTarget, CloudVolumeMaterialProxy, 
			bSkipAtmosphericLightShadowmap, bSecondAtmosphereLightEnabled] (FRHICommandListImmediate& RHICmdList)
{
	DrawDynamicMeshPass(MainView, RHICmdList, 
	[&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FMeshPassProcessor PassMeshProcessor(Scene, Scene->GetFeatureLevel(), &MainView, InDrawListContext);
		
		FMeshPassProcessorRenderState DrawRenderState;
		{
			DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
			DrawRenderState.SetViewUniformBuffer(ViewUniformBuffer);
			bShouldViewRenderVolumetricRenderTarget ? 
				DrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI()) : 
				DrawRenderState.SetBlendState(TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_SourceAlpha, BO_Add, BF_Zero, BF_SourceAlpha>::GetRHI());
		}

		FMeshBatch SingleTriangle;
		GetSingleTriangleMeshBatch(SingleTriangle, CloudVolumeMaterialProxy, MainView.GetFeatureLevel());

		FMaterialRenderProxy* MaterialRenderProxy = SingleTriangle.MaterialRenderProxy;
		const FMaterial& Material = SingleTriangle.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, MaterialRenderProxy);

		if (Material.GetMaterialDomain() != MD_Volume)
			return;

		bool bVolumetricCloudPerSampleAtmosphereTransmittance = ShouldUsePerSampleAtmosphereTransmittance(Scene, &MainView);
		bool bVolumetricCloudSampleLightShadowmap = !bSkipAtmosphericLightShadowmap && TEXT("r.VolumetricCloud.Shadow.SampleAtmosphericLightShadowmap") > 0;
		bool bVolumetricCloudSecondLight = bSecondAtmosphereLightEnabled;
		class PsType = 
		bVolumetricCloudSecondLight ? 
			bVolumetricCloudSampleLightShadowmap ? 
				bVolumetricCloudPerSampleAtmosphereTransmittance ? 
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight1>
					: FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight1>
				: bVolumetricCloudPerSampleAtmosphereTransmittance ?
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight1>
					: FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight1>
			: bVolumetricCloudSampleLightShadowmap ? 
				bVolumetricCloudPerSampleAtmosphereTransmittance ? 
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow1_SecondLight0>
					: FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow1_SecondLight0>
				: bVolumetricCloudPerSampleAtmosphereTransmittance ?
					FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance1_SampleShadow0_SecondLight0>
					: FRenderVolumetricCloudRenderViewPs<VolumetricCloudRenderViewPs_PerSampleAtmosphereTransmittance0_SampleShadow0_SecondLight0>

		TMeshProcessorShaders< FRenderVolumetricCloudVS, , , PsType> PassShaders;
		{
			PassShaders.VertexShader = Material.GetShader<FRenderVolumetricCloudVS>(SingleTriangle.VertexFactory->GetType());
			PassShaders.PixelShader = Material.GetShader<PsType>(SingleTriangle.VertexFactory->GetType());
		}

		FMeshMaterialShaderElementData EmptyShaderElementData;
		EmptyShaderElementData.InitializeMeshMaterialData(&MainView, nullptr, SingleTriangle, -1, false);

		PassMeshProcessor.BuildMeshDrawCommands(
			SingleTriangle, ~0ull, nullptr,
			MaterialRenderProxy, Material,
			DrawRenderState, PassShaders, FM_Solid, CM_None,
			CalculateMeshStaticSortKey(PassShaders.VertexShader, PassShaders.PixelShader),
			EMeshPassFeatures::Default, EmptyShaderElementData);
	});
}

/*
OutColor0.rgb : Radiance along view-ray caused by scattering in cloud (up to scene if OpaqueIntersectionMode == 2)
OutColor0.a   : Final grey-transmittance through layer (up to scene if OpaqueIntersectionMode == 2)
OutColor1.r   : Distance (in Km) between camera and cloud-point(?) if ray test passed (up to scene if OpaqueIntersectionMode == 2)
OutColor1.g   : Distance (in Km) between camera and scene if ray test passed
*/
void MainPS(
	  in FVertexFactoryInterpolantsVSToPS FactoryInterpolants
	, in float4 SvPosition : SV_Position
	, out float4 OutColor0 : SV_Target0
	, out float2 OutColor1 : SV_Target1 )
{
	OutColor0 = float4(0.0f, 0.0f, 0.0f, 1.0f);
	OutColor1 = MaxHalfFloat;

	ResolvedView = ResolveView();
	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(FactoryInterpolants, SvPosition);
	FPixelMaterialInputs PixelMaterialInputs	= (FPixelMaterialInputs)0;
	CalcMaterialParameters(MaterialParameters, PixelMaterialInputs, SvPosition, true);
	MaterialParameters.VolumeSampleConservativeDensity = float3(1.0f);

	float3 RayOrigin			= ResolvedView.WorldCameraOrigin;
	float3 RayOriginKm			= RayOrigin * CENTIMETER_TO_KILOMETER;
	float3 Raydir				= -MaterialParameters.CameraVector;
	float TMin					= -999999999.0f;
	float TMax					= -999999999.0f;

	float3 Luminance			= 0.0f;
	float3 TransmittanceToView	= 1.0f;
	float tAPWeightedSum		= 0.0f;
	float tAPWeightsSum			= 0.0f;

	if ( !TraceClouds(
		SvPosition, , OutColor1,
		RayOrigin, RayOriginKm, Raydir, TMin, TMax,
		MaterialParameters, PixelMaterialInputs,
		Luminance, TransmittanceToView, tAPWeightedSum, tAPWeightsSum) )
	{
		return;
	}

	const float tAP = (TransmittanceToView==1.0f) ? TMax : tAPWeightedSum / tAPWeightsSum;

	if (TraceParams.EnableAerialPerspectiveSampling && tAPWeightsSum > 0.0f)
	{
		...
	}

	if (TraceParams.EnableHeightFog && tAPWeightsSum > 0.0f)
	{
		...
	}

	float GrayScaleTransmittance = dot(TransmittanceToView, 1.0f / 3.0f);
	OutColor0 = float4(Luminance * OutputPreExposure, GrayScaleTransmittance);

	if (TraceParams.OpaqueIntersectionMode >= 1)
	{
		OutColor1.x = ((GrayScaleTransmittance > 0.99) ? TMax : tAP) * CmToKm;
	}
}

class UMaterialExpressionVolumetricAdvancedMaterialOutput : public UMaterialExpressionCustomOutput {
	/*0*/FExpressionInput PhaseG = 0.0;
	/*1*/FExpressionInput PhaseG2 = 0.0;
	/*2*/FExpressionInput PhaseBlend = 0.0;
	/*3*/FExpressionInput MultiScatteringContribution = 0.5;
	/*4*/FExpressionInput MultiScatteringOcclusion = 0.5;
	/*5*/FExpressionInput MultiScatteringEccentricity = 0.5;
	/*6*/FExpressionInput ConservativeDensity = {1,1,1};
	bool PerSamplePhaseEvaluation = false;
	uint32 MultiScatteringApproximationOctaveCount = 0;
	bool bGroundContribution = false;
	bool bGrayScaleMaterial = false;
	bool bRayMarchVolumeShadow = true;
}

#define CLOUD_PER_SAMPLE_ATMOSPHERE_TRANSMITTANCE 0
#define CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP 0
#define CLOUD_SAMPLE_SECOND_LIGHT 0
#define MATERIAL_VOLUMETRIC_ADVANCED_OVERRIDE_AMBIENT_OCCLUSION 0
#define MATERIAL_VOLUMETRIC_ADVANCED 1
#define MSCOUNT (1 + MATERIAL_VOLUMETRIC_ADVANCED_MULTISCATTERING_OCTAVE_COUNT)
#define MATERIAL_VOLUMETRIC_ADVANCED_PHASE_PERPIXEL 1
#define MATERIAL_VOLUMETRIC_ADVANCED_GRAYSCALE_MATERIAL 0
#define MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION 0
#define TraceParams RenderVolumetricCloudParameters


void UpdateMaterialCloudParam(
	inout FMaterialPixelParameters Out, 
	float3 AbsoluteWorldPosition, 
	ViewState InView, )
{
	Out.AbsoluteWorldPosition = AbsoluteWorldPosition;

	float SampleAltitudeKm = length(CmToKm * AbsoluteWorldPosition - TraceParams.CloudLayerCenterKm);
	Out.CloudSampleAltitude = (SampleAltitudeKm - TraceParams.PlanetRadiusKm) * KmToCm;
	Out.CloudSampleAltitudeInLayer = (SampleAltitudeKm - TraceParams.BottomRadiusKm) * KmToCm;
	Out.CloudSampleNormAltitudeInLayer = saturate( 
		(SampleAltitudeKm - TraceParams.BottomRadiusKm) / (TraceParams.TopRadiusKm - TraceParams.BottomRadiusKm) );
#if MATERIAL_VOLUMETRIC_ADVANCED_CONSERVATIVE_DENSITY
	Out.VolumeSampleConservativeDensity = GetVolumetricAdvancedMaterialOutput6(Out);	
#endif
}

struct ParticipatingMediaContext {
	float3 ScatteringCoefficients[MSCOUNT];
	float3 ExtinctionCoefficients[MSCOUNT];
	float3 TransmittanceToLight0[MSCOUNT];
};

ParticipatingMediaContext SetupParticipatingMediaContext(
	float3 BaseAlbedo, 
	float3 BaseExtinctionCoefficients, 
	float MsSFactor, 
	float MsEFactor, 
	float3 InitialTransmittanceToLight0 = 1.0, 
	float3 InitialTransmittanceToLight1 = 1.0)
{
	const float3 ScatteringCoefficients = BaseAlbedo * BaseExtinctionCoefficients;
	
	ParticipatingMediaContext PMC;
	for (int i = 0; i < MSCOUNT; ++i)
	{
		int j = max(0, i*2-1);
		PMC.ScatteringCoefficients[j] = ScatteringCoefficients * pow(MsSFactor, j);
		PMC.ExtinctionCoefficients[j] = BaseExtinctionCoefficients * pow(MsEFactor, j);
		PMC.TransmittanceToLight0[j] = InitialTransmittanceToLight0;
	}

	return PMC;
}

bool TraceClouds(
	in float4 SvPosition, , inout float2 OutColor1,
	in float3 RayOrigin, in float3 RayOriginKm, in float3 Raydir, 
	inout float TMin, inout float TMax,
	inout FMaterialPixelParameters MaterialParameters,
	inout FPixelMaterialInputs PixelMaterialInputs,
	inout float3 Luminance,
	inout float3 TransmittanceToView,
	inout float  tAPWeightedSum,
	inout float  tAPWeightsSum
	)
{
	float2 tTop = 0.0f;
	bool bOuterIntersect = RayIntersectSphereSolution(RayOriginKm, Raydir, float4(TraceParams.CloudLayerCenterKm, TraceParams.TopRadiusKm), tTop);
	if(!bOuterIntersect || tTop.y<0)
		return false;

	float2 tBottom = 0.0f;
	bool bInnerIntersect = RayIntersectSphereSolution(RayOriginKm, Raydir, float4(TraceParams.CloudLayerCenterKm, TraceParams.BottomRadiusKm), tBottom);

	if(!bInnerIntersect)
	{
		TMin = max(tTop.x, 0.0);
		TMax = tTop.y;
	}
	else
	{
		if(tBottom.x > 0)
		{
			TMin = max(tTop.x, 0.0);
			TMax = tBottom.x;	
		}
		else
		{
			TMin = max(tBottom.y, 0.0);
			TMax = tTop.y;
		}
	}
	TMin *= KILOMETER_TO_CENTIMETER;
	TMax *= KILOMETER_TO_CENTIMETER;

	if (TraceParams.TracingStartMaxDistance < TMin)
		return false;

	const float3 SunLighting0 = float(TraceParams.EnableAtmosphericLightsSampling) * 
		TraceParams.AtmosphericLightCloudScatteredLuminanceScale[0].rgb *
		ResolvedView.AtmosphereLightColorGlobalPostTransmittance[0].rgb; 

	float3 SkyLighting = float(TraceParams.EnableDistantSkyLightSampling) *
		(ResolvedView.SkyAtmospherePresentInScene > 0.0f) ?
			Texture2DSampleLevel(View.DistantSkyLightLutTexture, View.DistantSkyLightLutTextureSampler, float2(0.5f, 0.5f), 0.0f)
			: GetSkySHDiffuseSimple(float3(0.0f)); 

	const float3 wi = ResolvedView.AtmosphereLightDirection[0].xyz;
	const float3 wo = Raydir;
	const float PhaseG = GetVolumetricAdvancedMaterialOutput0(MaterialParameters);
	const float PhaseG2 = GetVolumetricAdvancedMaterialOutput1(MaterialParameters);
	const float PhaseBlend = GetVolumetricAdvancedMaterialOutput2(MaterialParameters);
	const float MsScattFactor = GetVolumetricAdvancedMaterialOutput3(MaterialParameters);
	const float MsExtinFactor = GetVolumetricAdvancedMaterialOutput4(MaterialParameters);
	const float MsPhaseFactor = GetVolumetricAdvancedMaterialOutput5(MaterialParameters);
	const float BasePhase0 = lerp(HgPhase(PhaseG, -dot(wi, wo)), HgPhase(PhaseG2, -dot(wi, wo)), PhaseBlend);

	float Phase0[MSCOUNT];
	for (int ms = 0; ms < MSCOUNT; ++ms)
	{
		Phase0[ms] = lerp(IsotropicPhase(), BasePhase0, pow(MsPhaseFactor, ms));
	}

	uint TraceDownFactor = TraceParams.TracingCoordToZbufferCoordScaleBias.x;
	uint2 CurrentPixelOffset = TraceParams.TracingCoordToZbufferCoordScaleBias.zw;
	uint2 Coord = uint2(SvPosition.xy) * TraceDownFactor + CurrentPixelOffset;
	float DeviceZ = TraceParams.SceneDepthTexture.Load( uint3(Coord, /*Mip*/0) ).r;
	float TDepthBuffer = length( SvPositionToWorld(float4(SvPosition.xy, DeviceZ, 1.0)) - RayOrigin );
	if (TraceParams.OpaqueIntersectionMode == 2)
	{
		TMax = min(TMax, TDepthBuffer);
	}
	TMax = TMin + min(TMax - TMin, TraceParams.TracingMaxDistance);
	
	OutColor1.y = TDepthBuffer * CENTIMETER_TO_KILOMETER;

	// TMax - TMin < 0 sets to StepCount = 0 !!
	const uint StepCount = TraceParams.SampleCountMax * saturate( (TMax - TMin) * TraceParams.InvDistanceToSampleCountMax );
	const float StepT = (TMax - TMin) / StepCount;
	assert( KmToCm * TEXT("r.VolumetricCloud.DistanceToSampleMaxCount") / TraceParams.SampleCountMax <= StepT);

	float t0 = TMin + UnitRand(int3(SvPosition.xy, View.StateFrameIndexMod8)) * StepT;
	
	for (uint i = 0, float t = t0; i < StepCount; ++i, t += StepT)
	{
		float3 ViewRaySamplePos = RayOrigin + t * Raydir;

		UpdateMaterialCloudParam(MaterialParameters, ViewRaySamplePos, ResolvedView, );
		if (MaterialParameters.VolumeSampleConservativeDensity.x <= 0.0f)
			continue;

		CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);
		const float3 EmissiveLuminance = SampleEmissive(PixelMaterialInputs);
		
		/* ------Does not impact on perpormance?-----------
		float ExtinctionCoeff = SampleExtinctionCoefficients(PixelMaterialInputs);
		if(ExtinctionCoeff == 0.0) continue; */

		float3 SkyLightingWithAO;
	#if MATERIAL_VOLUMETRIC_ADVANCED_OVERRIDE_AMBIENT_OCCLUSION
		SkyLightingWithAO = SkyLighting * SampleAmbientOcclusion(PixelMaterialInputs);
	#else
		SkyLightingWithAO = SkyLighting * saturate(TraceParams.SkyLightCloudBottomVisibility + MaterialParameters.CloudSampleNormAltitudeInLayer);
	#endif

		ParticipatingMediaContext PMC = SetupParticipatingMediaContext(
			SampleAlbedo(PixelMaterialInputs), 
			SampleExtinctionCoefficients(PixelMaterialInputs), 
			MsScattFactor, MsExtinFactor, 1.0, );

		if (any(PMC.ScatteringCoefficients[0] > 0.0f))
		{
		#if MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION
		#endif // MATERIAL_VOLUMETRIC_ADVANCED_GROUND_CONTRIBUTION
		
		#if CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP
		#endif // CLOUD_SAMPLE_ATMOSPHERIC_LIGHT_SHADOWMAP

		#if MATERIAL_VOLUMETRIC_ADVANCED_RAYMARCH_VOLUME_SHADOW==1
			const float ShadowRayLength = TraceParams.ShadowTracingMaxDistance;
			
			float3 ExtinctionAcc[MSCOUNT] = {0};
			{
				const float ShadowStepT = 1.0f / TraceParams.ShadowSampleCountMax;

				float PrevT = 0.0f;
				for (float ShadowT = ShadowStepT; ShadowT <= 1.0; ShadowT += ShadowStepT)
				{
					float CurrT = ShadowT * ShadowT;
					const float ExtinctionFactor = CurrT - PrevT;
					float3 SadowRaySamplePos = ViewRaySamplePos + wi * ShadowRayLength * (PrevT + CurrT) * 0.5f;
					PrevT = CurrT;

					UpdateMaterialCloudParam(MaterialParameters, SadowRaySamplePos, ResolvedView, );
					if (MaterialParameters.VolumeSampleConservativeDensity.x <= 0.0f)
						continue;
					// This is a problem because CloudSampleNormAltitudeInLayer is clamped to 1.0 !!
					if (MaterialParameters.CloudSampleNormAltitudeInLayer > 1.0f) 
						break;

					CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);

					ParticipatingMediaContext ShadowPMC = SetupParticipatingMediaContext(
						0.0f, 
						SampleExtinctionCoefficients(PixelMaterialInputs), , 
						MsExtinFactor, , );

					for (ms = 0; ms < MSCOUNT; ++ms)
					{
						ExtinctionAcc[ms] += ShadowPMC.ExtinctionCoefficients[ms] * (CurrT - PrevT) * ShadowRayLength * CmToM;
					}
				}
			}

			for (ms = 0; ms < MSCOUNT; ++ms)
			{
				PMC.TransmittanceToLight0[ms] *= exp(-ExtinctionAcc[ms]);
			}
		#else 
			float OutOpticalDepth = 0.0f;
			GetCloudVolumetricShadow(SampleWorldPosition, 
				TraceParams.CloudShadowmapWorldToLightClipMatrix[0], TraceParams.CloudShadowmapFarDepthKm[0],
				TraceParams.CloudShadowTexture0, TraceParams.CloudBilinearTextureSampler, 
				OutOpticalDepth);
			
			for (ms = 0; ms < MSCOUNT; ++ms)
			{
				PMC.TransmittanceToLight0[ms] *= exp(-OutOpticalDepth * pow(MsExtinFactor, ms));
			}
		#endif 
		}

		// Compute the weighted average of t for the aerial perspective evaluation.
		if (any(PMC.ExtinctionCoefficients[0]) > 0.0)
		{
			float tAPWeight = min(TransmittanceToView.r, TransmittanceToView.g, TransmittanceToView.b);
			tAPWeightedSum += t * tAPWeight;
			tAPWeightsSum += tAPWeight;
		}


		for (ms = MSCOUNT-1; ms >= 0; --ms)
		{
			const float3 SunSkyLuminance = (SunLighting0 * PMC.TransmittanceToLight0[ms]) * Phase0[ms]
									+ float(ms == 0) * SkyLightingWithAO;
			const float3 ScatteredLuminance = SunSkyLuminance * PMC.ScatteringCoefficients[ms] + EmissiveLuminance;

			const float3 PathSegmentTransmittance = exp(-PMC.ExtinctionCoefficients[ms] * StepT * CmToM);
			bool bLineIntegration = false;
		
		#if 1	
			bLineIntegration = ExtinctionCoefficients > 0.000001f;
		#endif

			if(!bLineIntegration)
			{
				Luminance += TransmittanceToView * ScatteredLuminance * StepT * CmToM;	
			}	
			else
			{
				Luminance += TransmittanceToView * ScatteredLuminance * (1.0 - PathSegmentTransmittance) / ExtinctionCoefficients;	
			}

			if (ms == 0)
			{
				TransmittanceToView *= PathSegmentTransmittance;
			}
		}

		if (all(TransmittanceToView < 0.005f))
		{
			break;
		}
	}

	return true;
}





static bool ShouldRenderCloudShadowmap(const FLightSceneProxy* AtmosphericLight)
{
	return TEXT("r.VolumetricCloud.ShadowMap") 
		&& AtmosphericLight && AtmosphericLight->GetCastCloudShadows();
}

static int32 GetVolumetricCloudShadowMapResolution(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return FMath::Min( 
			(int32) (512.0f * AtmosphericLight->GetCloudShadowMapResolutionScale()),
			TEXT("r.VolumetricCloud.ShadowMap.MaxResolution") );
	}
	return 32;
}
static float GetVolumetricCloudShadowMapExtentKm(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowExtent();
	}
	return 1.0f;
}
static int32 GetVolumetricCloudShadowRaySampleCountScale(const FLightSceneProxy* AtmosphericLight)
{
	if (AtmosphericLight)
	{
		return AtmosphericLight->GetCloudShadowRaySampleCountScale();
	}
	return 4;
}

void FSceneRenderer::InitVolumetricCloudsForViews(FRDGBuilder& GraphBuilder)
{
	FVolumetricCloudRenderSceneInfo& CloudInfo = *Scene->GetVolumetricCloudSceneInfo();
	FVolumetricCloudCommonShaderParameters& GlobalParams = CloudInfo.GetVolumetricCloudCommonShaderParameters();

	assume(AtmosphericLight0 && !AtmosphericLight1);

	const float CloudShadowTemporalWeight = FMath::Clamp(TEXT("r.VolumetricCloud.ShadowMap.TemporalFiltering.NewFrameWeight"), 0, 1);
	const bool bCloudShadowTemporalEnabled = CloudShadowTemporalWeight < 1.0f;
	const float SnapLength = TEXT("r.VolumetricCloud.ShadowMap.SnapLength") * KmToCm;

	const float Res = (float) GetVolumetricCloudShadowMapResolution(AtmosphericLight0);
	GlobalParams.CloudShadowmapSizeInvSize[0] = FVector4(Res, Res, 1.0f/Res, 1.0f/Res);
	if (bCloudShadowTemporalEnabled)
	{
		Vector2D PixelOffset[4] = { {0,0}, {1,1}, {1,0}, {0,1} };
		uint32 m = Views[0].ViewState->GetFrameIndex() % 4;
		GlobalParams.CloudShadowmapTracingPixelScaleOffset[0] = FVector4(2.0f, 2.0f, PixelOffset[m].X, PixelOffset[m].Y);
	}
	else
	{
		GlobalParams.CloudShadowmapTracingPixelScaleOffset[0] = FVector4(1.0f, 1.0f, 0.0f, 0.0f);
	}
	GlobalParams.CloudShadowmapStrength[0] = GetVolumetricCloudShadowmapStrength(AtmosphericLight0);
	GlobalParams.CloudShadowmapDepthBias[0] = GetVolumetricCloudShadowmapDepthBias(AtmosphericLight0);

	const FVector L = -AtmosphericLight0->GetDirection();
	const float LightDistance = GetVolumetricCloudShadowMapExtentKm(AtmosphericLight0) * KmToCm;

	FVector CenterToCameraDir = Views[0].ViewMatrices.GetViewOrigin() - GlobalParams.CloudLayerCenterKm * KmToCm;
	CenterToCameraDir.Normalize();
	const FVector LookAtPos = (GlobalParams.CloudLayerCenterKm + CenterToCameraDir * PlanetRadiusKm) * KmToCm;// Look at position is positioned on the planet surface under the camera.
	const FVector SnappedLookAtPos = FMath::Floor(LookAtPos / SnapLength + FVector(0.5f)) * SnapLength;
	const FVector LightPos = SnappedLookAtPos + L * LightDistance;

	const float NearPlane = 0.0f;
	const float FarPlane = LightDistance * 2.0f;
	const float Width = LightDistance * 2.0f;
	const float Height = LightDistance * 2.0f;
	FReversedZOrthoMatrix ShadowProjectionMatrix(Width, Height, 1.0f / (FarPlane-NearPlane), -NearPlane);
	FLookAtMatrix ShadowViewMatrix(LightPos, SnappedLookAtPos, FVector::UpVector);
	GlobalParams.CloudShadowmapWorldToLightClipMatrix[0] = ShadowViewMatrix * ShadowProjectionMatrix;
	GlobalParams.CloudShadowmapWorldToLightClipMatrixInv[0] = GlobalParams.CloudShadowmapWorldToLightClipMatrix[0].Inverse();
	GlobalParams.CloudShadowmapLightDir[0] = -L;
	GlobalParams.CloudShadowmapFarDepthKm[0] = FarPlane * CmToKm;

	assume(TEXT("r.VolumetricCloud.ShadowMap.RaySampleHorizonMultiplier") == 2.0f);
	const float RaySampleCountScale = GetVolumetricCloudShadowRaySampleCountScale(AtmosphericLight0);
	const float RaySampleCount = FMath::Clamp(16.0f * RaySampleCountScale, 4.0f, TEXT("r.VolumetricCloud.ShadowMap.RaySampleMaxCount"));
	const float HorizonFactor = FMath::Min(0.2f / FMath::Abs(FVector::DotProduct(CenterToCameraDir, L)), 1.0f); // Range in [0.2, 1.0]
	GlobalParams.CloudShadowmapSampleCount[0] = RaySampleCount * (1.0f + HorizonFactor);


	FRenderVolumetricCloudGlobalParameters& RenderParams = *GraphBuilder.AllocParameters<FRenderVolumetricCloudGlobalParameters>();
	RenderParams.VolumetricCloud = CloudInfo.GetVolumetricCloudCommonShaderParameters(); // SetupDefaultRenderVolumetricCloudGlobalParameters();
	
	{// GenerateCloudTexture(AtmosphericLight0, 0);
		FVolumetricCloudCommonShaderParameters& CloudGlobalShaderParams = CloudInfo.GetVolumetricCloudCommonShaderParameters();
		if (!ShouldRenderCloudShadowmap(AtmosphericLight0))
		{
			if (ViewInfo.ViewState)
			{
				ViewInfo.VolumetricCloudShadowRenderTarget[0].SafeRelease();
				ViewInfo.ViewState->VolumetricCloudShadowRenderTarget[0].Reset();
			}
		}
		else
		{
			const int32 CloudShadowSpatialFiltering = FMath::Clamp(TEXT("r.VolumetricCloud.ShadowMap.SpatialFiltering"), 0, 4);
			const uint32 Res = GetVolumetricCloudShadowMapResolution(AtmosphericLight0);
			const FIntPoint TracingRes = bCloudShadowTemporalEnabled ? FIntPoint(Res/2) : FIntPoint(Res);
			const EPixelFormat CloudShadowPixelFormat = PF_FloatR11G11B10;

			FRDGTextureRef NewCloudShadowTexture = GraphBuilder.CreateTexture(
				FRDGTextureDesc::Create2D(TracingRes, CloudShadowPixelFormat, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV | TexCreate_RenderTargetable), TEXT("CloudShadowTexture"));

			RenderParams.TraceShadowmap = 1;
			TRDGUniformBufferRef<FRenderVolumetricCloudGlobalParameters> TraceVolumetricCloudShadowParamsUB = GraphBuilder.CreateUniformBuffer(&RenderParams);

			{
				FVolumetricCloudShadowParametersPS* CloudShadowParameters = GraphBuilder.AllocParameters<FVolumetricCloudShadowParametersPS>();
				CloudShadowParameters->TraceVolumetricCloudParamsUB = TraceVolumetricCloudShadowParamsUB;
				CloudShadowParameters->RenderTargets[0] = FRenderTargetBinding(NewCloudShadowTexture, ERenderTargetLoadAction::ENoAction);

				GraphBuilder.AddPass(RDG_EVENT_NAME("CloudShadow"), CloudShadowParameters, ERDGPassFlags::Raster,
					[CloudShadowParameters, &ViewInfo, CloudVolumeMaterialProxy](FRHICommandListImmediate& RHICmdList)
					{
						DrawDynamicMeshPass(ViewInfo, RHICmdList,
							[&ViewInfo, CloudVolumeMaterialProxy](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
							{
								FVolumetricCloudRenderShadowMeshProcessor PassMeshProcessor(ViewInfo.Family->Scene->GetRenderScene(), &ViewInfo, DynamicMeshPassContext);
								FMeshBatch SingleTriangle = GetSingleTriangleMeshBatch(CloudVolumeMaterialProxy, ViewInfo.GetFeatureLevel());
								PassMeshProcessor.AddMeshBatch(SingleTriangle, ~0ull, nullptr);
							});
					});
			}

			if(bCloudShadowTemporalEnabled && ViewInfo.ViewState)// the view has a ViewState (not a sky light capture view for instance)
			{
				...
			}
			else
			{
				FRDGTextureRef FilteredTexture = NewCloudShadowTexture;
				for (int i = 0; i < CloudShadowSpatialFiltering; ++i)
				{
					const FIntPoint DownscaledRes = FIntPoint(FilteredTexture->Desc.Extent.X / 2.0f);
					FRDGTextureRef OutTexture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(DownscaledRes, PF_FloatR11G11B10, FClearValueBinding::None, TexCreate_ShaderResource | TexCreate_UAV), TEXT("CloudShadowTexture2") );

					TShaderMapRef<FCloudShadowFilterCS> ComputeShader;
					FCloudShadowFilterCS::FParameters* Parameters = GraphBuilder.AllocParameters<FCloudShadowFilterCS::FParameters>();
					Parameters->CloudShadowTexture = FilteredTexture;
					Parameters->OutCloudShadowTexture = GraphBuilder.CreateUAV(FRDGTextureUAVDesc(OutTexture));

					const FIntVector DispatchCount = DispatchCount.DivideAndRoundUp(FIntVector(DownscaledRes.X, DownscaledRes.Y, 1), FIntVector(8, 8, 1));
					FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("CloudDataSpatialFilter"), ComputeShader, Parameters, DispatchCount);
					/*
					[numthreads(8, 8, 1)]
					void MainShadowFilterCS(uint3 DispatchThreadId : SV_DispatchThreadID)
					{	
						const int2 CenterCoord = int2(DispatchThreadId.xy);
						float3 Data0 = CloudShadowTexture.Load0(CenterCoord * 2 + int2(0, 0));
						float3 Data1 = CloudShadowTexture.Load0(CenterCoord * 2 + int2(1, 0));
						float3 Data2 = CloudShadowTexture.Load0(CenterCoord * 2 + int2(1, 1));
						float3 Data3 = CloudShadowTexture.Load0(CenterCoord * 2 + int2(0, 1));
						OutCloudShadowTexture[CenterCoord] = (Data0 + Data1 + Data2 + Data3) * 0.25;
					}
					*/
					FilteredTexture = OutTexture;
				}
				ConvertToExternalTexture(GraphBuilder, FilteredTexture, ViewInfo.VolumetricCloudShadowRenderTarget[0]);
			}
		}
		
		if (ViewInfo.ViewState)
		{
			...
		}
	} // GenerateCloudTexture(AtmosphericLight0, 0);
}

void MainPS(
	in FVertexFactoryInterpolantsVSToPS FactoryInterpolants
	, in float4 SvPosition : SV_Position
	, out float3 OutColor0 : SV_Target0)
{
	ResolvedView = ResolveView();
	const float4 SizeInvSize					= TraceParams.CloudShadowmapSizeInvSize[0];
	const float4 TracingPixelScaleOffset		= TraceParams.CloudShadowmapTracingPixelScaleOffset[0];
	const float4x4 WorldToLightClipMatrixInv	= TraceParams.CloudShadowmapWorldToLightClipMatrixInv[0];
	const float3 TraceDir						= TraceParams.CloudShadowmapLightDir[0];
	const float FarDepthKm						= TraceParams.CloudShadowmapFarDepthKm[0];
	const float ShadowStepCount					= TraceParams.CloudShadowmapSampleCount[0];
	const float Strength						= TraceParams.CloudShadowmapStrength[0];
	const float DepthBias						= TraceParams.CloudShadowmapDepthBias[0];

	FMaterialPixelParameters MaterialParameters = GetMaterialPixelParameters(FactoryInterpolants, SvPosition);
	FPixelMaterialInputs PixelMaterialInputs;
	CalcMaterialParameters(MaterialParameters, PixelMaterialInputs, SvPosition, true);

	const float2 UV = float2(SvPosition.xy * TracingPixelScaleOffset.xy + TracingPixelScaleOffset.zw) * SizeInvSize.zw;
	const float NearZDepth = 1.0f;
	float3 NearClipPlaneWorldPos = CloudShadowUvToWorldSpace(NearZDepth, UV, WorldToLightClipMatrixInv);

	float TMin = -999999999.0f;
	float TMax = -999999999.0f;
	float3 RayOriginKm = NearClipPlaneWorldPos * CmToKm;
	
	float2 tTop2 = 0.0f;
	if (RayIntersectSphereSolution(RayOriginKm, TraceDir, float4(TraceParams.CloudLayerCenterKm, TraceParams.TopRadiusKm), tTop2))
	{
		float2 tBottom2 = 0.0f;
		if (RayIntersectSphereSolution(RayOriginKm, TraceDir, float4(TraceParams.CloudLayerCenterKm, TraceParams.BottomRadiusKm), tBottom2))
		{
			float TempTop = all(tTop2 > 0.0f) ? min(tTop2.x, tTop2.y) : max(tTop2.x, tTop2.y);
			float TempBottom = all(tBottom2 > 0.0f) ? min(tBottom2.x, tBottom2.y) : max(tBottom2.x, tBottom2.y);
			if (all(tBottom2 > 0.0f))
			{
				TempTop = max(0.0f, min(tTop2.x, tTop2.y));
			}
			else
			{
				OutColor0 = float3(0.0f, 0.0f, 0.0f);
				return;
			}
			TMin = min(TempBottom, TempTop);
			TMax = max(TempBottom, TempTop);
		}
		else
		{
			TMin = tTop2.x;
			TMax = tTop2.y;
		}
		TMin = max(0.0f, TMin) * KmToCm;
		TMax = max(0.0f, TMax) * KmToCm;
	}
	else
	{
		OutColor0 = float3(FarDepthKm, 0.0f, 0.0f);
		return;
	}
	
	const float3 TraceStartPos = NearClipPlaneWorldPos + TMin * TraceDir;
	const float TraceLength = TMax - TMin;
	const flaot StepT = TraceLength / ShadowStepCount;
	
	float NearDepth = TMax;
	float3 ExtinctionAcc = 0.0f;
	float ExtinctionAccCount = 0.0f;
	float3 OpticalDepth = 0.0f;

	for (float t = 0.5 * StepT; t < TraceLength; t += StepT)
	{
		UpdateMaterialCloudParam(MaterialParameters, TraceStartPos + t * TraceDir, ResolvedView);
		if (MaterialParameters.VolumeSampleConservativeDensity.x <= 0.0f) 
			continue;
		CalcPixelMaterialInputs(MaterialParameters, PixelMaterialInputs);
		float3 Extinction = Strength * SampleExtinctionCoefficients(PixelMaterialInputs);

		ExtinctionAcc += Extinction;
		OpticalDepth += Extinction * (StepT * CmToM);
		
		if(any(Extinction > 0.0f))
		{
			NearDepth = min(NearDepth, TMin + t);
			ExtinctionAccCount += 1.0f;			
		}
	}

	const float MeanGreyExtinction = dot(ExtinctionAcc, float3(1.0f/3.0f)) / max(1.0f, ExtinctionAccCount);
	const float GreyOpticalDepth = dot(OpticalDepth, float3(1.0f/3.0f));
	OutColor0 = float3(CmToKm * NearDepth + DepthBias, MeanGreyExtinction, GreyOpticalDepth);
}

float GetCloudVolumetricShadow(in float3 WorldPos, in float4x4 WorldToLightClipMatrix, in float FarDepthKm,
	Texture2D<float3> ShadowMapTexture, SamplerState ShadowMapTextureSampler, inout float OutOpticalDepth)
{
	float CloudShadowSampleZ = 0.0f;
	const float2 UVs = CloudShadowWorldSpaceToUv(WorldPos, WorldToLightClipMatrix, CloudShadowSampleZ);
	const float3 CloudShadowData = ShadowMapTexture.SampleLevel(ShadowMapTextureSampler, UVs.xy, 0).rgb;
	const float SampleDepthKm = saturate(1.0f - CloudShadowSampleZ) * FarDepthKm;

	const float NearDepthKm = CloudShadowData.r;
	const float MeanExtinction = CloudShadowData.g;
	const float LayerTotalOpticalDepth = CloudShadowData.b;
	OutOpticalDepth = min(MeanExtinction * max(0.0f, SampleDepthKm - NearDepthKm) * KmToM, LayerTotalOpticalDepth)
	return exp(-OutOpticalDepth);
}
