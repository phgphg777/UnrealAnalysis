
struct FScreenVertexOutput
{
	noperspective MaterialFloat2 UV : TEXCOORD0;
	float4 Position : SV_POSITION;
};

/** Used for calculating vertex positions and UVs when drawing with DrawRectangle */
void DrawRectangle( in float4 InPosition, in float2 InTexCoord, out float4 OutPosition, out float2 OutTexCoord)
{
	OutPosition = InPosition;
	OutPosition.xy = -1.0f + 2.0f * (DrawRectangleParameters.PosScaleBias.zw + (InPosition.xy * DrawRectangleParameters.PosScaleBias.xy)) * DrawRectangleParameters.InvTargetSizeAndTextureSize.xy;
	OutPosition.xy *= float2( 1, -1 );
	OutTexCoord.xy = (DrawRectangleParameters.UVScaleBias.zw + (InTexCoord.xy * DrawRectangleParameters.UVScaleBias.xy)) * DrawRectangleParameters.InvTargetSizeAndTextureSize.zw;
}

void Main(
	float2 InPosition : ATTRIBUTE0,
	float2 InUV       : ATTRIBUTE1,
	out FScreenVertexOutput Output
	)
{
	DrawRectangle( float4( InPosition, 0, 1 ), InUV, Output.Position, Output.UV);
}

float3 GetCubemapVector(float2 ScaledUVs)
{
	float3 CubeCoordinates;

	//@todo - this could be a 3x3 matrix multiply
	if (CubeFace == 0)
	{
		CubeCoordinates = float3(1, -ScaledUVs.y, -ScaledUVs.x);
	}
	else if (CubeFace == 1)
	{
		CubeCoordinates = float3(-1, -ScaledUVs.y, ScaledUVs.x);
	}
	else if (CubeFace == 2)
	{
		CubeCoordinates = float3(ScaledUVs.x, 1, ScaledUVs.y);
	}
	else if (CubeFace == 3)
	{
		CubeCoordinates = float3(ScaledUVs.x, -1, -ScaledUVs.y);
	}
	else if (CubeFace == 4)
	{
		CubeCoordinates = float3(ScaledUVs.x, -ScaledUVs.y, 1);
	}
	else
	{
		CubeCoordinates = float3(-ScaledUVs.x, -ScaledUVs.y, -1);
	}

	return CubeCoordinates;
}

