DFAO

bOverride_DFAO = false;
bUseDFAO = true;
bToStaticLights = false;
bToDynamicSky = true;
ObjectDFAOEndDistance = 100.0f;
GlobalDFAOStartDistance = 100.0f;
GlobalDFAOEndDistance = 2000.0f;
bUseObjectDFAO = true;
bUseGlobalDFAO = true;

GlobalDFResolution = 128;
GlobalDFLOD0Extent = 2500.0f;
GlobalDFLODMultiplier = 2.0f;
SolidAngleScale = 1.0f;

DistancingMethod
NumSamplesInCone = 10;
SampleMultiplier = 0.5f;

bUseSecondDistanceSampling = true;
bUseSecondDirectionSet = true;

ComputeBentNormal = true;

Contrast = 0.01f;
OcclusionExponent = 1.0f;
MinOcclusion = 0.0f;
OcclusionTint = FColor(0, 0, 0, 1);


   ShouldPrepareForDistanceFieldAO == true  
=> ShouldPrepareGlobalDistanceField == true
=> ShouldPrepareDistanceFieldScene == true

q1. how to compute object occlusion?
q2. ssao apply to non-sky?
q3. stationary sky





All Parameters:

# USkyLightComponent
o float OcclusionMaxDistance;	
o float Contrast;				-> FSkyLightParameters::ContrastAndNormalizeMulAdd.X
o float OcclusionExponent;		-> FSkyLightParameters::OcclusionExponent
o float MinOcclusion;			-> FSkyLightParameters::OcclusionTintAndMinOcclusion.W
o FColor OcclusionTint;			-> FSkyLightParameters::OcclusionTintAndMinOcclusion.XYZ
o Enum OcclusionCombineMode;	-> FSkyLightParameters::OcclusionCombineMode


# DistanceFieldAmbientOcclusion.cpp
o int32 GDistanceFieldAO = 1;						// TEXT("r.DistanceFieldAO")
o int32 GDistanceFieldAOQuality = 2; 				// TEXT("r.AOQuality")
o int32 GDistanceFieldAOApplyToStaticIndirect = 0;	// TEXT("r.AOApplyToStaticIndirect")
- int32 GDistanceFieldAOSpecularOcclusionMode = 1;	// TEXT("r.AOSpecularOcclusionMode")
o float GAOStepExponentScale = .5f;					// TEXT("r.AOStepExponentScale")
@ float GAOMaxViewDistance = 20000;					// TEXT("r.AOMaxViewDistance")
- int32 GAOComputeShaderNormalCalculation = 0;		// TEXT("r.AOComputeShaderNormalCalculation")
- int32 GAOSampleSet = 1;							// TEXT("r.AOSampleSet")	
- int32 GAOOverwriteSceneColor = 0;					// TEXT("r.AOOverwriteSceneColor")
- int32 GAOJitterConeDirections = 0;				// TEXT("r.AOJitterConeDirections")
o int32 GAOObjectDistanceField = 1;					// TEXT("r.AOObjectDistanceField")
- int32 GDistanceFieldAOTileSizeX = 16;
- int32 GDistanceFieldAOTileSizeY = 16;
- const FVector SpacedVectors9[];
- const FVector RelaxedSpacedVectors9[];
o uint32 GAONumConeSteps = 10;


# DistanceFieldAmbientOcclusion.h
- static const int32 GAOMaxSupportedLevel = 6;
o const int32 NumConeSampleDirections = 9;
o const int32 GAODownsampleFactor = 2;
- static int32 CulledTileDataStride = 2;
- static int32 ConeTraceObjectsThreadGroupSize = 64;
- const int32 RecordConeDataStride = 10;
- const int32 NumVisibilitySteps = 10;


# DistanceFieldScreenGridLighting.cpp
o int32 GAOUseJitter = 1;								// TEXT("r.AOUseJitter")
o int32 GConeTraceDownsampleFactor = 4;
o FVector2D JitterOffsets[4];
o int32 GConeTraceGlobalDFTileSize = 8;
- int32 GCombineConesSizeX = 8;


# DistanceFieldLightingPost.cpp
int32 GAOUseHistory = 1;					// TEXT("r.AOUseHistory")
int32 GAOClearHistory = 0;					// TEXT("r.AOClearHistory")
int32 GAOHistoryStabilityPass = 1;			// TEXT("r.AOHistoryStabilityPass")
float GAOHistoryWeight = .85f;				// TEXT("r.AOHistoryWeight")
float GAOHistoryDistanceThreshold = 30;		// TEXT("r.AOHistoryDistanceThreshold")
float GAOViewFadeDistanceScale = .7f;		// TEXT("r.AOViewFadeDistanceScale")


