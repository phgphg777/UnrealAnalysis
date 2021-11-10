
static uint32 GroupId = 0;
int32 GNumCSMCaches = 0;
FAutoConsoleVariableRef CVarNumCSMCaches(
	TEXT("r.Shadow.NumCSMCaches"), 
	GNumCSMCaches, 
	TEXT(""), 
	ECVF_RenderThreadSafe );

int32 GFixedGroupId = -1;
FAutoConsoleVariableRef CVarShadowGroupId(
	TEXT("r.Shadow.GroupId"), 
	GFixedGroupId, 
	TEXT(""), 
	ECVF_RenderThreadSafe );

class FScene {
	TMap< int32, FCachedOneCSMData > CachedCSMData;
}

class FCachedOneCSMData 
{
	TRefCountPtr<IPooledRenderTarget> ComposingTarget;
	TArray< TRefCountPtr<IPooledRenderTarget> > CachedTargets;
	uint32 CurrentIndex = 0;
public:
	float LastUsedTime;
	
	void Release()
	{
		ComposingTarget = nullptr;
		CachedTargets.Empty();
		CurrentIndex = 0;
	}
	void ResizeCaches(uint32 NumCaches, const FPooledRenderTargetDesc& Desc, FRHICommandList& RHICmdList, TGlobalResource<FRenderTargetPool>& RTPool)
	{
		Release();
		CachedTargets.AddDefaulted(NumCaches);
		for (uint32 i = 0; i < NumCaches; ++i)
		{
			RTPool.FindFreeElement(RHICmdList, Desc, CachedTargets[i], TEXT(""));
		}
	}
	void Cycle(TRefCountPtr<IPooledRenderTarget>& RenderTarget)
	{
		ComposingTarget = CachedTargets[CurrentIndex];
		CachedTargets[CurrentIndex] = RenderTarget;
		CurrentIndex = (CurrentIndex + 1) % CachedTargets.Num();
	}
	uint32 NumCaches() { return CachedTargets.Num(); }
	const TRefCountPtr<IPooledRenderTarget>& GetComposingTarget() const { return ComposingTarget; }
	const TArray<TRefCountPtr<IPooledRenderTarget>>& GetCachedTargets() const { return CachedTargets; }
};


void FSceneRenderer::AllocateCSMDepthTargets(FRHICommandListImmediate& RHICmdList, const TArray<FProjectedShadowInfo*, SceneRenderingAllocator>& WholeSceneDirectionalShadows)
{
	...

	if(GNumCSMCaches > 0 && bAllowAtlasing)
	{
		int32 LightId = WholeSceneDirectionalShadows[0]->GetLightSceneInfo().Id;
		FCachedOneCSMData* CachedOneCSMData = Scene->CachedCSMData.Find(LightId);

		if(!CachedOneCSMData)
		{
			CachedOneCSMData = & ( Scene->CachedCSMData.Add(LightId, FCachedOneCSMData()) );
		}

		if (CachedOneCSMData->NumCaches() != GNumCSMCaches)
		{
			FIntPoint AtlasSize(Layouts[0].TextureLayout.GetSizeX(), Layouts[0].TextureLayout.GetSizeY());
			FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(AtlasSize, PF_ShadowDepth, FClearValueBinding::DepthOne, TexCreate_None, TexCreate_DepthStencilTargetable | TexCreate_ShaderResource, false));
			Desc.Flags |= GFastVRamConfig.ShadowCSM;
			
			CachedOneCSMData->ResizeCaches(GNumCSMCaches, Desc, RHICmdList, GRenderTargetPool);
		}

		CachedOneCSMData->Cycle(SortedShadowsForShadowDepthPass.ShadowMapAtlases.Last().RenderTargets.DepthTarget);
		CachedOneCSMData->LastUsedTime = ViewFamily.CurrentRealTime;
	}
}

void FSceneRenderer::AllocateShadowDepthTargets(FRHICommandListImmediate& RHICmdList)
{
	...

	for (TMap<int32, FCachedOneCSMData>::TIterator It(Scene->CachedCSMData); It; ++It)
	{
		FCachedOneCSMData& CachedOneCSMData = It.Value();

		if (ViewFamily.CurrentRealTime - CachedOneCSMData.LastUsedTime > 1.0f)
		{
			CachedOneCSMData.Release();
		}
	}	
}

