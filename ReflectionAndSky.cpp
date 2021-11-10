
void FDeferredShadingSceneRenderer::RenderDeferredReflectionsAndSkyLighting(
	FRDGBuilder& GraphBuilder,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTexturesUniformBuffer,
	FRDGTextureMSAA SceneColorTexture,
	FRDGTextureRef DynamicBentNormalAOTexture,
	FRDGTextureRef VelocityTexture,
	FHairStrandsRenderingData* HairDatas)
{
	assume(!ShouldRenderDistanceFieldAO());
	assume(RayTracingReflectionOptions.bEnabled);
	assume(!bComposePlanarReflections);
	assume(DynamicBentNormalAOTexture == nullptr);
	assume(!Scene->HasVolumetricCloud());
	assume(!bCheckerboardSubsurfaceRendering);

	const bool bSkyLight = Scene->SkyLight
		&& (Scene->SkyLight->ProcessedTexture || Scene->SkyLight->bRealTimeCaptureEnabled)
		&& !Scene->SkyLight->bHasStaticLighting;
	const bool bDynamicSkyLight = ShouldRenderDeferredDynamicSkyLight(Scene, ViewFamily);
	const bool bReflectionEnv = ShouldDoReflectionEnvironment();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(GraphBuilder.RHICmdList);
	FRDGTextureRef AmbientOcclusionTexture = GraphBuilder.RegisterExternalTexture(SceneContext.bScreenSpaceAOIsValid ? SceneContext.ScreenSpaceAO : GSystemTextures.WhiteDummy);	
	DynamicBentNormalAOTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.WhiteDummy);
	FSceneTextureParameters SceneTextures = GetSceneTextureParameters(GraphBuilder);

	for (FViewInfo& View : Views)
	{
		const bool bScreenSpaceReflections = ShouldRenderScreenSpaceReflections(View);
		
		FRDGTextureRef ReflectionsColor = nullptr;
		if (bScreenSpaceReflections)
		{
			int32 DenoiserMode = GetReflectionsDenoiserMode();

			bool bDenoise = DenoiserMode != 0 && CVarDenoiseSSR.GetValueOnRenderThread();
			bool bTemporalFilter = !bDenoise && View.ViewState && IsSSRTemporalPassRequired(View);
			assume(!bDenoise);
			assume(!bTemporalFilter);

			IScreenSpaceDenoiser::FReflectionsInputs DenoiserInputs;
			IScreenSpaceDenoiser::FReflectionsRayTracingConfig DenoiserConfig;

			ESSRQuality SSRQuality;
			GetSSRQualityForView(View, &SSRQuality, &DenoiserConfig);
			RenderScreenSpaceReflections(
				GraphBuilder, SceneTextures, SceneColorTexture.Resolve, View, SSRQuality, bDenoise, &DenoiserInputs);
			
			ReflectionsColor = DenoiserInputs.Color;
		}

		bool bRequiresApply = ReflectionsColor != nullptr || bSkyLight || bReflectionEnv;

		if (bRequiresApply)
		{
			bool bHasBoxCaptures = (View.NumBoxReflectionCaptures > 0);
			bool bHasSphereCaptures = (View.NumSphereReflectionCaptures > 0);

			FReflectionEnvironmentSkyLightingPS::FParameters* PassParameters = GraphBuilder.AllocParameters<FReflectionEnvironmentSkyLightingPS::FParameters>();

			// Setup the parameters of the shader.
			{
				...
				//phgphg materialAO
				extern int32 GMaterialAOMode;
				PassParameters->MaterialAOMode = GMaterialAOMode;
				//phgphg materialAO -end

				PassParameters->BentNormalAOTexture = DynamicBentNormalAOTexture;
				PassParameters->BentNormalAOSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();

				PassParameters->AmbientOcclusionTexture = AmbientOcclusionTexture;
				PassParameters->AmbientOcclusionSampler = TStaticSamplerState<SF_Point>::GetRHI();

				PassParameters->ScreenSpaceReflectionsTexture = ReflectionsColor ? ReflectionsColor : GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
				PassParameters->ScreenSpaceReflectionsSampler = TStaticSamplerState<SF_Point>::GetRHI();

				PassParameters->CloudSkyAOTexture = GraphBuilder.RegisterExternalTexture(GSystemTextures.BlackDummy);
				PassParameters->CloudSkyAOSampler = TStaticSamplerState<SF_Bilinear>::GetRHI();
				PassParameters->CloudSkyAOEnabled = 0;

				PassParameters->PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
				PassParameters->PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

				PassParameters->SceneTextures = SceneTextures;

				PassParameters->ViewUniformBuffer = View.ViewUniformBuffer;
				PassParameters->ReflectionCaptureData = View.ReflectionCaptureUniformBuffer;
				{
					FReflectionUniformParameters ReflectionUniformParameters;
					SetupReflectionUniformParameters(View, ReflectionUniformParameters);
					PassParameters->ReflectionsParameters = CreateUniformBufferImmediate(ReflectionUniformParameters, UniformBuffer_SingleDraw);
				}
				PassParameters->ForwardLightData = View.ForwardLightingResources->ForwardLightDataUniformBuffer;
			}

			PassParameters->RenderTargets[0] = FRenderTargetBinding(SceneColorTexture.Target, ERenderTargetLoadAction::ELoad);

			auto PermutationVector = FReflectionEnvironmentSkyLightingPS::BuildPermutationVector(
				View, 
				bHasBoxCaptures, 	// REFLECTION_COMPOSITE_HAS_BOX_CAPTURES
				bHasSphereCaptures, // REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES
				false, 				// SUPPORT_DFAO_INDIRECT_OCCLUSION
				bSkyLight, 			// ENABLE_SKY_LIGHT
				bDynamicSkyLight, 	// ENABLE_DYNAMIC_SKY_LIGHT
				false, 				// APPLY_SKY_SHADOWING
				false);				// RAY_TRACED_REFLECTIONS

			TShaderMapRef<FReflectionEnvironmentSkyLightingPS> PixelShader(View.ShaderMap, PermutationVector);
			ClearUnusedGraphResources(PixelShader, PassParameters);

			GraphBuilder.AddPass(
				RDG_EVENT_NAME("ReflectionEnvironmentAndSky"),
				PassParameters,
				ERDGPassFlags::Raster,
				[PassParameters, &View, PixelShader](FRHICommandList& InRHICmdList)
			{
				FGraphicsPipelineStateInitializer GraphicsPSOInit;
				FPixelShaderUtils::InitFullscreenPipelineState(InRHICmdList, View.ShaderMap, PixelShader, GraphicsPSOInit);
				GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, BO_Add, BF_One, BF_One, BO_Add, BF_One, BF_One>::GetRHI();
				SetGraphicsPipelineState(InRHICmdList, GraphicsPSOInit);
				SetShaderParameters(InRHICmdList, PixelShader, PixelShader.GetPixelShader(), *PassParameters);
				
				InRHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, 0.0f, View.ViewRect.Max.X, View.ViewRect.Max.Y, 1.0f);
				FPixelShaderUtils::DrawFullscreenTriangle(InRHICmdList);
			});
		}
	}

	AddResolveSceneColorPass(GraphBuilder, Views, SceneColorTexture);
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionUniformParameters,)
	SHADER_PARAMETER(FVector4, SkyLightParameters)
	SHADER_PARAMETER(float, SkyLightCubemapBrightness)
	SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightCubemapSampler)
	SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightBlendDestinationCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyLightBlendDestinationCubemapSampler)
	SHADER_PARAMETER_TEXTURE(TextureCubeArray, ReflectionCubemap)
	SHADER_PARAMETER_SAMPLER(SamplerState, ReflectionCubemapSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D, PreIntegratedGF)
	SHADER_PARAMETER_SAMPLER(SamplerState, PreIntegratedGFSampler)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FReflectionUniformParameters, "ReflectionStruct");