# GlobalDistanceField.cpp
o int32 GAOGlobalDistanceField = 1;								// TEXT("r.AOGlobalDistanceField")
o float GAOGlobalDFStartDistance = 100;							// TEXT("r.AOGlobalDFStartDistance")
o int32 GAOGlobalDFResolution = 128;							// TEXT("r.AOGlobalDFResolution")
o float GAOGlobalDFClipmapDistanceExponent = 2;					// TEXT("r.AOGlobalDFClipmapDistanceExponent")
- int32 GAOUpdateGlobalDistanceField = 1;						// TEXT("r.AOUpdateGlobalDistanceField")
@ int32 GAOGlobalDistanceFieldCacheMostlyStaticSeparately = 1;	// TEXT("r.AOGlobalDistanceFieldCacheMostlyStaticSeparately")
@ int32 GAOGlobalDistanceFieldPartialUpdates = 1;				// TEXT("r.AOGlobalDistanceFieldPartialUpdates")
@ int32 GAOGlobalDistanceFieldStaggeredUpdates = 1;				// TEXT("r.AOGlobalDistanceFieldStaggeredUpdates")
@ int32 GAOGlobalDistanceFieldForceFullUpdate = 0;				// TEXT("r.AOGlobalDistanceFieldForceFullUpdate")
- int32 GAOLogGlobalDistanceFieldModifiedPrimitives = 0;		// TEXT("r.AOGlobalDistanceFieldLogModifiedPrimitives")
- int32 GAOGlobalDistanceFieldRepresentHeightfields = 1;		// TEXT("r.AOGlobalDistanceFieldRepresentHeightfields")
- uint32 CullObjectsGroupSize = 64;
- const int32 GMaxGridCulledObjects = 2048;
- const int32 GCullGridTileSize = 16;
- const int32 HeightfieldCompositeTileSize = 8;


# DistancefieldAtlas.cpp
o TEXT("r.DistanceFields.MaxPerMeshResolution") = 128;
o TEXT("r.DistanceFields.DefaultVoxelDensity") = 0.1f;


# DistanceFieldObjectManagement.cpp
float GAOMaxObjectBoundingRadius = 50000;			// TEXT("r.AOMaxObjectBoundingRadius")



GlobalDistanceField.cpp
Line 950, 3 -> GMaxGlobalDistanceFieldClipmaps - 1
Line 979, 4 -> GMaxGlobalDistanceFieldClipmaps - 1



RenderDFAOAsIndirectShadowing(RHICmdList, SceneContext.SceneVelocity, DynamicBentNormalAO);
{
	if ( GDistanceFieldAOApplyToStaticIndirect 
		&& ViewFamily.EngineShowFlags.DistanceFieldAO )
	{
		const float OcclusionMaxDistance = !Scene->SkyLight->bWantsStaticShadowing ? Scene->SkyLight->OcclusionMaxDistance :  Scene->DefaultMaxDistanceFieldOcclusionDistance;
		FDistanceFieldAOParameters Parameters(OcclusionMaxDistance);

		RenderDistanceFieldLighting(, Parameters, , DynamicBentNormalAO, true,);
	}
}

RenderDeferredReflectionsAndSkyLighting(RHICmdList, DynamicBentNormalAO, SceneContext.SceneVelocity);
{
	bool bDynamicSkyLight = 
		!Scene->SkyLight->bWantsStaticShadowing
		&& !Scene->SkyLight->bHasStaticLighting
		&& Scene->SkyLight->ProcessedTexture
		&& !ShouldRenderRayTracingSkyLight(Scene->SkyLight)
		&& ViewFamily.EngineShowFlags.SkyLighting ;

	bool bApplySkyShadowing = false;
	if (bDynamicSkyLight)
	{
		if ( !GDistanceFieldAOApplyToStaticIndirect 
			&& ViewFamily.EngineShowFlags.DistanceFieldAO
			&& ViewFamily.EngineShowFlags.AmbientOcclusion
			&& Scene->SkyLight->bCastShadows )
		{
			FDistanceFieldAOParameters Parameters(Scene->SkyLight->OcclusionMaxDistance, Scene->SkyLight->Contrast);

			bApplySkyShadowing = RenderDistanceFieldLighting(, Parameters, , DynamicBentNormalAO, false,);
		}
	}

	bool bSupportDFAOIndirectOcclusion = DynamicBentNormalAO != NULL;
	auto PermutationVector = FReflectionEnvironmentSkyLightingPS::BuildPermutationVector(, , , 
		bSupportDFAOIndirectOcclusion,	// SUPPORT_DFAO_INDIRECT_OCCLUSION
		bReflectionCapture, 			// SPECULAR_BOUNCE
		bSkyLight, 						// ENABLE_SKY_LIGHT
		bDynamicSkyLight, 				// ENABLE_DYNAMIC_SKY_LIGHT
		bApplySkyShadowing, 			// APPLY_SKY_SHADOWING
		bRayTracedReflections);			// RAY_TRACED_REFLECTIONS
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FReflectionEnvironmentSkyLightingPS> PixelShader(View.ShaderMap, PermutationVector);
	PixelShader->SetParameters(, , ReflectionsColor->GetRenderTargetItem().ShaderResourceTexture, DynamicBentNormalAO);
	DrawRectangle();
}