void FSceneRenderer::RenderShadowDepthMapAtlases(FRHICommandListImmediate& RHICmdList)
{
	for (const FSortedShadowMapAtlas& ShadowMapAtlas : SortedShadowsForShadowDepthPass.ShadowMapAtlases)
	{
		...

		extern int32 GNumCSMCaches;
		if(GNumCSMCaches > 0 && !GRHINeedsUnatlasedCSMDepthsWorkaround
			&& ShadowMapAtlas.Shadows.Num() > 0
			&& ShadowMapAtlas.Shadows[0]->IsWholeSceneDirectionalShadow())
		{
			const FCachedOneCSMData& CachedOneCSMData = Scene->CachedCSMData.FindChecked(
				ShadowMapAtlas.Shadows[0]->GetLightSceneInfo().Id);
			const TRefCountPtr<IPooledRenderTarget>& ComposingTarget = CachedOneCSMData.GetComposingTarget();
			
			FRHIRenderPassInfo RPInfo(
				ComposingTarget->GetRenderTargetItem().TargetableTexture,
				EDepthStencilTargetActions::LoadDepthStencil_StoreDepthStencil, 
				nullptr, 
				FExclusiveDepthStencil::DepthWrite_StencilWrite);

			RHICmdList.BeginRenderPass(RPInfo, TEXT(""));
			{
				for (FProjectedShadowInfo* ProjectedShadowInfo : ShadowMapAtlas.Shadows)
				{
					ProjectedShadowInfo->RenderTargets.DepthTarget = ComposingTarget.GetReference();
					ProjectedShadowInfo->CopyCachedCSM(RHICmdList, CachedOneCSMData);
				}
			}
			RHICmdList.EndRenderPass();
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ComposingTarget->GetRenderTargetItem().TargetableTexture);
		}
	}
}

// ShadowRendering.h
class FCachedOneCSMData;

class FProjectedShadowInfo : public FRefCountedObject {
	void CopyCachedCSM(FRHICommandList& RHICmdList, const FCachedOneCSMData& CachedOneCSMData);
}

void FProjectedShadowInfo::CopyCachedCSM(
	FRHICommandList& RHICmdList, 
	const FCachedOneCSMData& CachedOneCSMData)
{
	TShaderMapRef<FScreenVS> VertexShader(ShadowDepthView->ShaderMap); // Why need view dependency?
	TShaderMapRef<FCopyShadowMaps2DPS> PixelShader(ShadowDepthView->ShaderMap);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_LessEqual>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	}
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);	

	RHICmdList.SetViewport(
		X + BorderSize, Y + BorderSize, 0.0f,
		X + BorderSize + ResolutionX, Y + BorderSize + ResolutionY, 1.0f);

	for (const TRefCountPtr<IPooledRenderTarget>& CachedTarget : CachedOneCSMData.GetCachedTargets())
	{
		check(CachedTarget.GetReference() != RenderTargets.DepthTarget);

		if (CachedTarget->GetDesc().Extent == RenderTargets.DepthTarget->GetDesc().Extent)
		{
			PixelShader->SetParameters(RHICmdList, *ShadowDepthView, CachedTarget.GetReference());
			DrawRectangle(
				RHICmdList,
				0, 0, ResolutionX, ResolutionY,
				X + BorderSize, Y + BorderSize, ResolutionX, ResolutionY,
				FIntPoint(ResolutionX, ResolutionY),
				CachedTarget->GetDesc().Extent,
				VertexShader);
		}
	}
}

void FGatherShadowPrimitivesPacket::FilterPrimitiveForShadows(const FBoxSphereBounds& PrimitiveBounds, FPrimitiveFlagsCompact PrimitiveFlagsCompact, FPrimitiveSceneInfo* PrimitiveSceneInfo, FPrimitiveSceneProxy* PrimitiveProxy)
{
	if (GNumCSMCaches > 0 && !GRHINeedsUnatlasedCSMDepthsWorkaround)
	{
		uint32 SelectedGroupId = PrimitiveSceneInfo->GetIndex() % (GNumCSMCaches + 1);

		if (GFixedGroupId != -1 && SelectedGroupId != GFixedGroupId
			|| SelectedGroupId != GroupId)
		{
			return;
		}
	}
}

void FSceneRenderer::InitDynamicShadows(FRHICommandListImmediate& RHICmdList, FGlobalDynamicIndexBuffer& DynamicIndexBuffer, FGlobalDynamicVertexBuffer& DynamicVertexBuffer, FGlobalDynamicReadBuffer& DynamicReadBuffer)
{
	...
	
	if (GNumCSMCaches > 0 && !GRHINeedsUnatlasedCSMDepthsWorkaround)
	{
		GroupId = (GroupId + 1) % (GNumCSMCaches + 1);
	}
}