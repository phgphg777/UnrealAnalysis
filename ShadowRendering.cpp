
void SetStateForShadowDepth(bool bReflectiveShadowmap, bool bOnePassPointLightShadow, FMeshPassProcessorRenderState& DrawRenderState)
{
	DrawRenderState.SetBlendState(TStaticBlendState<CW_NONE>::GetRHI());// Disable color writes
	DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}

FShadowDepthPassMeshProcessor::FShadowDepthPassMeshProcessor(
	const FScene* Scene,
	const FSceneView* InViewIfDynamicMeshCommand,
	const TUniformBufferRef<FViewUniformShaderParameters>& InViewUniformBuffer,
	FRHIUniformBuffer* InPassUniformBuffer,
	FShadowDepthType InShadowDepthType,
	FMeshPassDrawListContext* InDrawListContext)
	: FMeshPassProcessor(Scene, Scene->GetFeatureLevel(), InViewIfDynamicMeshCommand, InDrawListContext)
	, PassDrawRenderState(FMeshPassProcessorRenderState(InViewUniformBuffer, InPassUniformBuffer))
	, ShadowDepthType(InShadowDepthType)
{
	SetStateForShadowDepth(ShadowDepthType.bReflectiveShadowmap, ShadowDepthType.bOnePassPointLightShadow, PassDrawRenderState);
}

FMeshPassProcessor* CreateCSMShadowDepthPassProcessor(const FScene* Scene, const FSceneView* InViewIfDynamicMeshCommand, FMeshPassDrawListContext* InDrawListContext)
{
	check(InViewIfDynamicMeshCommand == nullptr);
	return new(FMemStack::Get()) FShadowDepthPassMeshProcessor(
		Scene,
		nullptr,
		Scene->UniformBuffers.CSMShadowDepthViewUniformBuffer,
		Scene->UniformBuffers.CSMShadowDepthPassUniformBuffer,
		CSMShadowDepthType,
		InDrawListContext);
}

void FProjectedShadowInfo::SetupMeshDrawCommandsForShadowDepth(FSceneRenderer& Renderer, FRHIUniformBuffer* PassUniformBuffer)
{
	FShadowDepthPassUniformParameters ShadowDepthParameters;
	ShadowDepthPassUniformBuffer = TUniformBufferRef<FShadowDepthPassUniformParameters>::CreateUniformBufferImmediate(ShadowDepthParameters, UniformBuffer_MultiFrame, EUniformBufferValidation::None);

	FShadowDepthPassMeshProcessor* MeshPassProcessor = new(FMemStack::Get()) FShadowDepthPassMeshProcessor(
		Renderer.Scene,
		ShadowDepthView,
		ShadowDepthView->ViewUniformBuffer,
		ShadowDepthPassUniformBuffer,
		GetShadowDepthType(),
		nullptr);
	
	const uint32 InstanceFactor = GetShadowDepthType().bOnePassPointLightShadow ? 6 : 1;

	ShadowDepthPass.DispatchPassSetup(
		Renderer.Scene,
		*ShadowDepthView,
		EMeshPass::Num,
		FExclusiveDepthStencil::DepthNop_StencilNop, // BasePass override Depth&Stencil state, thus no meaning!
		MeshPassProcessor,
		DynamicSubjectMeshElements,
		nullptr,
		NumDynamicSubjectMeshElements * InstanceFactor,
		SubjectMeshCommandBuildRequests,
		NumSubjectMeshCommandBuildRequestElements * InstanceFactor,
		ShadowDepthPassVisibleCommands);

	Renderer.DispatchedShadowDepthPasses.Add(&ShadowDepthPass);
}

void FShadowDepthPassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (!MeshBatch.CastShadow)
		return;

	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);

	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
	const EBlendMode BlendMode = Material.GetBlendMode();
	const bool bShouldCastShadow = Material.ShouldCastDynamicShadows();

	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);

	ERasterizerCullMode FinalCullMode;
	{
		const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);
		const bool bTwoSided = Material.IsTwoSided() || PrimitiveSceneProxy->CastsShadowAsTwoSided();
		const bool bReverseCullMode = ShadowDepthType.bOnePassPointLightShadow;
		FinalCullMode = bTwoSided ? CM_None : bReverseCullMode ? InverseCullMode(MeshCullMode) : MeshCullMode;
	}

	if ( bShouldCastShadow
		&& Material.GetMaterialDomain() != MD_Volume
		&& ShouldIncludeMaterialInDefaultOpaquePass(Material))
	{
		const FMaterialRenderProxy* EffectiveMaterialRenderProxy = &MaterialRenderProxy;
		const FMaterial* EffectiveMaterial = &Material;
		OverrideWithDefaultMaterialForShadowDepth(EffectiveMaterialRenderProxy, EffectiveMaterial, , ); // Override with the default material when possible.

		Process(
			MeshBatch, 
			BatchElementMask, 
			StaticMeshId, 
			PrimitiveSceneProxy, 
			*EffectiveMaterialRenderProxy, 
			*EffectiveMaterial, 
			MeshFillMode, FinalCullMode);
	}
}

void FShadowDepthPassMeshProcessor::Process(
	const FMeshBatch& MeshBatch,
	uint64 BatchElementMask,
	int32 StaticMeshId,
	const FPrimitiveSceneProxy* PrimitiveSceneProxy,
	const FMaterialRenderProxy& MaterialRenderProxy,
	const FMaterial& MaterialResource,
	ERasterizerFillMode MeshFillMode,
	ERasterizerCullMode MeshCullMode)
{
	TMeshProcessorShaders<
		FShadowDepthVS,
		FBaseHS,
		FBaseDS,
		FShadowDepthBasePS,
		FOnePassPointShadowDepthGS> ShadowDepthPassShaders;

	const bool bUsePositionOnlyVS = 
		&& MeshBatch.VertexFactory->SupportsPositionAndNormalOnlyStream()
		&& MaterialResource.WritesEveryPixel(true)
		&& !MaterialResource.MaterialModifiesMeshPosition_RenderThread();

	GetShadowDepthPassShaders<bRenderReflectiveShadowMap>(
		MaterialResource,
		MeshBatch.VertexFactory,
		FeatureLevel,
		ShadowDepthType.bDirectionalLight,
		ShadowDepthType.bOnePassPointLightShadow,
		bUsePositionOnlyVS,
		ShadowDepthPassShaders.VertexShader,
		ShadowDepthPassShaders.HullShader,
		ShadowDepthPassShaders.DomainShader,
		ShadowDepthPassShaders.PixelShader,
		ShadowDepthPassShaders.GeometryShader);

	FShadowDepthShaderElementData ShaderElementData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

	const FMeshDrawCommandSortKey SortKey = CalculateMeshStaticSortKey(ShadowDepthPassShaders.VertexShader, ShadowDepthPassShaders.PixelShader);

	const uint32 InstanceFactor = ShadowDepthType.bOnePassPointLightShadow ? 6 : 1;
	for (uint32 i = 0; i < InstanceFactor; i++)
	{
		ShaderElementData.LayerId = i;

		BuildMeshDrawCommands(
			MeshBatch,
			BatchElementMask,
			PrimitiveSceneProxy,
			MaterialRenderProxy,
			MaterialResource,
			PassDrawRenderState,
			ShadowDepthPassShaders,
			MeshFillMode,
			MeshCullMode,
			SortKey,
			bUsePositionOnlyVS ? EMeshPassFeatures::PositionAndNormalOnly : EMeshPassFeatures::Default,
			ShaderElementData);
	}
}

