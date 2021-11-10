bool FDeferredShadingSceneRenderer::RenderBasePass(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, , bool bParallelBasePass, )
{
	FViewInfo& View = Views[i];

	TUniformBufferRef<FOpaqueBasePassUniformParameters> BasePassUniformBuffer;
	CreateOpaqueBasePassUniformBuffer(RHICmdList, View, ForwardScreenSpaceShadowMask, nullptr, nullptr, nullptr, BasePassUniformBuffer);

	const bool bShouldRenderView = View.ShouldRenderView();
	if (bShouldRenderView)
	{
		Scene->UniformBuffers.UpdateViewUniformBuffer(View);
		RenderBasePassView(RHICmdList, View, BasePassDepthStencilAccess, DrawRenderState);
	}

	RenderEditorPrimitives(RHICmdList, View, BasePassDepthStencilAccess, DrawRenderState, bDirty);
	
	if (bShouldRenderView && View.Family->EngineShowFlags.Atmosphere)
	{
		RenderSkyPassView(RHICmdList, View, BasePassDepthStencilAccess_NoDepthWrite, DrawRenderState);
	}
}

bool FDeferredShadingSceneRenderer::RenderBasePassView(
	FRHICommandListImmediate& RHICmdList, 
	FViewInfo& View, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, 
	const FMeshPassProcessorRenderState& InDrawRenderState/*No Need!!*/)
{
	View.ParallelMeshDrawCommandPasses[EMeshPass::BasePass].DispatchDraw(nullptr, RHICmdList);
}



void FBasePassMeshProcessor::AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId)
{
	if (!MeshBatch.bUseForMaterial)
		return;

	const FMaterialRenderProxy* FallbackMaterialRenderProxyPtr = nullptr;
	const FMaterial& Material = MeshBatch.MaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, FallbackMaterialRenderProxyPtr);
	const FMaterialRenderProxy& MaterialRenderProxy = FallbackMaterialRenderProxyPtr ? *FallbackMaterialRenderProxyPtr : *MeshBatch.MaterialRenderProxy;
	const EBlendMode BlendMode = Material.GetBlendMode();
	const FMaterialShadingModelField ShadingModels = Material.GetShadingModels();
	const bool bIsTranslucent = IsTranslucentBlendMode(BlendMode);
	const FMeshDrawingPolicyOverrideSettings OverrideSettings = ComputeMeshOverrideSettings(MeshBatch);
	const ERasterizerFillMode MeshFillMode = ComputeMeshFillMode(MeshBatch, Material, OverrideSettings);
	const ERasterizerCullMode MeshCullMode = ComputeMeshCullMode(MeshBatch, Material, OverrideSettings);

	if (bTranslucentBasePass)
	{
		if (bIsTranslucent && !Material.IsDeferredDecal())
		{
			switch (TranslucencyPassType)
			{
			case ETranslucencyPass::TPT_StandardTranslucency:
				bShouldDraw = !Material.IsTranslucencyAfterDOFEnabled();
			case ETranslucencyPass::TPT_TranslucencyAfterDOF:
				bShouldDraw = Material.IsTranslucencyAfterDOFEnabled();
			case ETranslucencyPass::TPT_TranslucencyAfterDOFModulate:
				bShouldDraw = Material.IsTranslucencyAfterDOFEnabled() && (Material.IsDualBlendingEnabled(GetFeatureLevelShaderPlatform(FeatureLevel)) || BlendMode == BLEND_Modulate);
			case ETranslucencyPass::TPT_AllTranslucency:
				bShouldDraw = true;
			}
		}
	}
	else
	{
		bShouldDraw = !bIsTranslucent;
	}

	bShouldDraw = bShouldDraw
			&& PrimitiveSceneProxy->ShouldRenderInMainPass()
			&& ShouldIncludeDomainInMeshPass(Material.GetMaterialDomain())
			&& ShouldIncludeMaterialInDefaultOpaquePass(Material);

	if(!bShouldDraw)
		return;

	if(ShadingModels.IsUnlit())
		goto UNLIT;

	const FLightMapInteraction LightMapInteraction = MeshBatch.LCI 
		? MeshBatch.LCI->GetLightMapInteraction(FeatureLevel) : FLightMapInteraction();
	const bool bUseVolumetricLightmap = Scene && Scene->VolumetricLightmapSceneData.HasData();

	if (bIsTranslucent
		&& PrimitiveSceneProxy->CastsVolumetricTranslucentShadow())
	{
		...
	}
	else
	{
		const bool bAllowHighQualityLightMaps = TEXT("r.HighQualityLightMaps") != 0 && LightMapInteraction.bAllowHighQualityLightMaps;
		const bool bAllowLowQualityLightMaps = TEXT("r.SupportLowQualityLightmaps") != 0;

		if(LightMapInteraction.GetType() == LMIT_Texture)
		/*  // Do not depend on Mobility!!
		LCI != nullptr
		&& LCI->bGlobalVolumeLightmap == false
		&& LCI->LightMap != nullptr
		&& LCI->LightMap->Textures[0] != nullptr
		&& LCI->LightMap->Textures[0]->Resource != nullptr 
		*/
		{
			if (bAllowHighQualityLightMaps)
			{
				const FShadowMapInteraction ShadowMapInteraction = MeshBatch.LCI
					? MeshBatch.LCI->GetShadowMapInteraction(FeatureLevel) : FShadowMapInteraction();

				if (ShadowMapInteraction.GetType() == SMIT_Texture)// Check!!
				{
					Process< FUniformLightMapPolicy >(,,,,,, BlendMode, ShadingModels,
						FUniformLightMapPolicy(LMP_DISTANCE_FIELD_SHADOWS_AND_HQ_LIGHTMAP),
						MeshBatch.LCI,,);
				}
				else
				{
					Process< FUniformLightMapPolicy >(,,,,,, BlendMode, ShadingModels,
						FUniformLightMapPolicy(LMP_HQ_LIGHTMAP),
						MeshBatch.LCI,,);
				}
			}/*
			else if (bAllowLowQualityLightMaps) {
				Process< FUniformLightMapPolicy >(,,,,,, BlendMode, ShadingModels,
					FUniformLightMapPolicy(LMP_LQ_LIGHTMAP),
					MeshBatch.LCI,,);
			} else { goto UNLIT; }*/
		}
		else
		{
			if (bUseVolumetricLightmap
				&& (PrimitiveSceneProxy->IsMovable()
					|| PrimitiveSceneProxy->NeedsUnbuiltPreviewLighting()
					|| PrimitiveSceneProxy->GetLightmapType() == ELightmapType::ForceVolumetric))
			{
				Process< FUniformLightMapPolicy >(,,,,,, BlendMode, ShadingModels,
					FUniformLightMapPolicy(LMP_PRECOMPUTED_IRRADIANCE_VOLUME_INDIRECT_LIGHTING),
					MeshBatch.LCI,,);
			}
			else
			{
UNLIT:
				Process< FUniformLightMapPolicy >(,,,,,, BlendMode, ShadingModels,
					FUniformLightMapPolicy(LMP_NO_LIGHTMAP),
					MeshBatch.LCI,,);
			}
		}
	}
}


