

class FLightCacheInterface {
	FUniformBufferRHIRef PrecomputedLightingUniformBuffer;/** The uniform buffer holding mapping the lightmap policy resources. */
	const FLightmapResourceCluster* ResourceCluster; // = MeshMapBuildData->ResourceCluster; in FStaticMeshSceneProxy::FLODInfo::FLODInfo()
}

// FBasePassMeshProcessor::AddMeshBatch() ->
void FUniformLightMapPolicy::GetVertexShaderBindings(
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const ElementDataType& ShaderElementData,
	const VertexParametersType* VertexShaderParameters,
	FMeshDrawSingleShaderBindings& ShaderBindings) 
{
	ShaderBindings.Add(
		VertexShaderParameters->PrecomputedLightingBufferParameter, 
		LCI->GetPrecomputedLightingBuffer());
	ShaderBindings.Add(
		VertexShaderParameters->IndirectLightingCacheParameter, 
		PrimitiveSceneProxy->GetPrimitiveSceneInfo()->IndirectLightingCacheUniformBuffer);
	ShaderBindings.Add(
		VertexShaderParameters->LightmapResourceCluster, 
		LCI->GetResourceCluster()->UniformBuffer);
}




// SceneManagement.h
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FLightmapResourceClusterShaderParameters,ENGINE_API)
	SHADER_PARAMETER_TEXTURE(Texture2D, LightMapTexture)
	SHADER_PARAMETER_TEXTURE(Texture2D, SkyOcclusionTexture) 
	SHADER_PARAMETER_TEXTURE(Texture2D, AOMaterialMaskTexture) 
	SHADER_PARAMETER_TEXTURE(Texture2D, StaticShadowTexture)
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTLightMapTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTLightMapTexture_1) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTSkyOcclusionTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTAOMaterialMaskTexture) // VT
	SHADER_PARAMETER_SRV(Texture2D<float4>, VTStaticShadowTexture) // VT
	SHADER_PARAMETER_SAMPLER(SamplerState, LightMapSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, SkyOcclusionSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, AOMaterialMaskSampler) 
	SHADER_PARAMETER_SAMPLER(SamplerState, StaticShadowTextureSampler)
	SHADER_PARAMETER_TEXTURE(Texture2D<uint4>, LightmapVirtualTexturePageTable0) // VT
	SHADER_PARAMETER_TEXTURE(Texture2D<uint4>, LightmapVirtualTexturePageTable1) // VT
END_GLOBAL_SHADER_PARAMETER_STRUCT()
// SceneManagement.cpp
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FLightmapResourceClusterShaderParameters, "LightmapResourceCluster");

// SceneManagement.cpp
void GetLightmapClusterResourceParameters(
	ERHIFeatureLevel::Type FeatureLevel, 
	const FLightmapClusterResourceInput& Input,
	IAllocatedVirtualTexture* AllocatedVT,
	FLightmapResourceClusterShaderParameters& Parameters)
{
	const bool bAllowHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel);
	const UTexture2D* LightMapTexture = Input.LightMapTextures[bAllowHighQualityLightMaps ? 0 : 1];

	Parameters.LightMapTexture = LightMapTexture ? LightMapTexture->TextureReference.TextureReferenceRHI.GetReference() : GBlackTexture->TextureRHI;
	Parameters.SkyOcclusionTexture = Input.SkyOcclusionTexture ? Input.SkyOcclusionTexture->TextureReference.TextureReferenceRHI.GetReference() : GWhiteTexture->TextureRHI;
	Parameters.AOMaterialMaskTexture = Input.AOMaterialMaskTexture ? Input.AOMaterialMaskTexture->TextureReference.TextureReferenceRHI.GetReference() : GBlackTexture->TextureRHI;

	Parameters.LightMapSampler = GetTextureSamplerState(LightMapTexture, GBlackTexture->SamplerStateRHI);
	Parameters.SkyOcclusionSampler = GetTextureSamplerState(Input.SkyOcclusionTexture, GWhiteTexture->SamplerStateRHI);
	Parameters.AOMaterialMaskSampler = GetTextureSamplerState(Input.AOMaterialMaskTexture, GBlackTexture->SamplerStateRHI);

	Parameters.StaticShadowTexture = Input.ShadowMapTexture ? Input.ShadowMapTexture->TextureReference.TextureReferenceRHI.GetReference() : GWhiteTexture->TextureRHI;
	Parameters.StaticShadowTextureSampler = GetTextureSamplerState(Input.ShadowMapTexture, GWhiteTexture->SamplerStateRHI);

	Parameters.LightmapVirtualTexturePageTable0 = GBlackTexture->TextureRHI;
	Parameters.LightmapVirtualTexturePageTable1 = GBlackTexture->TextureRHI;
}