UEditorEngine::Tick()
{
	USkyLightComponent::UpdateSkyCaptureContents(EditorContext.World());
	{
		SkyCapturesToUpdate.Num() && UpdateSkyCaptureContentsArray(WorldToUpdate, SkyCapturesToUpdate, true);
		{
			WorldToUpdate->Scene->UpdateSkyCaptureContents(
				CaptureComponent, // in
				CaptureComponent->bCaptureEmissiveOnly, // in
				CaptureComponent->Cubemap, // in
				CaptureComponent->ProcessedSkyTexture, // out
				CaptureComponent->AverageBrightness, // out
				CaptureComponent->IrradianceEnvironmentMap, // out 
				NULL) // out
			{
				ENQUEUE_RENDER_COMMAND(ClearCommand, CaptureCommand || CopyCubemapCommand, FilterCommand, CopyCommand)
				{
					// Clear ReflectionColorScratchCubemap[0], ReflectionColorScratchCubemap[1], Mip0-7 
					ClearScratchCubemaps(RHICmdList, CubemapSize);

					if (CaptureComponent->SourceType == SLS_CapturedScene)
						CaptureSceneToScratchCubemap(RHICmdList, SceneRenderer, (ECubeFace)CubeFace, CubemapSize, bCapturingForSkyLight, bLowerHemisphereIsBlack, LowerHemisphereColor);
					else
						CopyCubemapToScratchCubemap(RHICmdList, InnerFeatureLevel, SourceCubemap, CubemapSize, true, bLowerHemisphereIsBlack, SourceCubemapRotation, LowerHemisphereColor);
						{
							// ReflectionColorScratchCubemap[0], Mip0  <-  USkyLightComponent::Cubemap
							FScreenVS(TEXT("/Engine/Private/ScreenVertexShader.usf"),TEXT("Main"));
							FCopyCubemapToCubeFacePS("/Engine/Private/ReflectionEnvironmentShaders.usf", "CopyCubemapToCubeFaceColorPS");
						}

					ComputeAverageBrightness(RHICmdList, InFeatureLevel, CubemapSize, *AverageBrightness);
					{
						CreateCubeMips( RHICmdList, FeatureLevel, NumMips, DownSampledCube );
						{
							// ReflectionColorScratchCubemap[0], Mip1-7  <-  ReflectionColorScratchCubemap[0], Mip0-6
							FScreenVS();
							FCubeFilterPS("/Engine/Private/ReflectionEnvironmentShaders.usf", "DownsamplePS");
						}
						OutAverageBrightness = ComputeSingleAverageBrightnessFromCubemap(RHICmdList, FeatureLevel, CubmapSize, DownSampledCube)
						{
							FPostProcessVS();
							FComputeBrightnessPS();
						}
					}

					FilterReflectionEnvironment(RHICmdList, InFeatureLevel, CubemapSize, IrradianceEnvironmentMap);
					{
						// ReflectionColorScratchCubemap[0], Mip0  <-  FLinearColor::Black
						FScreenVS();
						FOneColorPS(TEXT("/Engine/Private/OneColorShader.usf"),TEXT("MainPixelShader"));

						CreateCubeMips( RHICmdList, FeatureLevel, NumMips, DownSampledCube )
						{
							// ReflectionColorScratchCubemap[0], Mip1-7  <-  ReflectionColorScratchCubemap[0], Mip0-6
							FScreenVS();
							FCubeFilterPS("/Engine/Private/ReflectionEnvironmentShaders.usf", "DownsamplePS");
						}

						ComputeDiffuseIrradiance(RHICmdList, FeatureLevel, DownSampledCube.ShaderResourceTexture, DiffuseConvolutionSourceMip, OutIrradianceEnvironmentMap)
						{
							// In the below..
						}

						// ReflectionColorScratchCubemap[1], Mip0-7  <-  ReflectionColorScratchCubemap[1]
						FScreenVS();
						TCubeFilterPS<0>(TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("FilterPS"));
					}

					CopyToSkyTexture(RHICmdList, Scene, OutProcessedTexture)
					{
						FSceneRenderTargetItem& FilteredCube = FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[1]->GetRenderTargetItem();
						RHICmdList.CopyTexture(FilteredCube.ShaderResourceTexture, ProcessedTexture->TextureRHI, CopyInfo);
					}
				}
			}
		}
	}
}



ComputeDiffuseIrradiance(RHICmdList, FeatureLevel, DownSampledCube.ShaderResourceTexture, DiffuseConvolutionSourceMip, OutIrradianceEnvironmentMap)
{
	for (int32 CoefficientIndex = 0; CoefficientIndex < FSHVector3::MaxSHBasis; CoefficientIndex++)
	{
		// DiffuseIrradianceScratchCubemap[0], Mip0  <-  ReflectionColorScratchCubemap[0], Mip2
		FScreenVS();
		FCopyDiffuseIrradiancePS(TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("DiffuseIrradianceCopyPS"));

		// DiffuseIrradianceScratchCubemap[1], Mip1  <-  DiffuseIrradianceScratchCubemap[0], Mip0
		// DiffuseIrradianceScratchCubemap[0], Mip2  <-  DiffuseIrradianceScratchCubemap[1], Mip1
		// DiffuseIrradianceScratchCubemap[1], Mip3  <-  DiffuseIrradianceScratchCubemap[0], Mip2
		// DiffuseIrradianceScratchCubemap[0], Mip4  <-  DiffuseIrradianceScratchCubemap[1], Mip3
		// DiffuseIrradianceScratchCubemap[1], Mip5  <-  DiffuseIrradianceScratchCubemap[0], Mip4
		FScreenVS();
		FAccumulateDiffuseIrradiancePS(TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("DiffuseIrradianceAccumulatePS"));

		// SkySHIrradianceMap  <-  DiffuseIrradianceScratchCubemap[1], Mip5
		FScreenVS();
		FAccumulateCubeFacesPS(TEXT("/Engine/Private/ReflectionEnvironmentShaders.usf"),TEXT("AccumulateCubeFacesPS"));
	}

	//OutIrradianceEnvironmentMap  <-  SkySHIrradianceMap
	RHICmdList.ReadSurfaceFloatData(EffectiveRT.ShaderResourceTexture, FIntRect(0, 0, FSHVector3::MaxSHBasis, 1), SurfaceData, CubeFace_PosX, 0, 0);
	for (int32 CoefficientIndex = 0; CoefficientIndex < FSHVector3::MaxSHBasis; CoefficientIndex++)
	{
		const FLinearColor CoefficientValue(SurfaceData[CoefficientIndex]);
		OutIrradianceEnvironmentMap->R.V[CoefficientIndex] = CoefficientValue.R;
		OutIrradianceEnvironmentMap->G.V[CoefficientIndex] = CoefficientValue.G;
		OutIrradianceEnvironmentMap->B.V[CoefficientIndex] = CoefficientValue.B;
	}	
}








