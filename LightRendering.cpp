class ENGINE_API ULightComponent;
class ENGINE_API FLightSceneProxy;
//class FLightSceneInfo;
//class FLightSceneInfoCompact;
class FVisibleLightViewInfo;
class FVisibleLightInfo;
struct FSortedLightSceneInfo;
struct FSortedLightSetSceneInfo;


////////////////////////////////////////////////////////////////////////////////////////////////
void FSceneRenderer::ComputeViewVisibility()
{
	VisibleLightInfos.AddZeroed(Scene->Lights.GetMaxIndex());
	
	for(int32 LightIndex = 0;LightIndex < Scene->Lights.GetMaxIndex();LightIndex++)
	{
		new(View.VisibleLightInfos) FVisibleLightViewInfo();
	}
}

void FSceneRenderer::PostVisibilityFrameSetup(FILCUpdatePrimTaskData& OutILCTaskData)
{
	for(TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights);LightIt;++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		// view frustum cull lights in each view
		for(int32 ViewIndex = 0;ViewIndex < Views.Num();ViewIndex++)
		{		
			const FLightSceneProxy* Proxy = LightSceneInfo->Proxy;
			FViewInfo& View = Views[ViewIndex];
			FVisibleLightViewInfo& VisibleLightViewInfo = View.VisibleLightInfos[LightIt.GetIndex()];
			
			if(Proxy->GetLightType() != LightType_Directional)
			{
				VisibleLightViewInfo.bInViewFrustum = true;
			}
			else
			{
				FSphere const& BoundingSphere = Proxy->GetBoundingSphere();
				if (View.ViewFrustum.IntersectSphere(BoundingSphere.Center, BoundingSphere.W))
				{
					FSphere Bounds = Proxy->GetBoundingSphere();
					float DistanceSquared = (Bounds.Center - View.ViewMatrices.GetViewOrigin()).SizeSquared();
					float MaxDistSquared = Proxy->GetMaxDrawDistance() * Proxy->GetMaxDrawDistance() * GLightMaxDrawDistanceScale * GLightMaxDrawDistanceScale;
					const bool bDrawLight = (FMath::Square(FMath::Min(0.0002f, GMinScreenRadiusForLights / Bounds.W) * View.LODDistanceFactor) * DistanceSquared < 1.0f)
												&& (MaxDistSquared == 0 || DistanceSquared < MaxDistSquared);
						
					VisibleLightViewInfo.bInViewFrustum = bDrawLight;	
				}
			}		
		}
	}
}

void FDeferredShadingSceneRenderer::InitViewsPossiblyAfterPrepass(FRHICommandListImmediate& RHICmdList, struct FILCUpdatePrimTaskData& ILCTaskData, FGraphEventArray& UpdateViewCustomDataEvents)
{
	// Setup dynamic shadows.
	InitDynamicShadows(RHICmdList, DynamicIndexBufferForInitShadows, DynamicVertexBufferForInitShadows, DynamicReadBufferForInitShadows);
}
////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////
class FSimpleLightArray {
	TArray<FSimpleLightEntry> InstanceData;/** Data per simple dynamic light instance, independent of view */
}

struct FSortedLightSetSceneInfo {
	int SimpleLightsEnd;
	int TiledSupportedEnd;
	int ClusteredSupportedEnd;
	int AttenuationLightStart;/** First light with shadow map or */

	FSimpleLightArray SimpleLights;
	TArray<FSortedLightSceneInfo> SortedLights;
};

struct FSortedLightSceneInfo
{
	union {
		struct {
			uint32 LightType : 2;	// (LightType_Directional=0, LightType_Point=1, LightType_Spot=2, LightType_Rect=3)			
			uint32 bTextureProfile : 1;
			uint32 bLightFunction : 1;
			uint32 bShadowed : 1;			
			uint32 bUsesLightingChannels : 1;

			uint32 bIsNotSimpleLight : 1;
			uint32 bTiledDeferredNotSupported : 1;
			uint32 bClusteredDeferredNotSupported : 1;
		} Fields;

		int32 Packed = 0;
	} SortKey;

	const FLightSceneInfo* LightSceneInfo;
	int32 SimpleLightIndex;
};