// LightmapUniformShaderParameters.h
BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FPrecomputedLightingUniformParameters, ENGINE_API)
	SHADER_PARAMETER(FVector4, StaticShadowMapMasks) 				// TDistanceFieldShadowsAndLightMapPolicy
	SHADER_PARAMETER(FVector4, InvUniformPenumbraSizes) 			// TDistanceFieldShadowsAndLightMapPolicy
	SHADER_PARAMETER(FVector4, ShadowMapCoordinateScaleBias) 		// TDistanceFieldShadowsAndLightMapPolicy
	SHADER_PARAMETER(FVector4, LightMapCoordinateScaleBias) 								// TLightMapPolicy
	SHADER_PARAMETER_ARRAY_EX(FVector4, LightMapScale, [2], EShaderPrecisionModifier::Half) // TLightMapPolicy
	SHADER_PARAMETER_ARRAY_EX(FVector4, LightMapAdd, [2], EShaderPrecisionModifier::Half) 	// TLightMapPolicy
	SHADER_PARAMETER_ARRAY(FUintVector4, LightmapVTPackedPageTableUniform, [2]) // VT (1 page table, 2x uint4)
	SHADER_PARAMETER_ARRAY(FUintVector4, LightmapVTPackedUniform, [5]) // VT (5 layers, 1x uint4 per layer)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
// LightmapUniformShaderParameters.cpp
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FPrecomputedLightingUniformParameters, "PrecomputedLightingBuffer");

// LightmapUniformShaderParameters.cpp
void GetPrecomputedLightingParameters(
	ERHIFeatureLevel::Type FeatureLevel,
	FPrecomputedLightingUniformParameters& Parameters,
	const FLightCacheInterface* LCI
)
{
	// TDistanceFieldShadowsAndLightMapPolicy
	const FShadowMapInteraction ShadowMapInteraction = LCI ? LCI->GetShadowMapInteraction(FeatureLevel) : FShadowMapInteraction();
	if (ShadowMapInteraction.GetType() == SMIT_Texture)
	{
		Parameters.ShadowMapCoordinateScaleBias = FVector4(ShadowMapInteraction.GetCoordinateScale(), ShadowMapInteraction.GetCoordinateBias());
		Parameters.StaticShadowMapMasks = FVector4(ShadowMapInteraction.GetChannelValid(0), ShadowMapInteraction.GetChannelValid(1), ShadowMapInteraction.GetChannelValid(2), ShadowMapInteraction.GetChannelValid(3));
		Parameters.InvUniformPenumbraSizes = ShadowMapInteraction.GetInvUniformPenumbraSize();
	}
	else
	{
		Parameters.ShadowMapCoordinateScaleBias = FVector4(1, 1, 0, 0);
		Parameters.StaticShadowMapMasks = FVector4(1, 1, 1, 1);
		Parameters.InvUniformPenumbraSizes = FVector4(0, 0, 0, 0);
	}

	// TLightMapPolicy
	const FLightMapInteraction LightMapInteraction = LCI ? LCI->GetLightMapInteraction(FeatureLevel) : FLightMapInteraction();
	if (LightMapInteraction.GetType() == LMIT_Texture)
	{
		const bool bAllowHighQualityLightMaps = AllowHighQualityLightmaps(FeatureLevel) && LightMapInteraction.AllowsHighQualityLightmaps();

		// Vertex Shader
		const FVector2D LightmapCoordinateScale = LightMapInteraction.GetCoordinateScale();
		const FVector2D LightmapCoordinateBias = LightMapInteraction.GetCoordinateBias();
		Parameters.LightMapCoordinateScaleBias = FVector4(LightmapCoordinateScale.X, LightmapCoordinateScale.Y, LightmapCoordinateBias.X, LightmapCoordinateBias.Y);

		const FVector4* Scales = LightMapInteraction.GetScaleArray();
		const FVector4* Adds = LightMapInteraction.GetAddArray();
		for (uint32 CoefIndex = 0; CoefIndex < 2; ++CoefIndex)
		{
			Parameters.LightMapScale[CoefIndex] = Scales[CoefIndex];
			Parameters.LightMapAdd[CoefIndex] = Adds[CoefIndex];
		}
	}
	else
	{
		// Vertex Shader
		Parameters.LightMapCoordinateScaleBias = FVector4(1, 1, 0, 0);

		// Pixel Shader
		for (uint32 CoefIndex = 0; CoefIndex < 2; ++CoefIndex)
		{
			Parameters.LightMapScale[CoefIndex] = FVector4(1, 1, 1, 1);
			Parameters.LightMapAdd[CoefIndex] = FVector4(0, 0, 0, 0);
		}
	}
}