void CopyCubemapToScratchCubemap(FRHICommandList& RHICmdList, ERHIFeatureLevel::Type FeatureLevel, UTextureCube* SourceCubemap, int32 CubemapSize, bool bIsSkyLight, bool bLowerHemisphereIsBlack, float SourceCubemapRotation, const FLinearColor& LowerHemisphereColorValue)
{
	SCOPED_DRAW_EVENT(RHICmdList, CopyCubemapToScratchCubemap);
	check(SourceCubemap);
	
	const int32 EffectiveSize = CubemapSize;
	FSceneRenderTargetItem& EffectiveColorRT =  FSceneRenderTargets::Get(RHICmdList).ReflectionColorScratchCubemap[0]->GetRenderTargetItem();

	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EffectiveColorRT.TargetableTexture);

	for (uint32 CubeFace = 0; CubeFace < CubeFace_MAX; CubeFace++)
	{
		// Copy the captured scene into the cubemap face
		FRHIRenderPassInfo RPInfo(EffectiveColorRT.TargetableTexture, ERenderTargetActions::DontLoad_Store, nullptr, 0, CubeFace);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyCubemapToScratchCubemapRP"));

		const FTexture* SourceCubemapResource = SourceCubemap->Resource;
		const FIntPoint SourceDimensions(SourceCubemapResource->GetSizeX(), SourceCubemapResource->GetSizeY());
		const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
		RHICmdList.SetViewport(0, 0, 0.0f, EffectiveSize, EffectiveSize, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		TShaderMapRef<FScreenVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
		TShaderMapRef<FCopyCubemapToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		PixelShader->SetParameters(RHICmdList, SourceCubemapResource, CubeFace, bIsSkyLight, bLowerHemisphereIsBlack, SourceCubemapRotation, LowerHemisphereColorValue);

		DrawRectangle(
			RHICmdList,
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Width(), ViewRect.Height(),
			0, 0, 
			SourceDimensions.X, SourceDimensions.Y,
			FIntPoint(ViewRect.Width(), ViewRect.Height()),
			SourceDimensions,
			*VertexShader);

		RHICmdList.EndRenderPass();
	}
}

{
	SceneRenderer->Render(RHICmdList);
	
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_CaptureSceneToScratchCubemap_Flush);
		FRHICommandListExecutor::GetImmediateCommandList().ImmediateFlush(EImmediateFlushType::FlushRHIThread);
	}

	// some platforms may not be able to keep enqueueing commands like crazy, this will
	// allow them to restart their command buffers
	RHICmdList.SubmitCommandsAndFlushGPU();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.AllocateReflectionTargets(RHICmdList, CubemapSize);

	auto ShaderMap = GetGlobalShaderMap(FeatureLevel);

	const int32 EffectiveSize = CubemapSize;
	FSceneRenderTargetItem& EffectiveColorRT =  SceneContext.ReflectionColorScratchCubemap[0]->GetRenderTargetItem();
	RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EffectiveColorRT.TargetableTexture);

	{
		SCOPED_DRAW_EVENT(RHICmdList, CubeMapCopyScene);
		
		// Copy the captured scene into the cubemap face
		FRHIRenderPassInfo RPInfo(EffectiveColorRT.TargetableTexture, ERenderTargetActions::DontLoad_Store, nullptr, 0, CubeFace);
		RHICmdList.BeginRenderPass(RPInfo, TEXT("CubeMapCopySceneRP"));

		const FIntRect ViewRect(0, 0, EffectiveSize, EffectiveSize);
		RHICmdList.SetViewport(0, 0, 0.0f, EffectiveSize, EffectiveSize, 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();

		TShaderMapRef<FCopyToCubeFaceVS> VertexShader(GetGlobalShaderMap(FeatureLevel));
		TShaderMapRef<FCopySceneColorToCubeFacePS> PixelShader(GetGlobalShaderMap(FeatureLevel));

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*PixelShader);
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		PixelShader->SetParameters(RHICmdList, SceneRenderer->Views[0], bCapturingForSkyLight, bLowerHemisphereIsBlack, LowerHemisphereColor);
		VertexShader->SetParameters(RHICmdList, SceneRenderer->Views[0]);

		DrawRectangle( 
			RHICmdList,
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Width(), ViewRect.Height(),
			ViewRect.Min.X, ViewRect.Min.Y, 
			ViewRect.Width() * GSupersampleCaptureFactor, ViewRect.Height() * GSupersampleCaptureFactor,
			FIntPoint(ViewRect.Width(), ViewRect.Height()),
			SceneContext.GetBufferSizeXY(),
			*VertexShader);

		RHICmdList.EndRenderPass();
	}
}