bool FLightSceneInfo::ShouldRenderLightViewIndependent() const
{	
	// return FALSE for vaild static light
	return !Proxy->GetColor().IsAlmostBlack()
		&& !(Proxy->HasStaticLighting() && IsPrecomputedLightingValid());
}

bool FLightSceneInfo::ShouldRenderLight(const FViewInfo& View) const
{
	return bVisible ? View.VisibleLightInfos[Id].bInViewFrustum : true;
}

FPointLightSceneProxy::FPointLightSceneProxy(const UPointLightComponent* Component)
{
	// tiled deferred only supported for point/spot lights with 0 length
	bTiledDeferredLightingSupported = (SourceLength == 0.0f);
}

bool FSceneRenderer::CheckForProjectedShadows( const FLightSceneInfo* LightSceneInfo ) const
{
	const TArray<FProjectedShadowInfo*>& AllProjectedShadows = this->VisibleLightInfos[LightSceneInfo->Id].AllProjectedShadows;
	const FSceneBitArray& ProjectedShadowVisibilityMap = Views[0].VisibleLightInfos[LightSceneInfo->Id].ProjectedShadowVisibilityMap;
	
	for(const FProjectedShadowInfo* ProjectedShadowInfo : AllProjectedShadows)
	{
		if(ProjectedShadowVisibilityMap[ProjectedShadowInfo->ShadowId])
		{
			return true;
		}
	}

	return false;
}