void FPrimitiveSceneInfo::AddToScene(FRHICommandListImmediate& RHICmdList, FScene* Scene, const TArrayView<FPrimitiveSceneInfo*>& SceneInfos, bool bUpdateStaticDrawLists, bool bAddToStaticDrawLists, bool bAsyncCreateLPIs)
{
	for (FPrimitiveSceneInfo* SceneInfo : SceneInfos)
	{
		SceneInfo->NumLightmapDataEntries = SceneInfo->UpdateStaticLightingBuffer();
		if (SceneInfo->NumLightmapDataEntries > 0 && bUseGPUScene)
		{
			SceneInfo->LightmapDataOffset = Scene->GPUScene.LightmapDataAllocator.Allocate(SceneInfo->NumLightmapDataEntries);
		}
	}
}


// bUseGPUScene == false /////////////////////////////////////////////////////////////////////////////////
int32 FPrimitiveSceneInfo::UpdateStaticLightingBuffer()
{
	FPrimitiveSceneProxy::FLCIArray LCIs;
	Proxy->GetLCIs(LCIs);
	/*
	void FStaticMeshSceneProxy::GetLCIs(FLCIArray& LCIs)
	{
		for (int32 LODIndex = 0; LODIndex < LODs.Num(); ++LODIndex)
		{
			FLightCacheInterface* LCI = &LODs[LODIndex];
			LCIs.Push(LCI);
		}
	}
	*/
	for (int32 i = 0; i < LCIs.Num(); ++i)
	{
		FLightCacheInterface* LCI = LCIs[i];
		if (LCI)
		{
			LCI->CreatePrecomputedLightingUniformBuffer_RenderingThread(Scene->GetFeatureLevel());
		}
	}

	return LCIs.Num();
}

void FLightCacheInterface::CreatePrecomputedLightingUniformBuffer_RenderingThread(ERHIFeatureLevel::Type FeatureLevel)
{
	if (LightMap || ShadowMap)
	{
		FPrecomputedLightingUniformParameters Parameters;
		GetPrecomputedLightingParameters(FeatureLevel, Parameters, this);
		if (PrecomputedLightingUniformBuffer)
		{
			RHIUpdateUniformBuffer(PrecomputedLightingUniformBuffer, &Parameters);
		}
		else
		{
			PrecomputedLightingUniformBuffer = FPrecomputedLightingUniformParameters::CreateUniformBuffer(Parameters, UniformBuffer_MultiFrame);
		}
	}
}
// bUseGPUScene == false /////////////////////////////////////////////////////////////////////////////////




// bUseGPUScene == true  /////////////////////////////////////////////////////////////////////////////////
class FScene : public FSceneInterface {
	FGPUScene GPUScene;
}

