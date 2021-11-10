
FRHICommandListImmediate& RHICmdList = Context.RHICmdList;
FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
const FViewInfo& View = Context.View;
const FSceneViewFamily& ViewFamily = *(View.Family);
const bool bStencilSizeThreshold = CVarStencilSizeThreshold.GetValueOnRenderThread() >= 0;
FScene& Scene = *(FScene*)ViewFamily.Scene;

void FRCPassPostProcessDeferredDecals::Process(FRenderingCompositePassContext& Context)
{
	if(Scene.Decals.Num()==0) return;

	bool bShouldResolveTargets = false;
	
	FDecalRenderTargetManager RenderTargetManager(RHICmdList, Context.GetShaderPlatform(), CurrentStage);
	
	FTransientDecalRenderDataList SortedDecals;
	FDecalRendering::BuildVisibleDecalList(Scene, View, CurrentStage, &SortedDecals);

	if (SortedDecals.Num() > 0)
	{
		SCOPED_DRAW_EVENTF(RHICmdList, DeferredDecalsInner, TEXT("DeferredDecalsInner %d/%d"), SortedDecals.Num(), Scene.Decals.Num());
		SCOPED_DRAW_EVENT(RHICmdList, Decals);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		EDecalRasterizerState LastDecalRasterizerState = DRS_Undefined;
		FDecalDepthState LastDecalDepthState;
		int32 LastDecalBlendMode = -1;
		int32 LastDecalHasNormal = -1; // Decal state can change based on its normal property.(SM5)
		uint32 StencilRef = 0;
		ERenderTargetMode LastRenderTargetMode = RTM_Unknown;

		for (int32 DecalIndex = 0; DecalIndex < SortedDecals.Num(); DecalIndex++)
		{
			const FTransientDecalRenderData& DecalData = SortedDecals[DecalIndex];
			const FDeferredDecalProxy& DecalProxy = *DecalData.DecalProxy;
			const FMatrix ComponentToWorldMatrix = DecalProxy.ComponentTrans.ToMatrixWithScale();
			const FMatrix FrustumComponentToClip = FDecalRendering::ComputeComponentToClipMatrix(View, ComponentToWorldMatrix);

			EDecalBlendMode DecalBlendMode = DecalData.FinalDecalBlendMode;
			EDecalRenderStage LocalDecalStage = DRS_BeforeLighting;
			bool bStencilThisDecal = true;

			FDecalRenderingCommon::ERenderTargetMode CurrentRenderTargetMode = FDecalRenderingCommon::ComputeRenderTargetMode(View.GetShaderPlatform(), DecalBlendMode, DecalData.bHasNormal);

			// Here we assume that GBuffer can only be WorldNormal since it is the only GBufferTarget handled correctly.
			if (RenderTargetManager.bGufferADirty && DecalData.MaterialResource->NeedsGBuffer())
			{
				RHICmdList.CopyToResolveTarget(SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture, SceneContext.GBufferA->GetRenderTargetItem().TargetableTexture, FResolveParams());
				RenderTargetManager.TargetsToResolve[FDecalRenderTargetManager::GBufferAIndex] = nullptr;
				RenderTargetManager.bGufferADirty = false;
			}

			// fewer rendertarget switches if possible
			if (CurrentRenderTargetMode != LastRenderTargetMode)
			{
				LastRenderTargetMode = CurrentRenderTargetMode;

				RenderTargetManager.SetRenderTargetMode(CurrentRenderTargetMode, DecalData.bHasNormal, );
				Context.SetViewportAndCallRHI(Context.View.ViewRect);
				RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			}

			check(RHICmdList.IsInsideRenderPass());

			bool bThisDecalUsesStencil = false;

			if (bStencilThisDecal && bStencilSizeThreshold)
			{
				// note this is after a SetStreamSource (in if CurrentRenderTargetMode != LastRenderTargetMode) call as it needs to get the VB input
				bThisDecalUsesStencil = RenderPreStencil(Context, ComponentToWorldMatrix, FrustumComponentToClip);

				LastDecalRasterizerState = DRS_Undefined;
				LastDecalDepthState = FDecalDepthState();
				LastDecalBlendMode = -1;
			}

			const bool bBlendStateChange = DecalBlendMode != LastDecalBlendMode;// Has decal mode changed.
			const bool bDecalNormalChanged = GSupportsSeparateRenderTargetBlendState && // has normal changed for SM5 stain/translucent decals?
				(DecalBlendMode == DBM_Translucent || DecalBlendMode == DBM_Stain) &&
				(int32)DecalData.bHasNormal != LastDecalHasNormal;

			// fewer blend state changes if possible
			if (bBlendStateChange || bDecalNormalChanged)
			{
				LastDecalBlendMode = DecalBlendMode;
				LastDecalHasNormal = (int32)DecalData.bHasNormal;

				GraphicsPSOInit.BlendState = GetDecalBlendState(, CurrentStage, (EDecalBlendMode)LastDecalBlendMode, DecalData.bHasNormal);
			}

			// todo
			const float ConservativeRadius = DecalData.ConservativeRadius;
			//			const int32 IsInsideDecal = ((FVector)View.ViewMatrices.ViewOrigin - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f) + ( bThisDecalUsesStencil ) ? 2 : 0;
			const bool bInsideDecal = ((FVector)View.ViewMatrices.GetViewOrigin() - ComponentToWorldMatrix.GetOrigin()).SizeSquared() < FMath::Square(ConservativeRadius * 1.05f + View.NearClippingDistance * 2.0f);
			//			const bool bInsideDecal =  !(IsInsideDecal & 1);

			// update rasterizer state if needed
			{
				bool bReverseHanded = false;
				{
					// Account for the reversal of handedness caused by negative scale on the decal
					const auto& Scale3d = DecalProxy.ComponentTrans.GetScale3D();
					bReverseHanded = Scale3d[0] * Scale3d[1] * Scale3d[2] < 0.f;
				}
				EDecalRasterizerState DecalRasterizerState = FDecalRenderingCommon::ComputeDecalRasterizerState(bInsideDecal, bReverseHanded, View.bReverseCulling);

				if (LastDecalRasterizerState != DecalRasterizerState)
				{
					LastDecalRasterizerState = DecalRasterizerState;
					GraphicsPSOInit.RasterizerState = GetDecalRasterizerState(DecalRasterizerState);
				}
			}

			// update DepthStencil state if needed
			{
				FDecalDepthState DecalDepthState = ComputeDecalDepthState(LocalDecalStage, bInsideDecal, bThisDecalUsesStencil);

				if (LastDecalDepthState != DecalDepthState)
				{
					LastDecalDepthState = DecalDepthState;
					GraphicsPSOInit.DepthStencilState = GetDecalDepthState(StencilRef, DecalDepthState);
				}
			}

			GraphicsPSOInit.PrimitiveType = PT_TriangleList;

			FDecalRendering::SetShader(RHICmdList, GraphicsPSOInit, View, DecalData, CurrentStage, FrustumComponentToClip);
			RHICmdList.SetStencilRef(StencilRef);

			RHICmdList.DrawIndexedPrimitive(GetUnitCubeIndexBuffer(), 0, 0, 8, 0, ARRAY_COUNT(GCubeIndices) / 3, 1);
			RenderTargetManager.bGufferADirty |= (RenderTargetManager.TargetsToResolve[FDecalRenderTargetManager::GBufferAIndex] != nullptr);
		}

		check(RHICmdList.IsInsideRenderPass());
		// Finished rendering sorted decals, so end the renderpass.
		RHICmdList.EndRenderPass();
	}

	if (RHICmdList.IsInsideRenderPass())
	{
		// If the SortedDecals list is empty we may have started a renderpass to clear the dbuffer.
		// If we only draw mesh decals we'll have an active renderpass here as well.
		RHICmdList.EndRenderPass();
	}

	// This stops the targets from being resolved and decoded until the last view is rendered.
	// This is done so as to not run eliminate fast clear on the views before the end.
	bool bLastView = Context.View.Family->Views.Last() == &Context.View;
	if ((Scene.Decals.Num() > 0) && bLastView && CurrentStage == DRS_Emissive)
	{
		// we don't modify stencil but if out input was having stencil for us (after base pass - we need to clear)
		// Clear stencil to 0, which is the assumed default by other passes

		FRHIRenderPassInfo RPInfo;
		RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::DontLoad_DontStore, ERenderTargetActions::Clear_Store);
		RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthTexture();
		RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthNop_StencilWrite;
		RPInfo.DepthStencilRenderTarget.ResolveTarget = nullptr;

		RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearStencil"));
		RHICmdList.EndRenderPass();
	}

	if (bLastView || !GSupportsRenderTargetWriteMask)
	{
		bShouldResolveTargets = true;
	}

	if (bShouldResolveTargets)
	{
		RenderTargetManager.ResolveTargets();
	}

	check(RHICmdList.IsOutsideRenderPass());
}