void FDeferredShadingSceneRenderer::GatherAndSortLights(FSortedLightSetSceneInfo& OutSortedLights)
{
	if (bAllowSimpleLights)
	{
		GatherSimpleLights(ViewFamily, Views, OutSortedLights.SimpleLights);
	}
	const auto& SimpleLights = OutSortedLights.SimpleLights;
	
	auto& SortedLights = OutSortedLights.SortedLights;
	SortedLights.Empty(Scene->Lights.Num() + SimpleLights.InstanceData.Num());// NOTE: we allocate space also for simple lights such that they can be referenced in the same sorted range

	// Build a list of visible lights.
	for (TSparseArray<FLightSceneInfoCompact>::TConstIterator LightIt(Scene->Lights); LightIt; ++LightIt)
	{
		const FLightSceneInfoCompact& LightSceneInfoCompact = *LightIt;
		const FLightSceneInfo* const LightSceneInfo = LightSceneInfoCompact.LightSceneInfo;

		if (LightSceneInfo->ShouldRenderLightViewIndependent())
		{
			for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ViewIndex++)
			{
				if (LightSceneInfo->ShouldRenderLight(Views[ViewIndex]))
				{
					FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo();
					SortedLightInfo->LightSceneInfo = LightSceneInfo;
					SortedLightInfo->SimpleLightIndex = -1;

					SortedLightInfo->SortKey.Fields.LightType = LightSceneInfoCompact.LightType;
					SortedLightInfo->SortKey.Fields.bTextureProfile = LightSceneInfo->Proxy->GetIESTextureResource();
					SortedLightInfo->SortKey.Fields.bLightFunction = CheckForLightFunction(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bShadowed = CheckForProjectedShadows(LightSceneInfo);
					SortedLightInfo->SortKey.Fields.bUsesLightingChannels = Views[ViewIndex].bUsesLightingChannels && LightSceneInfo->Proxy->GetLightingChannelMask() != GetDefaultLightingChannelMask();

					const bool bTiledOrClusteredDeferredSupported =
						!SortedLightInfo->SortKey.Fields.bTextureProfile &&
						!SortedLightInfo->SortKey.Fields.bShadowed &&
						!SortedLightInfo->SortKey.Fields.bLightFunction &&
						!SortedLightInfo->SortKey.Fields.bUsesLightingChannels
						&& LightSceneInfoCompact.LightType != LightType_Directional
						&& LightSceneInfoCompact.LightType != LightType_Rect;

					SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 1;
					SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = !(bTiledOrClusteredDeferredSupported && LightSceneInfo->Proxy->bTiledDeferredLightingSupported);
					SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = !bTiledOrClusteredDeferredSupported;
					
					break;
				}
			}
		}
	}

	// Add the simple lights also
	for (int32 SimpleLightIndex = 0; SimpleLightIndex < SimpleLights.InstanceData.Num(); SimpleLightIndex++)
	{
		FSortedLightSceneInfo* SortedLightInfo = new(SortedLights) FSortedLightSceneInfo();
		SortedLightInfo->LightSceneInfo = nullptr;
		SortedLightInfo->SimpleLightIndex = SimpleLightIndex;

		SortedLightInfo->SortKey.Fields.LightType = LightType_Point;
		SortedLightInfo->SortKey.Fields.bTextureProfile = 0;
		SortedLightInfo->SortKey.Fields.bLightFunction = 0;
		SortedLightInfo->SortKey.Fields.bShadowed = 0;
		SortedLightInfo->SortKey.Fields.bUsesLightingChannels = 0;

		SortedLightInfo->SortKey.Fields.bIsNotSimpleLight = 0;
		SortedLightInfo->SortKey.Fields.bTiledDeferredNotSupported = 0;
		SortedLightInfo->SortKey.Fields.bClusteredDeferredNotSupported = 0;
	}

	/* Note: 
	(6th bit) bIsNotSimpleLight == 0 => 
	(7th bit) bTiledDeferredNotSupported == 0 => 
	(8th bit) bClusteredDeferredNotSupported == 0 => 
	(2~5th bit) bTextureProfile == bLightFunction == bShadowed == bUsesLightingChannels == 0
	*/
	
	SortedLights.Sort([](const FSortedLightSceneInfo& A, const FSortedLightSceneInfo& B){return A.SortKey.Packed < B.SortKey.Packed;});

	int32 LightIndex = 0;	
	for (; LightIndex < SortedLights.Num(); LightIndex++)
	{
		if (SortedLights[LightIndex].SortKey.Fields.bIsNotSimpleLight)
			break;			
	}
	OutSortedLights.SimpleLightsEnd = LightIndex;

	for (; LightIndex < SortedLights.Num(); LightIndex++)
	{
		if (SortedLights[LightIndex].SortKey.Fields.bTiledDeferredNotSupported)
			break;			
	}
	OutSortedLights.TiledSupportedEnd = LightIndex;

	for (; LightIndex < SortedLights.Num(); LightIndex++)
	{
		if (SortedLights[LightIndex].SortKey.Fields.bClusteredDeferredNotSupported)
			break;			
	}
	OutSortedLights.ClusteredSupportedEnd = LightIndex;

	for (; LightIndex < SortedLights.Num(); LightIndex++)
	{
		if (SortedLights[LightIndex].SortKey.Fields.bShadowed
			|| SortedLights[LightIndex].SortKey.Fields.bLightFunction
			|| SortedLights[LightIndex].SortKey.Fields.bUsesLightingChannels)
			break;			
	}
	OutSortedLights.AttenuationLightStart = LightIndex;
}
////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////
void FDeferredShadingSceneRenderer::RenderLights(
	FRHICommandListImmediate& RHICmdList, 
	FSortedLightSetSceneInfo &SortedLightSet, 
	const FHairStrandsDatas* HairDatas)
{
	bool bStencilBufferDirty = false;	// The stencil buffer should've been cleared to 0 already
	
	const FSimpleLightArray &SimpleLights = SortedLightSet.SimpleLights;
	const TArray<FSortedLightSceneInfo>& SortedLights = SortedLightSet.SortedLights;
	const int32 AttenuationLightStart = SortedLightSet.AttenuationLightStart;

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);

	bool bRenderSimpleLightsStandardDeferred = SortedLightSet.SimpleLights.InstanceData.Num() > 0;
	int32 StandardDeferredStart = SortedLightSet.SimpleLightsEnd;

	if (TEXT("r.UseClusteredDeferredShading") && AreClusteredLightsInLightGrid())
	{
		StandardDeferredStart = SortedLightSet.ClusteredSupportedEnd;
		bRenderSimpleLightsStandardDeferred = false;
		AddClusteredDeferredShadingPass(...);
	}
	else if (TEXT("r.TiledDeferredShading") && SortedLightSet.TiledSupportedEnd >= TEXT("r.TiledDeferredShading.MinimumCount"))
	{
		StandardDeferredStart = SortedLightSet.TiledSupportedEnd;
		bRenderSimpleLightsStandardDeferred = false;
		RenderTiledDeferredLighting(...);
	}

	if (bRenderSimpleLightsStandardDeferred)
	{
		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite);
		RenderSimpleLightsStandardDeferred(...);
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}

	SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
	{
		for (int32 LightIndex = StandardDeferredStart; LightIndex < AttenuationLightStart; LightIndex++)
		{
			RenderLight(RHICmdList, SortedLights[LightIndex].LightSceneInfo, nullptr, nullptr, , );
		}
	}
	SceneContext.FinishRenderingSceneColor(RHICmdList);

	if (TEXT("r.TranslucentLightingVolume") && GSupportsVolumeTextureRendering)
	{
		...
	}


	TRefCountPtr<IPooledRenderTarget> ScreenShadowMaskTexture;
	FPooledRenderTargetDesc Desc(FPooledRenderTargetDesc::Create2DDesc(GetBufferSizeXY(), PF_B8G8R8A8, FClearValueBinding::White, TexCreate_None, TexCreate_RenderTargetable | TexCreate_ShaderResource, false));
	GRenderTargetPool.FindFreeElement(RHICmdList, Desc, ScreenShadowMaskTexture, TEXT("ScreenShadowMaskTexture"));
	
	for (int32 LightIndex = AttenuationLightStart; LightIndex < SortedLights.Num(); LightIndex++)
	{
		const FSortedLightSceneInfo& SortedLightInfo = SortedLights[LightIndex];
		const FLightSceneInfo* LightSceneInfo = SortedLightInfo.LightSceneInfo;
		const bool bDrawShadows = SortedLightInfo.SortKey.Fields.bShadowed;
		const bool bDrawLightFunction = SortedLightInfo.SortKey.Fields.bLightFunction;
		const bool bUsedShadowMaskTexture = bDrawShadows | bDrawLightFunction;
		bool bInjectedTranslucentVolume = false;

		if (bDrawShadows)
		{	
			if (OcclusionType == FOcclusionType::Shadowmap)
			{
				assume(TEXT("r.AllowClearLightSceneExtentsOnly") == 0);
				FRHIRenderPassInfo RPInfo(ScreenShadowMaskTexture->GetRenderTargetItem().TargetableTexture, ERenderTargetActions::Clear_Store);//Clear_DontStore
				RPInfo.DepthStencilRenderTarget.DepthStencilTarget = SceneContext.GetSceneDepthSurface();
				RPInfo.DepthStencilRenderTarget.Action = MakeDepthStencilTargetActions(ERenderTargetActions::Load_DontStore, ERenderTargetActions::Load_Store);//Load_Store, Load_Store
				RPInfo.DepthStencilRenderTarget.ExclusiveDepthStencil = FExclusiveDepthStencil::DepthRead_StencilWrite;

				TransitionRenderPassTargets(RHICmdList, RPInfo);
				RHICmdList.BeginRenderPass(RPInfo, TEXT("ClearScreenShadowMask"));
				RHICmdList.EndRenderPass();
			
				RenderShadowProjections(RHICmdList, LightSceneInfo, ScreenShadowMaskTexture, , HairDatas, bInjectedTranslucentVolume);
			}
		}
		
		if (bDrawLightFunction)
		{
			RenderLightFunction(RHICmdList, LightSceneInfo, ScreenShadowMaskTexture, bDrawShadows, false);			
		}

		if (bUsedShadowMaskTexture)
		{
			RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture);
		}

		if(!bInjectedTranslucentVolume)
		{
			InjectTranslucentVolumeLighting(RHICmdList, *LightSceneInfo, NULL, Views[0], 0);
		}

		SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
		{
			RenderLight(
				RHICmdList, 
				LightSceneInfo, 
				bUsedShadowMaskTexture ? ScreenShadowMaskTexture : nullptr, 
				nullptr, , );
		}
		SceneContext.FinishRenderingSceneColor(RHICmdList);
	}
}