void SetupReflectionUniformParameters(const FViewInfo& View, FReflectionUniformParameters& OutParameters)
{
	FTextureRHIRef SkyLightTextureResource = GBlackTextureCube->TextureRHI;
	FSamplerStateRHIRef SkyLightCubemapSampler = TStaticSamplerState<SF_Trilinear>::GetRHI();
	FTexture* SkyLightBlendDestinationTextureResource = GBlackTextureCube;
	float ApplySkyLightMask = 0;
	float BlendFraction = 0;
	bool bSkyLightIsDynamic = false;
	float SkyAverageBrightness = 1.0f;

	const bool bApplySkyLight = View.Family->EngineShowFlags.SkyLighting;
	const FScene* Scene = (const FScene*) View.Family->Scene;

	if (Scene
		&& Scene->SkyLight
		&& (Scene->SkyLight->ProcessedTexture || (Scene->SkyLight->bRealTimeCaptureEnabled && Scene->ConvolvedSkyRenderTargetReadyIndex >= 0))
		&& bApplySkyLight)
	{
		const FSkyLightSceneProxy& SkyLight = *Scene->SkyLight;

		if (Scene->SkyLight->bRealTimeCaptureEnabled && Scene->ConvolvedSkyRenderTargetReadyIndex >= 0)
		{
			// Cannot blend with this capture mode as of today.
			SkyLightTextureResource = Scene->ConvolvedSkyRenderTarget[Scene->ConvolvedSkyRenderTargetReadyIndex]->GetRenderTargetItem().ShaderResourceTexture;
		}
		else if (Scene->SkyLight->ProcessedTexture)
		{
			SkyLightTextureResource = SkyLight.ProcessedTexture->TextureRHI;
			SkyLightCubemapSampler = SkyLight.ProcessedTexture->SamplerStateRHI;
			BlendFraction = SkyLight.BlendFraction;

			if (SkyLight.BlendFraction > 0.0f && SkyLight.BlendDestinationProcessedTexture)
			{
				if (SkyLight.BlendFraction < 1.0f)
				{
					SkyLightBlendDestinationTextureResource = SkyLight.BlendDestinationProcessedTexture;
				}
				else
				{
					SkyLightTextureResource = SkyLight.BlendDestinationProcessedTexture->TextureRHI;
					SkyLightCubemapSampler = SkyLight.ProcessedTexture->SamplerStateRHI;
					BlendFraction = 0;
				}
			}
		}

		ApplySkyLightMask = 1;
		bSkyLightIsDynamic = !SkyLight.bHasStaticLighting && !SkyLight.bWantsStaticShadowing;
		SkyAverageBrightness = SkyLight.AverageBrightness;
	}

	const int32 CubemapWidth = SkyLightTextureResource->GetSizeXYZ().X;
	const float SkyMipCount = FMath::Log2(CubemapWidth) + 1.0f;

	OutParameters.SkyLightCubemap = SkyLightTextureResource;
	OutParameters.SkyLightCubemapSampler = SkyLightCubemapSampler;
	OutParameters.SkyLightBlendDestinationCubemap = SkyLightBlendDestinationTextureResource->TextureRHI;
	OutParameters.SkyLightBlendDestinationCubemapSampler = SkyLightBlendDestinationTextureResource->SamplerStateRHI;
	OutParameters.SkyLightParameters = FVector4(SkyMipCount - 1.0f, ApplySkyLightMask, bSkyLightIsDynamic ? 1.0f : 0.0f, BlendFraction);
	OutParameters.SkyLightCubemapBrightness = SkyAverageBrightness;

	// Note: GBlackCubeArrayTexture has an alpha of 0, which is needed to represent invalid data so the sky cubemap can still be applied
	FRHITexture* CubeArrayTexture = (SupportsTextureCubeArray(View.FeatureLevel))? GBlackCubeArrayTexture->TextureRHI : GBlackTextureCube->TextureRHI;

	if (View.Family->EngineShowFlags.ReflectionEnvironment
		&& SupportsTextureCubeArray(View.FeatureLevel)
		&& Scene
		&& Scene->ReflectionSceneData.CubemapArray.IsValid()
		&& Scene->ReflectionSceneData.RegisteredReflectionCaptures.Num())
	{
		CubeArrayTexture = Scene->ReflectionSceneData.CubemapArray.GetRenderTarget().ShaderResourceTexture;
	}

	OutParameters.ReflectionCubemap = CubeArrayTexture;
	OutParameters.ReflectionCubemapSampler = TStaticSamplerState<SF_Trilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

	OutParameters.PreIntegratedGF = GSystemTextures.PreintegratedGF->GetRenderTargetItem().ShaderResourceTexture;
	OutParameters.PreIntegratedGFSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ReflectionEnvironmentSkyLighting(
	in float4 SvPosition : SV_Position,
	out float4 OutColor : SV_Target0)
{
	float2 BufferUV = SvPositionToBufferUV(SvPosition);
	float2 ScreenPosition = SvPositionToScreenPosition(SvPosition).xy;
	FGBufferData GBuffer = GetGBufferDataFromSceneTextures(BufferUV, true, MaterialAOMode > 1);
	
	float3 DiffuseColor = GBuffer.DiffuseColor;
	float3 SpecularColor = GBuffer.SpecularColor;
	RemapClearCoatDiffuseAndSpecularColor(GBuffer, ScreenPosition, DiffuseColor, SpecularColor);

	float AmbientOcclusion = (bKeepMaterialAO ? GBuffer.GBufferAO : 1.0f) * 
		AmbientOcclusionTexture.SampleLevel(AmbientOcclusionSampler, BufferUV, 0).r
	
	uint ShadingModelID = GBuffer.ShadingModelID;
	float3 BentNormal = GBuffer.WorldNormal;
	
#if APPLY_SKY_SHADOWING
	BentNormal = UpsampleDFAO(BufferUV, GBuffer.Depth, GBuffer.WorldNormal);
#endif
	
	OutColor = 0.0f;

	if (ShadingModelID == SHADINGMODELID_UNLIT)
		return;

#if ENABLE_DYNAMIC_SKY_LIGHT
	float3 SkyLighting = SkyLightDiffuse(GBuffer, AmbientOcclusion, BufferUV, ScreenPosition, BentNormal, DiffuseColor);
	FLightAccumulator LightAccumulator = (FLightAccumulator)0;
	LightAccumulator_Add(LightAccumulator, SkyLighting, SkyLighting, 1.0f, UseSubsurfaceProfile(ShadingModelID));
	OutColor = LightAccumulator_GetResult(LightAccumulator);
#endif

	OutColor.xyz += ReflectionEnvironment(
		GBuffer, AmbientOcclusion, BufferUV, ScreenPosition, SvPosition, BentNormal, SpecularColor);
}

float3 ReflectionEnvironment(
	FGBufferData GBuffer, 
	float AmbientOcclusion, 
	float2 BufferUV, 
	float2 ScreenPosition, 
	float4 SvPosition, 
	float3 BentNormal, 
	float3 SpecularColor)
{
	assume(GBuffer.ShadingModelID != SHADINGMODELID_CLEAR_COAT);
	assume(GBuffer.Anisotropy == 0.0);

	float IndirectIrradiance = GBuffer.IndirectIrradiance;

	float3 WorldPosition = mul(float4(ScreenPosition * GBuffer.Depth, GBuffer.Depth, 1), View.ScreenToWorld).xyz;
	float3 CameraToPixel = normalize(WorldPosition - View.WorldCameraOrigin);
	float3 N = GBuffer.WorldNormal;
	float3 V = -CameraToPixel;
	float3 R = reflect(CameraToPixel, N);
	float NoV = saturate(dot( N, V ));
	
	// Point lobe in off-specular peak direction
	{
		float a = Square(GBuffer.Roughness);
		R = lerp( N, R, (1-a) * (sqrt(1-a) + a) ); // ?
	}
	
	float RoughnessSq = GBuffer.Roughness * GBuffer.Roughness;
	float SpecularOcclusion = saturate( pow( NoV + AmbientOcclusion, RoughnessSq ) - 1 + AmbientOcclusion ); // ?

	float2 LocalPosition = SvPosition.xy - View.ViewRectMin.xy;
	uint GridIndex = ComputeLightGridCellIndex(uint2(LocalPosition.x, LocalPosition.y), GBuffer.Depth);
	uint NumCulledEntryIndex = (ForwardLightData.NumGridCells + GridIndex) * NUM_CULLED_LIGHTS_GRID_STRIDE;
	uint NumCulledReflectionCaptures = min(ForwardLightData.NumCulledLightsGrid[NumCulledEntryIndex + 0], ForwardLightData.NumReflectionCaptures);
	uint DataStartIndex = ForwardLightData.NumCulledLightsGrid[NumCulledEntryIndex + 1];

	float4 SSR = Texture2DSample(ScreenSpaceReflectionsTexture, ScreenSpaceReflectionsSampler, BufferUV);
	
#if ENABLE_SKY_LIGHT
	if (ReflectionStruct.SkyLightParameters.z > 0) // bSkyLightIsDynamic && showflag == 1
	{
		IndirectIrradiance += Luminance(GetSkySHDiffuse(GBuffer.WorldNormal) * View.SkyLightColor.rgb) * length(BentNormal);
	}
#endif

	float3 Color = SSR.rgb + View.PreExposure * GatherRadiance( 
		(1 - SSR.a) * SpecularOcclusion , WorldPosition, R, GBuffer.Roughness, BentNormal, IndirectIrradiance, GBuffer.ShadingModelID, 
		NumCulledReflectionCaptures, DataStartIndex);

	Color *= EnvBRDF( SpecularColor, GBuffer.Roughness, NoV );

	return max(Color, 0.0);
}

float3 GatherRadiance(
	float CompositeAlpha, 
	float3 WorldPosition, 
	float3 RayDirection, 
	float Roughness, 
	float3 BentNormal, 
	float IndirectIrradiance, 
	uint ShadingModelID, 
	uint NumCulledReflectionCaptures, 
	uint CaptureDataStartIndex)
{
	return CompositeReflectionCapturesAndSkylight(
		CompositeAlpha, 
		WorldPosition, 
		RayDirection, 
		Roughness, 
		IndirectIrradiance, 
		1.0f, //IndirectSpecularOcclusion
		0,    //ExtraIndirectSpecular 
		NumCulledReflectionCaptures, 
		CaptureDataStartIndex, , ,);
}


float3 GetLookupVectorForSphereCapture(float3 ReflectionVector, float3 WorldPosition, float4 SphereCapturePositionAndRadius, float NormalizedDistanceToCapture, float3 LocalCaptureOffset, inout float DistanceAlpha)
{
	float3 ProjectedCaptureVector = ReflectionVector;
	float ProjectionSphereRadius = SphereCapturePositionAndRadius.w;
	float SphereRadiusSquared = ProjectionSphereRadius * ProjectionSphereRadius;

	float3 LocalPosition = WorldPosition - SphereCapturePositionAndRadius.xyz;
	float LocalPositionSqr = dot(LocalPosition, LocalPosition);

	// Find the intersection between the ray along the reflection vector and the capture's sphere
	float3 QuadraticCoef;
	QuadraticCoef.x = 1;
	QuadraticCoef.y = dot(ReflectionVector, LocalPosition);
	QuadraticCoef.z = LocalPositionSqr - SphereRadiusSquared;

	float Determinant = QuadraticCoef.y * QuadraticCoef.y - QuadraticCoef.z;

	// Only continue if the ray intersects the sphere
	if (Determinant >= 0)
	{
		float FarIntersection = sqrt(Determinant) - QuadraticCoef.y;

		float3 LocalIntersectionPosition = LocalPosition + FarIntersection * ReflectionVector;
		ProjectedCaptureVector = LocalIntersectionPosition - LocalCaptureOffset;
		// Note: some compilers don't handle smoothstep min > max (this was 1, .6)
		//DistanceAlpha = 1.0 - smoothstep(.6, 1, NormalizedDistanceToCapture);

		float x = saturate( 2.5 * NormalizedDistanceToCapture - 1.5 );
		DistanceAlpha = 1 - x*x*(3 - 2*x);
	}
	return ProjectedCaptureVector;
}

float3 CompositeReflectionCapturesAndSkylight(
	float CompositeAlpha, 
	float3 WorldPosition, 
	float3 RayDirection, 
	float Roughness, 
	float IndirectIrradiance, 
	float IndirectSpecularOcclusion,
	float3 ExtraIndirectSpecular,
	uint NumCapturesAffectingTile,
	uint CaptureDataStartIndex, , ,)
{
	float Mip = ComputeReflectionCaptureMipFromRoughness(Roughness, View.ReflectionCubemapMaxMip);
	float4 ImageBasedReflections = float4(0, 0, 0, CompositeAlpha);
	float2 CompositedAverageBrightness = float2(0.0f, 1.0f);

	// Accumulate reflections from captures affecting this tile, 
	// applying largest captures first so that the smallest ones display on top
	for (uint TileCaptureIndex = 0; TileCaptureIndex < NumCapturesAffectingTile; TileCaptureIndex++) 
	{
		if (ImageBasedReflections.a < 0.001)
		{
			break;
		}

		uint CaptureIndex = ForwardLightData.CulledLightDataGrid[CaptureDataStartIndex + TileCaptureIndex];
	
		float4 CapturePositionAndRadius = ReflectionCapture.PositionAndRadius[CaptureIndex];
		float4 CaptureProperties = ReflectionCapture.CaptureProperties[CaptureIndex];

		float3 CaptureVector = WorldPosition - CapturePositionAndRadius.xyz;
		float CaptureVectorLength = sqrt(dot(CaptureVector, CaptureVector));		
		float NormalizedDistanceToCapture = saturate(CaptureVectorLength / CapturePositionAndRadius.w);

		if (CaptureVectorLength < CapturePositionAndRadius.w)
		{
			float3 ProjectedCaptureVector = RayDirection;
			float4 CaptureOffsetAndAverageBrightness = ReflectionCapture.CaptureOffsetAndAverageBrightness[CaptureIndex];

			// Fade out based on distance to capture
			float DistanceAlpha = 0;
		
		#if REFLECTION_COMPOSITE_HAS_BOX_CAPTURES
			#if REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES
			// Box
			if (CaptureProperties.b > 0)
			#endif
			{
				ProjectedCaptureVector = GetLookupVectorForBoxCapture(RayDirection, WorldPosition, CapturePositionAndRadius, ReflectionCapture.BoxTransform[CaptureIndex], ReflectionCapture.BoxScales[CaptureIndex], CaptureOffsetAndAverageBrightness.xyz, DistanceAlpha);
			}
		#endif

		#if REFLECTION_COMPOSITE_HAS_SPHERE_CAPTURES
			// Sphere
			#if REFLECTION_COMPOSITE_HAS_BOX_CAPTURES
			else
			#endif
			{
				ProjectedCaptureVector = GetLookupVectorForSphereCapture(RayDirection, WorldPosition, CapturePositionAndRadius, NormalizedDistanceToCapture, CaptureOffsetAndAverageBrightness.xyz, DistanceAlpha);
			}
		#endif

			float CaptureArrayIndex = CaptureProperties.g;

			{
				float4 Sample = ReflectionStruct.ReflectionCubemap.SampleLevel(ReflectionStruct.ReflectionCubemapSampler, float4(ProjectedCaptureVector, CaptureArrayIndex), Mip);

				Sample.rgb *= CaptureProperties.r;
				Sample *= DistanceAlpha;

				// Under operator (back to front)
				ImageBasedReflections.rgb += Sample.rgb * ImageBasedReflections.a * IndirectSpecularOcclusion;
				ImageBasedReflections.a *= 1 - Sample.a;

				float AverageBrightness = CaptureOffsetAndAverageBrightness.w;
				CompositedAverageBrightness.x += AverageBrightness * DistanceAlpha * CompositedAverageBrightness.y;
				CompositedAverageBrightness.y *= 1 - DistanceAlpha;
			}
		}
	}

	// Apply indirect lighting scale while we have only accumulated reflection captures
	ImageBasedReflections.rgb *= View.IndirectLightingColorScale;
	CompositedAverageBrightness.x *= Luminance( View.IndirectLightingColorScale );

#if ENABLE_SKY_LIGHT
	if (ReflectionStruct.SkyLightParameters.y > 0)
	{
		float SkyAverageBrightness = 1.0f;
		float3 SkyLighting = GetSkyLightReflectionSupportingBlend(RayDirection, Roughness, SkyAverageBrightness);

		if (ReflectionStruct.SkyLightParameters.z < 1) // Stationary Sky
		{
			ImageBasedReflections.rgb += SkyLighting * ImageBasedReflections.a * IndirectSpecularOcclusion;
			CompositedAverageBrightness.x += SkyAverageBrightness * CompositedAverageBrightness.y;
		}
		else // Movable Sky
		{
			ExtraIndirectSpecular += SkyLighting * IndirectSpecularOcclusion;
		}
	}
#endif

	ImageBasedRefelctions.rgb *= ComputeMixingWeight(IndirectIrradiance, CompositedAverageBrightness.x, Roughness);
	ImageBasedReflections.rgb += ImageBasedReflections.a * ExtraIndirectSpecular;

	return ImageBasedReflections.rgb;
}

float ComputeMixingWeight(float IndirectIrradiance, float AverageBrightness, float Roughness)
{
	float3 v = View.ReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight;

	float MixingAlpha = smoothstep(0, 1, saturate(Roughness*v.x + v.y));
	float MixingWeight = IndirectIrradiance / max(AverageBrightness, .0001f);
	MixingWeight = min(MixingWeight, v.z);

	return lerp(1.0f, MixingWeight, MixingAlpha);
}

FVector GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight()
{
	float Range = 1.0f / FMath::Max(TEXT("r.EndRoughness") - TEXT("r.BeginRoughness"), .001f);

	if (TEXT("r.DoMixing") == 0)
	{
		return FVector(0, 0, TEXT("r.MaxWeight"));
	}
	else if (TEXT("r.MixBasedOnRoughness") == 0)
	{
		return FVector(0, 1, TEXT("r.MaxWeight"));
	}

	return FVector(Range, -TEXT("r.BeginRoughness") * Range, TEXT("r.MaxWeight"));
}

void FViewInfo::SetupUniformBufferParameters(...)
{
	ViewUniformShaderParameters.ReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight = GetReflectionEnvironmentRoughnessMixingScaleBiasAndLargestWeight();
}