class FGPUScene {
	bool bUpdateAllPrimitives = false;
	TArray<int32> PrimitivesToUpdate;/** Indices of primitives that need to be updated in GPU Scene */
	TBitArray<> PrimitivesMarkedToUpdate;/** Bit array of all scene primitives. Set bit means that current primitive is in PrimitivesToUpdate array. */

	FRWBufferStructured PrimitiveBuffer;/** GPU mirror of Primitives */
	FScatterUploadBuffer PrimitiveUploadBuffer;
	FRWBufferStructured		LightmapDataBuffer;
	FScatterUploadBuffer	LightmapUploadBuffer;
	FGrowOnlySpanAllocator	LightmapDataAllocator;
};

class FPrimitiveSceneInfo : public FDeferredCleanupInterface {
	int32 LightmapDataOffset;/** Offset into the scene's lightmap data buffer, when GPUScene is enabled. */
}

struct FLightmapSceneShaderData {
	enum { LightmapDataStrideInFloat4s = 15 };
	FVector4 Data[LightmapDataStrideInFloat4s];
};

FLightmapSceneShaderData::FLightmapSceneShaderData(const class FLightCacheInterface* LCI, ERHIFeatureLevel::Type FeatureLevel)
{
	FPrecomputedLightingUniformParameters Parameters;
	GetPrecomputedLightingParameters(FeatureLevel, Parameters, LCI); // Parameters <- LCI
	Setup(Parameters); // Data <- Parameters
}

// FDeferredShadingSceneRenderer::Render() -> UpdateGPUScene() -> 
void UpdateGPUSceneInternal<FRWBufferStructured>(FRHICommandListImmediate& RHICmdList, FScene& Scene)
{
	const int32 NumPrimitiveDataUploads = Scene.GPUScene.PrimitivesToUpdate.Num();
	int32 NumLightmapDataUploads = 0;
	
	...

	if (NumLightmapDataUploads > 0)
	{
		Scene.GPUScene.LightmapUploadBuffer.Init(NumLightmapDataUploads, sizeof(FLightmapSceneShaderData::Data), true, TEXT("LightmapUploadBuffer"));

		for (int32 Index : Scene.GPUScene.PrimitivesToUpdate)
		{
			FPrimitiveSceneProxy* PrimitiveSceneProxy = Scene.PrimitiveSceneProxies[Index];

			FPrimitiveSceneProxy::FLCIArray LCIs;
			PrimitiveSceneProxy->GetLCIs(LCIs);

			const int32 LightmapDataOffset = PrimitiveSceneProxy->GetPrimitiveSceneInfo()->GetLightmapDataOffset();

			for (int32 i = 0; i < LCIs.Num(); i++)
			{
				FLightmapSceneShaderData LightmapSceneData(LCIs[i], Scene.GetFeatureLevel());
				Scene.GPUScene.LightmapUploadBuffer.Add(LightmapDataOffset + i, &LightmapSceneData.Data[0]);
			}
		}

		RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, EResourceTransitionPipeline::EGfxToCompute, Scene.GPUScene.LightmapDataBuffer.UAV);
		Scene.GPUScene.LightmapUploadBuffer.ResourceUploadTo(RHICmdList, Scene.GPUScene.LightmapDataBuffer, false);
		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, EResourceTransitionPipeline::EComputeToGfx, Scene.GPUScene.LightmapDataBuffer.UAV);
	}
}

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT_WITH_CONSTRUCTOR(FViewUniformShaderParameters, ENGINE_API)
	SHADER_PARAMETER_SRV(StructuredBuffer<float4>, LightmapSceneData)
END_GLOBAL_SHADER_PARAMETER_STRUCT()

void FViewInfo::SetupUniformBufferParameters(
	FSceneRenderTargets& SceneContext,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices,
	FBox* OutTranslucentCascadeBoundsArray,
	int32 NumTranslucentCascades,
	FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	ViewUniformShaderParameters.LightmapSceneData = Scene->GPUScene.LightmapDataBuffer.SRV;
}
// bUseGPUScene == true  /////////////////////////////////////////////////////////////////////////////////