void FDeferredShadingSceneRenderer::RenderLight(
	FRHICommandList& RHICmdList, 
	const FLightSceneInfo* LightSceneInfo, 
	IPooledRenderTarget* ScreenShadowMaskTexture, 
	const FHairStrandsVisibilityViews* InHairVisibilityViews, , )
{
	FViewInfo& View = Views[0];
	if (!LightSceneInfo->ShouldRenderLight(View))
		return;

	const bool bTransmission = LightSceneInfo->Proxy->Transmission();
	const bool bDirectional = LightSceneInfo->Proxy->GetLightType() == LightType_Directional;
	const bool bRect = LightSceneInfo->Proxy->GetLightType() == LightType_Rect;
	const bool bAtmosphereTransmittance = bDirectional && LightSceneInfo->Proxy->IsUsedAsAtmosphereSunLight() && ShouldApplyAtmosphereLightPerPixelTransmittance(Scene, View.Family->EngineShowFlags);
	const bool bSourceTexture = bRect && LightSceneInfo->Proxy->HasSourceTexture();
	const bool bUseIESTexture = !bDirectional && LightSceneInfo->Proxy->GetIESTextureResource() != 0;
	const bool bInverseSquared = !bDirectional && LightSceneInfo->Proxy->IsInverseSquared();
	assume(TEXT("r.AllowDepthBoundsTest") == 0);

	FDeferredLightPS::FPermutationDomain PermutationVector;
	{
		PermutationVector.Set< FDeferredLightPS::FVisualizeCullingDim >( false );
		PermutationVector.Set< FDeferredLightPS::FLightingChannelsDim >( View.bUsesLightingChannels );
		PermutationVector.Set< FDeferredLightPS::FTransmissionDim >( bTransmission );
		PermutationVector.Set< FDeferredLightPS::FHairLighting >( bHairLighting ? 1 : 0 );

		PermutationVector.Set< FDeferredLightPS::FSourceShapeDim >( 
			bDirectional ? ELightSourceShape::Directional : 
			bRect ? ELightSourceShape::Rect : ELightSourceShape::Capsule );
		PermutationVector.Set< FDeferredLightPS::FInverseSquaredDim >( bInverseSquared );
		PermutationVector.Set< FDeferredLightPS::FSourceTextureDim >( bSourceTexture );
		PermutationVector.Set< FDeferredLightPS::FIESProfileDim >( bUseIESTexture );
		PermutationVector.Set< FDeferredLightPS::FAtmosphereTransmittance >( bAtmosphereTransmittance );
	}
	TShaderMapRef< FDeferredLightPS > PixelShader( View.ShaderMap, PermutationVector );

	RHICmdList.SetViewport(View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Max.X, View.ViewRect.Max.Y);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
	GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA, 
		BO_Add, BF_One, BF_One, 
		BO_Add, BF_One, BF_One>::GetRHI();
	GraphicsPSOInit.PrimitiveType = PT_TriangleList;
	GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

	if (bDirectional)
	{
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		
		TShaderMapRef< TDeferredLightVS<false> > VertexShader(View.ShaderMap);	
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);
		PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, nullptr);

		DrawRectangle(
			RHICmdList,
			0, 0, View.ViewRect.Width(), View.ViewRect.Height(),
			View.ViewRect.Min.X, View.ViewRect.Min.Y, View.ViewRect.Width(), View.ViewRect.Height(),
			View.ViewRect.Size(),
			FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
			VertexShader,
			EDRF_UseTriangleOptimization);
	}
	else
	{
		assume(!View.bReverseCulling);
		const FSphere LightBounds = LightSceneInfo->Proxy->GetBoundingSphere();
		const bool bCameraInsideLightGeometry = (View.ViewMatrices.GetViewOrigin() - LightBounds.Center).Size() < LightBounds.W * 1.05f + View.NearClippingDistance * 2.0f
		if(bCameraInsideLightGeometry)
		{
			TStaticRasterizerState<FM_Solid, CM_CCW>::GetRHI(); // Draw Backface
			TStaticDepthStencilState<false, CF_Always>::GetRHI();
		}
		else
		{
			TStaticRasterizerState<FM_Solid, CM_CW>::GetRHI();
			TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI();
		}
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GetVertexDeclarationFVector4();
		
		TShaderMapRef<TDeferredLightVS<true> > VertexShader(View.ShaderMap);
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);
		VertexShader->SetParameters(RHICmdList, View, LightSceneInfo);
		PixelShader->SetParameters(RHICmdList, View, LightSceneInfo, ScreenShadowMaskTexture, nullptr);

		if(LightSceneInfo->Proxy->GetLightType() == LightType_Spot)
		{
			RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilConeVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(
				StencilingGeometry::GStencilConeIndexBuffer.IndexBufferRHI, 0, 0,
				StencilingGeometry::GStencilConeVertexBuffer.GetVertexCount(), 0, 
				StencilingGeometry::GStencilConeIndexBuffer.GetIndexCount() / 3, 1);
		}
		else
		{
			RHICmdList.SetStreamSource(0, StencilingGeometry::GStencilSphereVertexBuffer.VertexBufferRHI, 0);
			RHICmdList.DrawIndexedPrimitive(
				StencilingGeometry::GStencilSphereIndexBuffer.IndexBufferRHI, 0, 0,
				StencilingGeometry::GStencilSphereVertexBuffer.GetVertexCount(), 0,
				StencilingGeometry::GStencilSphereIndexBuffer.GetIndexCount() / 3, 1);
		}
	}
}

