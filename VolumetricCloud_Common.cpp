bool ShouldRenderAtmosphere(const FSceneViewFamily& Family)
{
	return GSupportsVolumeTextureRendering
		&& Family.EngineShowFlags.Atmosphere
		&& Family.EngineShowFlags.Fog
		&& TEXT("r.SupportAtmosphericFog");
}

bool ShouldRenderSkyAtmosphere(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
	return Scene 
		&& Scene->SkyAtmosphere 
		&& EngineShowFlags.Atmosphere/* && EngineShowFlags.Lighting */
		&& TEXT("r.SupportSkyAtmosphere") && TEXT("r.SkyAtmosphere");
}

bool ShouldRenderVolumetricCloud(const FScene* Scene, const FEngineShowFlags& EngineShowFlags)
{
	if( Scene 
		&& Scene->VolumetricCloud 
		&& EngineShowFlags.Atmosphere/* && EngineShowFlags.Lighting */
		&& TEXT("r.VolumetricCloud")
		&& Scene->VolumetricCloud->VolumetricCloudSceneProxy.CloudVolumeMaterial )
	{
		FMaterialRenderProxy* CloudVolumeMaterialProxy = Scene->VolumetricCloud->VolumetricCloudSceneProxy.CloudVolumeMaterial->GetRenderProxy();
		const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
		const FMaterial& MaterialTest = CloudVolumeMaterialProxy->GetMaterialWithFallback(Scene->GetFeatureLevel(), FallbackMaterialRenderProxyPtr);
		
		return MaterialTest.GetMaterialDomain() == MD_Volume;
	}
	return false;
}

void FDeferredShadingSceneRenderer::Render(FRHICommandListImmediate& RHICmdList)
{
	const bool bShouldRenderVolumetricCloud = ShouldRenderVolumetricCloud(Scene, ViewFamily.EngineShowFlags);

	if (TEXT("r.VolumetricRenderTarget"))
	{
		InitVolumetricRenderTargetForViews(GraphBuilder);
	}

	if (bShouldRenderVolumetricCloud)
	{
		InitVolumetricCloudsForViews(GraphBuilder);
	}

	...

	if (bShouldRenderVolumetricCloud && TEXT("r.VolumetricRenderTarget"))
	{
		RenderVolumetricCloud(GraphBuilder, GetSceneTextureShaderParameters(SceneTextures), , , SceneColorTexture, SceneDepthTexture);
		ReconstructVolumetricRenderTarget(GraphBuilder);
	}

	...

	if (bShouldRenderVolumetricCloud && !TEXT("r.VolumetricRenderTarget"))
	{
		RenderVolumetricCloud(GraphBuilder, GetSceneTextureShaderParameters(SceneTextures), , , SceneColorTexture, SceneDepthTexture);
	}

	if (bShouldRenderVolumetricCloud)
	{
		ComposeVolumetricRenderTargetOverScene(GraphBuilder, SceneColorTexture.Target, SceneDepthTexture.Target, , );
	}
}

void FDeferredShadingSceneRenderer::UpdateHalfResDepthSurfaceCheckerboardMinMax(FRDGBuilder& GraphBuilder, FRDGTextureRef SceneDepthTexture)
{
	FViewInfo& View = Views[0];

	const uint32 DownscaleFactor = 2;
	const FIntPoint InExtent = SceneDepthTexture->Desc.Extent;
	const FIntRect InViewRect = View.ViewRect;
	const FIntPoint OutExtent = GetDownscaledExtent(InExtent, DownscaleFactor);
	const FIntRect OutViewRect = GetDownscaledRect(InViewRect, DownscaleFactor);

	const FRDGTextureDesc OutDepthDesc = FRDGTextureDesc::Create2D(OutExtent, PF_DepthStencil, FClearValueBinding::None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
	FRDGTextureRef OutDepthTexture = GraphBuilder.CreateTexture(OutDepthDesc, TEXT("HalfResDepthSurfaceCheckerboardMinMax"));

	{
		FDownsampleDepthPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FDownsampleDepthPS::FParameters>();
		PassParameters->View = View.ViewUniformBuffer;
		PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(OutDepthTexture, ...);
		PassParameters->DepthTexture = SceneDepthTexture;
		PassParameters->DestTexelOffsets = FVector2D(1.0f / OutExtent.X, 1.0f / OutExtent.Y);
		PassParameters->DestBufferSize = OutExtent;

		AddDrawScreenPass(...);
	}

	TRefCountPtr<IPooledRenderTarget> SmallDepthTarget;
	ConvertToUntrackedExternalTexture(GraphBuilder, OutDepthTexture, SmallDepthTarget, ERHIAccess::SRVMask);
	View.HalfResDepthSurfaceCheckerboardMinMax = SmallDepthTarget;
}

Texture2D<float> DepthTexture;
float2 DestTexelOffsets;
float2 DestinationResolution;
float2 DestBufferSize;

void Main(
	noperspective float4 UV : TEXCOORD0,
	out float OutDepth : SV_DEPTH)
{	
	// Lookup the four DeviceZ's of the full resolution pixels corresponding to this low resolution pixel
	float DeviceZ0 = DepthTexture.Sample( GlobalPointClampedSampler, UV.xy + DestTexelOffsets * 0.25 * float2(-1,-1) );
	float DeviceZ1 = DepthTexture.Sample( GlobalPointClampedSampler, UV.xy + DestTexelOffsets * 0.25 * float2(-1, 1) );
	float DeviceZ2 = DepthTexture.Sample( GlobalPointClampedSampler, UV.xy + DestTexelOffsets * 0.25 * float2( 1,-1) );
	float DeviceZ3 = DepthTexture.Sample( GlobalPointClampedSampler, UV.xy + DestTexelOffsets * 0.25 * float2( 1, 1) );
	const float MaxDeviceZ = max(max(DeviceZ0, DeviceZ1), max(DeviceZ2, DeviceZ3));
	const float MinDeviceZ = min(min(DeviceZ0, DeviceZ1), min(DeviceZ2, DeviceZ3));

	const uint2 PixelPos = uint2(UV.xy * DestBufferSize); // == SV_Position - 0.5
	const uint2 PixelPosStep = (PixelPos / 2) * 2;
	
	OutDepth = (PixelPos.x - PixelPosStep.x) != (PixelPos.y - PixelPosStep.y) ? MaxDeviceZ : MinDeviceZ;
}