void GetShadowDepthPassShaders(
	const FMaterial& Material,
	const FVertexFactory* VertexFactory,
	ERHIFeatureLevel::Type FeatureLevel,
	bool bDirectionalLight,
	bool bOnePassPointLightShadow,
	bool bPositionOnlyVS,
	TShaderRef<FShadowDepthVS>& VertexShader,
	TShaderRef<FBaseHS>& HullShader,
	TShaderRef<FBaseDS>& DomainShader,
	TShaderRef<FShadowDepthBasePS>& PixelShader,
	TShaderRef<FOnePassPointShadowDepthGS>& GeometryShader)
{
	FVertexFactoryType* VFType = VertexFactory->GetType();

	const bool bUsePerspectiveCorrectShadowDepths = !bDirectionalLight && !bOnePassPointLightShadow;
	const bool bInitializeTessellationShaders = Material.GetTessellationMode() != MTM_NoTessellation && VFType->SupportsTessellationShaders();

	EShadowDepthVertexShaderMode VSMode = 
		bOnePassPointLightShadow ? VertexShadowDepth_OnePassPointLight :
		bUsePerspectiveCorrectShadowDepths ? VertexShadowDepth_PerspectiveCorrect : VertexShadowDepth_OutputDepth;

	EShadowDepthPixelShaderMode PSMode = 
		bOnePassPointLightShadow ? PixelShadowDepth_OnePassPointLight :
		bUsePerspectiveCorrectShadowDepths ? PixelShadowDepth_PerspectiveCorrect : PixelShadowDepth_NonPerspectiveCorrect;

	HullShader.Reset();
	DomainShader.Reset();
	GeometryShader.Reset();

	typedef TShadowDepthVS<VSMode,false,bPositionOnlyVS,bOnePassPointLightShadow> VSType;
	typedef TShadowDepthHS<VSMode,false> HSType;
	typedef TShadowDepthDS<VSMode,false> DSType;
	typedef FOnePassPointShadowDepthGS GSType;
	typedef TShadowDepthPS<PSMode,false> PSType;

	VertexShader = Material.GetShader<VSType> (VFType);

	if (bOnePassPointLightShadow)
	{
		GeometryShader = Material.GetShader<GSType> (VFType);
	}

	if (bInitializeTessellationShaders)
	{
		HullShader = Material.GetShader<HSType> (VFType);
		DomainShader = Material.GetShader<DSType> (VFType);
	}

	// bUsePerspectiveCorrectShadowDepths == true only for spot and per&pre point
	if (!bUsePerspectiveCorrectShadowDepths && Material.WritesEveryPixel(true) && !bRenderingReflectiveShadowMaps && VertexFactory->SupportsNullPixelShader())
	{
		PixelShader.Reset(); // Empty pixel shader!!
	}
	else
	{
		PixelShader = Material.GetShader<PSType> (VFType, false);
	}
}


void FProjectedShadowInfo::TransitionCachedShadowmap(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	if (CacheMode == SDCM_MovablePrimitivesOnly)
	{
		const FCachedShadowMapData& CachedShadowMapData = Scene->CachedShadowMaps.FindChecked(GetLightSceneInfo().Id);
		if (CachedShadowMapData.bCachedShadowMapHasPrimitives && CachedShadowMapData.ShadowMap.IsValid())
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, CachedShadowMapData.ShadowMap.DepthTarget->GetRenderTargetItem().ShaderResourceTexture);
		}
	}
}

void FProjectedShadowInfo::SetupShadowUniformBuffers(FRHICommandListImmediate& RHICmdList, FScene* Scene)
{
	FShadowDepthPassUniformParameters ShadowDepthPassParameters;
	SetupShadowDepthPassUniformBuffer(this, RHICmdList, *ShadowDepthView, ShadowDepthPassParameters, );
	
	if (IsWholeSceneDirectionalShadow())
	{
		Scene->UniformBuffers.CSMShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);
	}
	ShadowDepthPassUniformBuffer.UpdateUniformBufferImmediate(ShadowDepthPassParameters);

	UploadDynamicPrimitiveShaderDataForView(RHICmdList, *Scene, *ShadowDepthView); //??
}