template<bool bRadialLight>
class TDeferredLightVS : public FGlobalShader {
	LAYOUT_FIELD(FShaderParameter, StencilGeometryPosAndScale)
	LAYOUT_FIELD(FShaderParameter, StencilConeParameters)
	LAYOUT_FIELD(FShaderParameter, StencilConeTransform)
	LAYOUT_FIELD(FShaderParameter, StencilPreViewTranslation)

	void SetParameters(FRHICommandList& RHICmdList, const FViewInfo& View, const FLightSceneInfo* LightSceneInfo)
	{
		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
		
		if( LightSceneInfo->Proxy->GetLightType() == LightType_Point ||
			LightSceneInfo->Proxy->GetLightType() == LightType_Rect )
		{
			StencilGeometryPosAndScale = FVector4(
				LightSceneInfo->Proxy->GetBoundingSphere().Center + View.ViewMatrices.GetPreViewTranslation(),
				LightSceneInfo->Proxy->GetBoundingSphere().W + alpha);
			StencilConeParameters = FVector4(0.0f);
		}
		else if(LightSceneInfo->Proxy->GetLightType() == LightType_Spot)
		{
			StencilConeTransform = LightSceneInfo->Proxy->GetLightToWorld();
			StencilPreViewTranslation = View.ViewMatrices.GetPreViewTranslation();
			StencilConeParameters = FVector4(
				StencilingGeometry::FStencilConeIndexBuffer::NumSides,
				StencilingGeometry::FStencilConeIndexBuffer::NumSlices,
				LightSceneInfo->Proxy->GetOuterConeAngle(),
				LightSceneInfo->Proxy->GetRadius() );
		}
	}
}
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<false>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("DirectionalVertexMain"),SF_Vertex);
IMPLEMENT_SHADER_TYPE(template<>,TDeferredLightVS<true>,TEXT("/Engine/Private/DeferredLightVertexShaders.usf"),TEXT("RadialVertexMain"),SF_Vertex);

