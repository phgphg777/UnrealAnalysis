// EngineShowFlags.Diffuse
// EngineShowFlags.Specular
void UGameViewportClient::Draw(FViewport* InViewport, FCanvas* SceneCanvas)
{
	FSceneView* View = LocalPlayer->CalcSceneView(&ViewFamily, ViewLocation, ViewRotation, InViewport, nullptr, PassType);

	if (View)
	{
		if (!View->Family->EngineShowFlags.Diffuse)
		{
			View->DiffuseOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		}

		if (!View->Family->EngineShowFlags.Specular)
		{
			View->SpecularOverrideParameter = FVector4(0.f, 0.f, 0.f, 0.f);
		}
	}
}

// EngineShowFlags.GlobalIllumination
void FSceneView::EndFinalPostprocessSettings(const FSceneViewInitOptions& ViewInitOptions)
{
	if (!Family->EngineShowFlags.Lighting || !Family->EngineShowFlags.GlobalIllumination)
	{
		FinalPostProcessSettings.IndirectLightingColor = FLinearColor(0,0,0,0);
		FinalPostProcessSettings.IndirectLightingIntensity = 0.0f;
	}
}

void FViewInfo::SetupUniformBufferParameters(...)
{
	ViewUniformShaderParameters.IndirectLightingColorScale = 
		FVector(FinalPostProcessSettings.IndirectLightingColor) * FinalPostProcessSettings.IndirectLightingIntensity;

}

void TVolumetricFogLightScatteringCS::SetParameters(...)
{
	float v = 0;

	if (View.Family->EngineShowFlags.GlobalIllumination && View.Family->EngineShowFlags.VolumetricLightmap)
	{
		v = FogInfo.VolumetricFogStaticLightingScatteringIntensity;
	}
	SetShaderValue(RHICmdList, ShaderRHI, StaticLightingScatteringIntensity, v);
}

// EngineShowFlags.ScreenSpaceReflections
bool ShouldRenderScreenSpaceReflections(const FViewInfo& View)
{
	if(!View.Family->EngineShowFlags.ScreenSpaceReflections)
	{
		return false;
	}
	...
}

void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLighting(...)
{
	if(ShouldRenderScreenSpaceReflections(View))
	{
		RenderScreenSpaceReflections(
			GraphBuilder, SceneTextures, SceneColorTexture.Resolve, View, SSRQuality, bDenoise, &DenoiserInputs);
	}
}

// EngineShowFlags.ReflectionEnvironment
void FSceneRenderer::SetupSceneReflectionCaptureBuffer()
{
	if (View.Family->EngineShowFlags.ReflectionEnvironment)
	{
		View.NumBoxReflectionCaptures = Scene->ReflectionSceneData.NumBoxCaptures;
		View.NumSphereReflectionCaptures = Scene->ReflectionSceneData.NumSphereCaptures;
		...
	}
}