bool FDeferredShadingSceneRenderer::RenderDistanceFieldLighting( , 
	const FDistanceFieldAOParameters& Parameters, ,
	TRefCountPtr<IPooledRenderTarget>& OutDynamicBentNormalAO,
	bool bModulateToSceneColor, )
{	
	bool ret = false;

	if (GDistanceFieldAO 
		&& GDistanceFieldAOQuality > 0
		&& GDistanceFieldVolumeTextureAtlas.VolumeTextureRHI
		&& Scene->DistanceFieldSceneData.NumObjectsInBuffer)
	{
		ret = true;

		bool UseAOObjectDistanceField = GAOObjectDistanceField && GDistanceFieldAOQuality >= 2;

		if (UseAOObjectDistanceField)
		{
			CullObjectsToView(RHICmdList, Scene, View, Parameters, GAOCulledObjectBuffers);
		}

		TRefCountPtr<IPooledRenderTarget> DistanceFieldNormal;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DistanceFieldNormal, TEXT("DistanceFieldNormal"));
		ComputeDistanceFieldNormal(RHICmdList, Views, DistanceFieldNormal->GetRenderTargetItem(), Parameters);
		{
			// DistanceFieldNormal  <- GBufferA, SceneDepth
			FPostProcessVS();
			FComputeDistanceFieldNormalPS(TEXT("/Engine/Private/DistanceFieldScreenGridLighting.usf"),TEXT("ComputeDistanceFieldNormalPS"));
			{
				void ComputeDistanceFieldNormalPS(
					in float4 UVAndScreenPos : TEXCOORD0, 
					in float4 SVPos : SV_POSITION,
					out float4 OutColor : SV_Target0)
				{
					float2 ScreenUV = float2((floor(SVPos.xy) * DOWNSAMPLE_FACTOR + View.ViewRectMin.xy + .5f) * View.BufferSizeAndInvSize.zw);
					FGBufferData GBufferData = GetGBufferData(ScreenUV);

					OutColor = float4(GBufferData.WorldNormal.xyz, CalcSceneDepth(ScreenUV));
				}
			}
		}

		if (UseAOObjectDistanceField)
		{
			BuildTileObjectLists(RHICmdList, Scene, Views, DistanceFieldNormal->GetRenderTargetItem(), Parameters);
		}
		
		TRefCountPtr<IPooledRenderTarget> DownsampledBentNormal;
		GRenderTargetPool.FindFreeElement(RHICmdList, Desc, DownsampledBentNormal, TEXT("DownsampledBentNormal"));
		RenderDistanceFieldAOScreenGrid( , ,
			Parameters, ,
			DistanceFieldNormal, 
			DownsampledBentNormal);
		{
		}

		TRefCountPtr<IPooledRenderTarget> BentNormalOutput;	
		UpdateHistory( , , TEXT("DistanceFieldAOHistory"), ,
			DistanceFieldNormal->GetRenderTargetItem(),
			DownsampledBentNormal->GetRenderTargetItem(),
			&View.State->DistanceFieldAOHistoryViewRect,
			&View.State->DistanceFieldAOHistoryRT,
			BentNormalOutput,
			Parameters);
		{
		}
	
		RenderCapsuleShadowsForMovableSkylight(RHICmdList, BentNormalOutput) {}

		FRHIRenderPassInfo RPInfo(	SceneContext.GetSceneColorSurface(), , 
									SceneContext.GetSceneDepthSurface(), , );
		RHICmdList.BeginRenderPass(RPInfo, TEXT("DistanceFieldAO"));

		if (bModulateToSceneColor)
		{
			UpsampleBentNormalAO(RHICmdList, Views, BentNormalOutput, bModulateToSceneColor && !bVisualizeAmbientOcclusion);
			{
				//
				FPostProcessVS();
				TDistanceFieldAOUpsamplePS<true>(TEXT("/Engine/Private/DistanceFieldLightingPost.usf"),TEXT("AOUpsamplePS"));
			}
		}
		OutDynamicBentNormalAO = BentNormalOutput;

		RHICmdList.EndRenderPass();
	}

	return ret;
}