class FDeferredLightPS : public FGlobalShader {
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightAttenuationTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCMatSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LTCAmpSampler);
	LAYOUT_FIELD(FShaderResourceParameter, IESTexture);
	LAYOUT_FIELD(FShaderResourceParameter, IESTextureSampler);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsTexture);
	LAYOUT_FIELD(FShaderResourceParameter, LightingChannelsSampler);
}
IMPLEMENT_GLOBAL_SHADER(FDeferredLightPS, "/Engine/Private/DeferredLightPixelShaders.usf", "DeferredLightPixelMain", SF_Pixel);

void FDeferredLightPS::SetParameters(
	FRHICommandList& RHICmdList, 
	const FSceneView& View, 
	const FLightSceneInfo* LightSceneInfo, 
	IPooledRenderTarget* ScreenShadowMaskTexture, 
	FRenderLightParams* RenderLightParams)
{
	FSceneRenderTargets& SceneRenderTargets = FSceneRenderTargets::Get(RHICmdList);
	const FTextureRHIRef& WhiteDummy = GSystemTextures.WhiteDummy->GetRenderTargetItem().TargetableTexture;
	FRHIPixelShader* ShaderRHI = RHICmdList.GetBoundPixelShader();

	FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, ShaderRHI,View.ViewUniformBuffer);
	SceneTextureParameters.Set(RHICmdList, ShaderRHI, View.FeatureLevel, ESceneTextureSetupMode::All);

	LightAttenuationTexture = ScreenShadowMaskTexture->GetRenderTargetItem().ShaderResourceTexture or GWhiteTexture->TextureRHI;
	LightAttenuationTextureSampler = TStaticSamplerState<SF_Point, AM_Wrap>::GetRHI();

	IESTexture = LightSceneInfo->Proxy->GetIESTextureResource()->TextureRHI or WhiteDummy;
	IESTextureSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp>::GetRHI();

	LightingChannelsTexture = SceneRenderTargets.LightingChannels->GetRenderTargetItem().ShaderResourceTexture or WhiteDummy;
	LightingChannelsSampler = TStaticSamplerState<SF_Point, AM_Clamp>::GetRHI();
	
	LTCMatTexture = GSystemTextures.LTCMat->GetRenderTargetItem().ShaderResourceTexture;
	LTCMatSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp>::GetRHI();
	
	LTCAmpTexture = GSystemTextures.LTCAmp->GetRenderTargetItem().ShaderResourceTexture;
	LTCAmpSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp>::GetRHI();

	...

	const TShaderUniformBufferParameter<FDeferredLightUniformStruct>& 
		ShaderParameter = GetUniformBufferParameter<FDeferredLightUniformStruct>();
	FDeferredLightUniformStruct 
		UniformsValue = GetDeferredLightParameters(RHICmdList, ShaderRHI, LightSceneInfo, View);
	SetUniformBufferParameterImmediate(RHICmdList, ShaderRHI, ShaderParameter, UniformsValue);
}