// PerObject Shadow & CSM & SpotLight
void FSceneRenderer::RenderShadowDepthMapAtlases(FRHICommandListImmediate& RHICmdList)
{
	bool bCanUseParallelDispatch = RHICmdList.IsImmediate() &&  // translucent shadows are draw on the render thread, using a recursive cmdlist (which is not immediate)
		GRHICommandList.UseParallelAlgorithms() && TEXT("r.ParallelShadows");

	for (const FSortedShadowMapAtlas& ShadowMapAtlas : SortedShadowsForShadowDepthPass.ShadowMapAtlases)
	{
		FSceneRenderTargetItem& RenderTarget = ShadowMapAtlas.RenderTargets.DepthTarget->GetRenderTargetItem();
		check(RenderTarget.TargetableTexture->GetDepthClearValue() == 1.0f);

		TArray<FProjectedShadowInfo*> ParallelShadowPasses;
		TArray<FProjectedShadowInfo*> SerialShadowPasses;
		for (FProjectedShadowInfo* ProjectedShadowInfo : ShadowMapAtlas.Shadows)
		{
			const bool bDoParallelDispatch = bCanUseParallelDispatch &&
				(ProjectedShadowInfo->IsWholeSceneDirectionalShadow() || TEXT("r.ParallelShadowsNonWholeScene"));

			if (bDoParallelDispatch)
			{
				ParallelShadowPasses.Add(ProjectedShadowInfo);
			}
			else
			{
				SerialShadowPasses.Add(ProjectedShadowInfo);
			}
		}

		auto BeginShadowRenderPass = [this, &RenderTarget] (FRHICommandList& InRHICmdList, bool bPerformClear){...};

		if (ParallelShadowPasses.Num() > 0)
		{
			BeginShadowRenderPass(RHICmdList, true);// Clear before going wide.
			RHICmdList.EndRenderPass();
		
			for (FProjectedShadowInfo* ProjectedShadowInfo : ParallelShadowPasses)
			{
				ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);
				ProjectedShadowInfo->TransitionCachedShadowmap(RHICmdList, Scene);
				ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, true);
			}
		}

		assume(TEXT("r.Shadow.ForceSerialSingleRenderPass") == 1);	
		if (SerialShadowPasses.Num() > 0)
		{
			BeginShadowRenderPass(RHICmdList, true);

			for (FProjectedShadowInfo* ProjectedShadowInfo : SerialShadowPasses)
			{
				ProjectedShadowInfo->SetupShadowUniformBuffers(RHICmdList, Scene);
				ProjectedShadowInfo->TransitionCachedShadowmap(RHICmdList, Scene);
				ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, false);
			}
			
			RHICmdList.EndRenderPass();
		}

		RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTarget.TargetableTexture);
	}
}

void FProjectedShadowInfo::RenderDepth(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, bool bDoParallelDispatch)
{
	if (IsWholeSceneDirectionalShadow())
	{
		// CSM shadow depth cached mesh draw commands are all referencing the same view uniform buffer.  We need to update it before rendering each cascade.
		ShadowDepthView->ViewUniformBuffer.UpdateUniformBufferImmediate(*ShadowDepthView->CachedViewUniformShaderParameters);
	}

	FMeshPassProcessorRenderState DrawRenderState(*ShadowDepthView, ShadowDepthPassUniformBuffer);
	SetStateForShadowDepth(bReflectiveShadowmap, bOnePassPointLightShadow, DrawRenderState);
	
	RHICmdList.SetViewport(
		X + BorderSize, Y + BorderSize, 0.0f,
		X + BorderSize + ResolutionX, Y + BorderSize + ResolutionY, 1.0f);

	if (CacheMode == SDCM_MovablePrimitivesOnly)
	{
		if (bDoParallelDispatch)
		{
			BeginShadowRenderPass(RHICmdList, false);
		}

		CopyCachedShadowMap(RHICmdList, DrawRenderState, SceneRenderer, *ShadowDepthView);

		if (bDoParallelDispatch)
		{
			RHICmdList.EndRenderPass();
		}
	}

	if (bDoParallelDispatch)
	{
		bool bFlush = TEXT("r.RHICmdFlushRenderThreadTasksShadowPass") || TEXT("r.RHICmdFlushRenderThreadTasks");
		FScopedCommandListWaitForTasks Flusher(bFlush);

		FShadowParallelCommandListSet ParallelCommandListSet(*ShadowDepthView, SceneRenderer, RHICmdList, TEXT("r.RHICmdShadowDeferredContexts"), !bFlush, DrawRenderState, *this, BeginShadowRenderPass);
		ShadowDepthPass.DispatchDraw(&ParallelCommandListSet, RHICmdList);
	}
	else
	{
		ShadowDepthPass.DispatchDraw(nullptr, RHICmdList);
	}
}