class FCopySceneColorToCubeFacePS : public FGlobalShader
{
	FSceneTextureShaderParameters SceneTextureParameters;	
	
	FShaderResourceParameter InTexture;
	FShaderResourceParameter InTextureSampler;
	
	FShaderParameter SkyLightCaptureParameters;
	FShaderParameter LowerHemisphereColor;

	void SetParameters(, 
		const FViewInfo& View, 
		bool bCapturingForSkyLight, 
		bool bLowerHemisphereIsBlack, 
		const FLinearColor& LowerHemisphereColorValue)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI, View.ViewUniformBuffer);
		SceneTextureParameters.Set( , , View.FeatureLevel, ESceneTextureSetupMode::All);

		InTexture = FSceneRenderTargets::Get(RHICmdList).GetSceneColor()->GetRenderTargetItem().ShaderResourceTexture;
		InTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

		FVector SkyLightParametersValue = FVector::ZeroVector;
		FScene* Scene = (FScene*)View.Family->Scene;

		if (bCapturingForSkyLight)
		{
			// When capturing reflection captures, support forcing all low hemisphere lighting to be black
			SkyLightParametersValue = FVector(0, 0, bLowerHemisphereIsBlack ? 1.0f : 0.0f);
		}
		else if (Scene->SkyLight && !Scene->SkyLight->bHasStaticLighting)
		{
			// When capturing reflection captures and there's a stationary sky light, mask out any pixels whose depth classify it as part of the sky
			// This will allow changing the stationary sky light at runtime
			SkyLightParametersValue = FVector(1, Scene->SkyLight->SkyDistanceThreshold, 0);
		}
		else
		{
			// When capturing reflection captures and there's no sky light, or only a static sky light, capture all depth ranges
			SkyLightParametersValue = FVector(2, 0, 0);
		}

		SkyLightCaptureParameters = SkyLightParametersValue;
		LowerHemisphereColor = LowerHemisphereColorValue;
	}
}

class FCopyCubemapToCubeFacePS : public FGlobalShader
{
	FShaderParameter CubeFace;
	
	FShaderResourceParameter SourceTexture;
	FShaderResourceParameter SourceTextureSampler;
	
	FShaderParameter SkyLightCaptureParameters;
	FShaderParameter LowerHemisphereColor;
	
	FShaderParameter SinCosSourceCubemapRotation;

	void SetParameters(, 
		const FTexture* SourceCubemap, 
		uint32 CubeFaceValue, 
		bool bIsSkyLight, 
		float SourceCubemapRotation, 
		bool bLowerHemisphereIsBlack, 
		const FLinearColor& LowerHemisphereColorValue)
	{
		SinCosSourceCubemapRotation = FVector2D(FMath::Sin(SourceCubemapRotation), FMath::Cos(SourceCubemapRotation));
		CubeFace = CubeFaceValue;

		SourceTexture = SourceCubemap;
		SourceTextureSampler = ;
		
		SkyLightCaptureParameters = FVector(bIsSkyLight ? 1.0f : 0.0f, 0.0f, bLowerHemisphereIsBlack ? 1.0f : 0.0f);
		LowerHemisphereColor = LowerHemisphereColorValue;
	}
}