BEGIN_SHADER_PARAMETER_STRUCT(FLightShaderParameters, ENGINE_API)
	SHADER_PARAMETER(FVector, Position)// Position of the light in the world space.
	SHADER_PARAMETER(float, InvRadius)// 1 / light's falloff radius from Position.
	SHADER_PARAMETER(FVector, Color)// Color of the light.
	SHADER_PARAMETER(float, FalloffExponent)// The exponent for the falloff of the light intensity from the distance.
	SHADER_PARAMETER(FVector, Direction)// Direction of the light if applies.
	SHADER_PARAMETER(float, SpecularScale)// Factor to applies on the specular.
	SHADER_PARAMETER(FVector, Tangent)// One tangent of the light if applies.
	SHADER_PARAMETER(float, SourceRadius)// Radius of the point light.
	SHADER_PARAMETER(FVector2D, SpotAngles)// Dimensions of the light, for spot light, but also
	SHADER_PARAMETER(float, SoftSourceRadius)// Radius of the soft source.
	SHADER_PARAMETER(float, SourceLength)// Other dimensions of the light source for rect light specifically.
	SHADER_PARAMETER(float, RectLightBarnCosAngle)// Barn door angle for rect light
	SHADER_PARAMETER(float, RectLightBarnLength)// Barn door length for rect light	
	SHADER_PARAMETER_TEXTURE(Texture2D, SourceTexture)// Texture of the rect light.
END_SHADER_PARAMETER_STRUCT()

BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredLightUniformStruct,)
	SHADER_PARAMETER(FVector4, ShadowMapChannelMask)
	SHADER_PARAMETER(FVector2D, DistanceFadeMAD)
	SHADER_PARAMETER(float, ContactShadowLength)
	SHADER_PARAMETER(float, VolumetricScatteringIntensity)
	SHADER_PARAMETER(uint32, ShadowedBits)
	SHADER_PARAMETER(uint32, LightingChannelMask)
	SHADER_PARAMETER_STRUCT_INCLUDE(FLightShaderParameters, LightParameters)
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDeferredLightUniformStruct, "DeferredLightUniforms");

template<typename ShaderRHIParamRef>
FDeferredLightUniformStruct GetDeferredLightParameters(
	FRHICommandList& RHICmdList, 
	const FLightSceneInfo* LightSceneInfo,
	const FSceneView& View)
{
	FDeferredLightUniformStruct DeferredLightUniformsValue;

	int32 ShadowMapChannel = LightSceneInfo->Proxy->GetShadowMapChannel();
	DeferredLightUniformsValue.ShadowMapChannelMask = FVector4(
		ShadowMapChannel == 0, ShadowMapChannel == 1, ShadowMapChannel == 2, ShadowMapChannel == 3);
	DeferredLightUniformsValue.DistanceFadeMAD = ...;
	DeferredLightUniformsValue.ContactShadowLength = ...;
	DeferredLightUniformsValue.VolumetricScatteringIntensity = LightSceneInfo->Proxy->GetVolumetricScatteringIntensity();
	DeferredLightUniformsValue.ShadowedBits = 
		LightSceneInfo->Proxy->CastsDynamicShadow() ? 3 : 
		LightSceneInfo->Proxy->CastsStaticShadow() || LightSceneInfo->Proxy->GetLightFunctionMaterial() ? 1 : 0;
	DeferredLightUniformsValue.LightingChannelMask = LightSceneInfo->Proxy->GetLightingChannelMask();

	LightSceneInfo->Proxy->GetLightShaderParameters(DeferredLightUniformsValue.LightParameters);
	if (LightSceneInfo->Proxy->GetLightType() != LightType_Directional)
	{
		DeferredLightUniformsValue.LightParameters.Color *= GetLightFadeFactor(View, LightSceneInfo->Proxy);
	}
	
	return DeferredLightUniformsValue;
}