bool FScene::ShouldRenderSkylightInBasePass(EBlendMode BlendMode) const
{
	return SkyLight && !ShouldRenderRayTracingSkyLight(SkyLight)
		&& ((SkyLight->bMovable && IsTranslucentBlendMode(BlendMode)) || SkyLight->bHasStaticLighting)
}

void FBasePassMeshProcessor::Process<FUniformLightMapPolicy> (,,,,,,
	EBlendMode BlendMode,
	FMaterialShadingModelField ShadingModels,
	const FUniformLightMapPolicy& LightMapPolicy,
	const FUniformLightMapPolicy::ElementDataType& LightMapElementData,,)
{
	const FVertexFactory* VertexFactory = MeshBatch.VertexFactory;

	const bool bRenderSkylight = Scene && Scene->ShouldRenderSkylightInBasePass(BlendMode) && ShadingModels.IsLit();
	const bool bRenderAtmosphericFog = IsTranslucentBlendMode(BlendMode) && Scene && Scene->HasAtmosphericFog();

	TMeshProcessorShaders<
		TBasePassVertexShaderPolicyParamType<FUniformLightMapPolicy>,
		FBaseHS, FBaseDS,
		TBasePassPixelShaderPolicyParamType<FUniformLightMapPolicy> > BasePassShaders;
	{
		GetBasePassShaders<FUniformLightMapPolicy>(
			MaterialResource,
			VertexFactory->GetType(),
			LightMapPolicy,
			FeatureLevel,
			bRenderAtmosphericFog,
			bRenderSkylight,
			Get128BitRequirement(),
			BasePassShaders.HullShader,
			BasePassShaders.DomainShader,
			BasePassShaders.VertexShader,
			BasePassShaders.PixelShader
			);
	}

	FMeshPassProcessorRenderState DrawRenderState(PassDrawRenderState);
	// {
	// 	SetDepthStencilStateForBasePass(
	// 		ViewIfDynamicMeshCommand,
	// 		DrawRenderState,
	// 		FeatureLevel,
	// 		MeshBatch,
	// 		StaticMeshId,
	// 		PrimitiveSceneProxy,
	// 		bEnableReceiveDecalOutput);

	// 	if (bTranslucentBasePass)
	// 	{
	// 		SetTranslucentRenderState(DrawRenderState, MaterialResource, GShaderPlatformForFeatureLevel[FeatureLevel], TranslucencyPassType);
	// 	}
	// }

	TBasePassShaderElementData<FUniformLightMapPolicy> ShaderElementData(LightMapElementData);
	{
		ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, true);
	}

	FMeshDrawCommandSortKey SortKey
	// { 
	// 	SortKey = bTranslucentBasePass
	// 		? CalculateTranslucentMeshStaticSortKey(PrimitiveSceneProxy, MeshBatch.MeshIdInPrimitive)
	// 		: CalculateBasePassMeshStaticSortKey(EarlyZPassMode, BlendMode, BasePassShaders.VertexShader, BasePassShaders.PixelShader);
	// }

	BuildMeshDrawCommands(,,,,,
		DrawRenderState,
		BasePassShaders,,,
		SortKey,,
		ShaderElementData);
}