FShadowDepthPassMeshProcessor::FShadowDepthPassMeshProcessor()
{
	PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}

// Action to take when a rendertarget is set.
enum class ERenderTargetLoadAction : uint8 { ENoAction, ELoad, EClear };

// Action to take when a rendertarget is unset or at the end of a pass. It has meaning only when FRHIRenderPassInfo::FDepthStencilEntry::ResolveTarget is assigned.
enum class ERenderTargetStoreAction : uint8 { ENoAction, EStore, EMultisampleResolve };

auto BeginShadowRenderPass = [this, &RenderTarget](FRHICommandList& InRHICmdList, bool bPerformClear)
{
	ERenderTargetLoadAction DepthLoadAction = bPerformClear 
		? ERenderTargetLoadAction::EClear : ERenderTargetLoadAction::ELoad;
	ERenderTargetActions DepthAction = (DepthLoadAction << 2) | ERenderTargetStoreAction::EStore;
	EDepthStencilTargetActions DepthStencilAction = (DepthAction << 4) | ERenderTargetActions::Load_Store

	FRHIRenderPassInfo RPInfo(
		RenderTarget.TargetableTexture, 
		DepthStencilAction, // thus, [load,store,load,store] or [clear,store,load,store] 
		nullptr,
		FExclusiveDepthStencil::DepthWrite_StencilWrite); //??

	InRHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, RenderTarget.TargetableTexture);
	InRHICmdList.BeginRenderPass(RPInfo, );
};

void FSceneRenderer::RenderShadowDepthMapAtlases()
{
	BeginShadowRenderPass(RHICmdList, true);
	{
		InRHICmdList.BeginRenderPass(RPInfo);
		{
			FD3D11DynamicRHI::RHISetRenderTargetsAndClear(const FRHISetRenderTargetsInfo& RenderTargetsInfo);
			{
				RHISetRenderTargets(, , &RenderTargetsInfo.DepthStencilRenderTarget);
				{
					if(bTargetChanged)
					{
						CommitRenderTargets(true);
						{
							Direct3DDeviceIMContext->OMSetRenderTargets(, , CurrentDepthStencilTarget);
						}
					}

					D3D11_TEXTURE2D_DESC DTTDesc;
					DepthTargetTexture->GetDesc(&DTTDesc);
					RHISetViewport(0.0f, 0.0f, 0.0f, (float)DTTDesc.Width, (float)DTTDesc.Height, 1.0f);
					{
						Direct3DDeviceIMContext->RSSetViewports(1,&Viewport);
						Direct3DDeviceIMContext->RSSetScissorRects(1, &ScissorRect);
					}
				}

				if (RenderTargetsInfo.bClearColor || RenderTargetsInfo.bClearStencil || RenderTargetsInfo.bClearDepth)
				{
					RHIClearMRTImpl(RenderTargetsInfo.bClearColor, RenderTargetsInfo.NumColorRenderTargets, ClearColors, RenderTargetsInfo.bClearDepth, DepthClear, RenderTargetsInfo.bClearStencil, StencilClear);
					{
						Direct3DDeviceIMContext->ClearDepthStencilView(DepthStencilView,ClearFlags,Depth,Stencil);
					}
				}
			}
		}
	}

	for (FProjectedShadowInfo* ProjectedShadowInfo : SerialShadowPasses)
	{
		ProjectedShadowInfo->RenderDepth(RHICmdList, this, BeginShadowRenderPass, false);
		{
			SetStateForView(RHICmdList);
			{
				RHICmdList.SetViewport(X + BorderSize, Y + BorderSize, 0.0f, X + BorderSize + ResolutionX, Y + BorderSize + ResolutionY, 1.0f);
				{
					Direct3DDeviceIMContext->RSSetViewports(1,&Viewport);
					Direct3DDeviceIMContext->RSSetScissorRects(1, &ScissorRect);
				}
			}

			...
		}
	}
	RHICmdList.EndRenderPass(); // Do nothing..
}


BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FShadowDepthPassUniformParameters,)
	SHADER_PARAMETER_STRUCT(FSceneTexturesUniformParameters, SceneTextures)
	SHADER_PARAMETER(FMatrix, ProjectionMatrix)
	SHADER_PARAMETER(FMatrix, ViewMatrix)
	SHADER_PARAMETER(FVector4, ShadowParams)
	SHADER_PARAMETER(float, bClampToNearPlane)
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewProjectionMatrices, [6])
	SHADER_PARAMETER_ARRAY(FMatrix, ShadowViewMatrices, [6])
END_GLOBAL_SHADER_PARAMETER_STRUCT()

void SetupShadowDepthPassUniformBuffer(
	const FProjectedShadowInfo* ShadowInfo, ,
	const FViewInfo& View,
	FShadowDepthPassUniformParameters& ShadowDepthPassParameters,)
{
	SetupSceneTextureUniformParameters(FSceneRenderTargets::Get(), View.FeatureLevel, ESceneTextureSetupMode::None, ShadowDepthPassParameters.SceneTextures);

	ShadowDepthPassParameters.ProjectionMatrix = FTranslationMatrix(ShadowInfo->PreShadowTranslation - View.ViewMatrices.GetPreViewTranslation()) * ShadowInfo->SubjectAndReceiverMatrix;	
	ShadowDepthPassParameters.ViewMatrix = ShadowInfo->ShadowViewMatrix;

	ShadowDepthPassParameters.ShadowParams = FVector4(
		ShadowInfo->GetShaderDepthBias(), 
		ShadowInfo->GetShaderSlopeDepthBias(), 
		ShadowInfo->GetShaderMaxSlopeDepthBias(), 
		ShadowInfo->bOnePassPointLightShadow ? 1 : ShadowInfo->InvMaxSubjectDepth);
	ShadowDepthPassParameters.bClampToNearPlane = ShadowInfo->IsWholeSceneDirectionalShadow() ? 1.0f : 0.0f;

	if (ShadowInfo->bOnePassPointLightShadow)
	{
		const FMatrix Translation = FTranslationMatrix(-View.ViewMatrices.GetPreViewTranslation());

		for (int32 FaceIndex = 0; FaceIndex < 6; FaceIndex++)
		{
			// Have to apply the pre-view translation to the view - projection matrices
			FMatrix TranslatedShadowViewProjectionMatrix = Translation * ShadowInfo->OnePassShadowViewProjectionMatrices[FaceIndex];
			ShadowDepthPassParameters.ShadowViewProjectionMatrices[FaceIndex] = TranslatedShadowViewProjectionMatrix;
			ShadowDepthPassParameters.ShadowViewMatrices[FaceIndex] = ShadowInfo->OnePassShadowViewMatrices[FaceIndex];
		}
	}
}