void FSpotLightSceneProxy::GetLightShaderParameters(FLightShaderParameters& LightParameters) const
{
	FPointLightSceneProxy::GetLightShaderParameters(LightParameters);
	LightParameters.SpotAngles = FVector2D(CosOuterCone, 1.0f / (CosInnerCone - CosOuterCone));
}

void FPointLightSceneProxy::GetLightShaderParameters(FLightShaderParameters& LightParameters) const
{
	LightParameters.FalloffExponent = FalloffExponent;
	LightParameters.SpecularScale = SpecularScale;

	LightParameters.Color = FVector(GetColor());
	LightParameters.Position = GetOrigin();
	LightParameters.Direction = -GetDirection();
	LightParameters.Tangent = FVector(WorldToLight.M[0][2], WorldToLight.M[1][2], WorldToLight.M[2][2]);
	
	LightParameters.InvRadius = InvRadius;
	LightParameters.SourceRadius = SourceRadius;
	LightParameters.SoftSourceRadius = SoftSourceRadius;
	LightParameters.SourceLength = SourceLength;
	
	LightParameters.SpotAngles = FVector2D(-2.0f, 1.0f);
	LightParameters.SourceTexture = GWhiteTexture->TextureRHI;
	LightParameters.RectLightBarnCosAngle = 0.0f;
	LightParameters.RectLightBarnLength = -2.0f;
}

void FRectLightSceneProxy::GetLightShaderParameters(FLightShaderParameters& LightParameters) const
{
	LightParameters.FalloffExponent = 0.0f;
	LightParameters.SpecularScale = SpecularScale;
	
	LightParameters.Color = FVector(GetColor() / (0.5f * SourceWidth * SourceHeight));
	LightParameters.Position = GetOrigin();
	LightParameters.Direction = -GetDirection();
	LightParameters.Tangent = FVector(WorldToLight.M[0][2], WorldToLight.M[1][2], WorldToLight.M[2][2]);
	
	LightParameters.InvRadius = InvRadius;
	LightParameters.SourceRadius = SourceWidth * 0.5f;
	LightParameters.SoftSourceRadius = 0.0f;
	LightParameters.SourceLength = SourceHeight * 0.5f;
	
	LightParameters.SpotAngles = FVector2D(-2.0f, 1.0f);
	LightParameters.SourceTexture = SourceTexture->Resource->TextureRHI or GWhiteTexture->TextureRHI;
	LightParameters.RectLightBarnCosAngle = FMath::Cos(FMath::DegreesToRadians(BarnDoorAngle));
	LightParameters.RectLightBarnLength = BarnDoorLength;
}

void FDirectionalLightSceneProxy::GetLightShaderParameters(FLightShaderParameters& LightParameters) const
{
	LightParameters.FalloffExponent = 0.0f;
	LightParameters.SpecularScale = SpecularScale;
	
	LightParameters.Color = FVector(GetColor() * AtmosphereTransmittanceFactor);
	LightParameters.Position = FVector::ZeroVector;
	LightParameters.Direction = -GetDirection();
	LightParameters.Tangent = -GetDirection();
	
	LightParameters.InvRadius = 0.0f;
	LightParameters.SourceRadius = FMath::Sin(0.5f * FMath::DegreesToRadians(LightSourceAngle));
	LightParameters.SoftSourceRadius = FMath::Sin(0.5f * FMath::DegreesToRadians(LightSourceSoftAngle));
	LightParameters.SourceLength = 0.0f;
	
	LightParameters.SpotAngles = FVector2D(0, 0);
	LightParameters.SourceTexture = GWhiteTexture->TextureRHI;
}
