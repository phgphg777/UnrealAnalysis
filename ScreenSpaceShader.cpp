
//RenderUtils.h
class FScreenSpaceVertexBuffer : public FVertexBuffer {
	void InitRHI()
	{
		static const FVector2D Vertices[4] = {
			FVector2D(-1,-1),
			FVector2D(-1,+1),
			FVector2D(+1,-1),
			FVector2D(+1,+1),
		};

		FRHIResourceCreateInfo CreateInfo;
		VertexBufferRHI = RHICreateVertexBuffer(sizeof(FVector2D) * 4, BUF_Static, CreateInfo);
		void* VoidPtr = RHILockVertexBuffer(VertexBufferRHI, 0, sizeof(FVector2D) * 4, RLM_WriteOnly);
		FMemory::Memcpy(VoidPtr, Vertices, sizeof(FVector2D) * 4);
		RHIUnlockVertexBuffer(VertexBufferRHI);
	}
}; TGlobalResource<FScreenSpaceVertexBuffer> GScreenSpaceVertexBuffer;

class FTwoTrianglesIndexBuffer : public FIndexBuffer {
	void InitRHI()
	{
		static const uint16 Indices[] = { 0, 1, 3, 0, 3, 2 };

		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), sizeof(uint16) * 6, BUF_Static, CreateInfo);
		void* VoidPtr = RHILockIndexBuffer(IndexBufferRHI, 0, sizeof(uint16) * 6, RLM_WriteOnly);
		FMemory::Memcpy(VoidPtr, Indices, 6 * sizeof(uint16));
		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
}; TGlobalResource<FTwoTrianglesIndexBuffer> GTwoTrianglesIndexBuffer;

// FogRendering.cpp
class FFogVertexDeclaration : public FRenderResource {
	FVertexDeclarationRHIRef VertexDeclarationRHI;
	void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		Elements.Add( FVertexElement(0, 0, VET_Float2, 0, sizeof(FVector2D)) ); // StreamIndex, Offset, Type, AttributeIndex, Stride
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
}; TGlobalResource<FFogVertexDeclaration> GFogVertexDeclaration;

bool FDeferredShadingSceneRenderer::RenderFog(FRHICommandListImmediate& RHICmdList, const FLightShaftsOutput& LightShaftsOutput)
{
	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(RHICmdList);
	SceneContext.BeginRenderingSceneColor(RHICmdList, ESimpleRenderTargetMode::EExistingColorAndDepth, FExclusiveDepthStencil::DepthRead_StencilWrite, true);
	RenderViewFog(RHICmdList, Views[0], LightShaftsOutput.LightShaftOcclusion);
	SceneContext.FinishRenderingSceneColor(RHICmdList);
}

template<EHeightFogFeature HeightFogFeature = 0>
void FDeferredShadingSceneRenderer::RenderViewFog(
	FRHICommandList& RHICmdList, 
	const FViewInfo& View, 
	TRefCountPtr<IPooledRenderTarget>& LightShaftOcclusion)
{
	TShaderMapRef<FHeightFogVS> VertexShader(View.ShaderMap);
	TShaderMapRef<TExponentialHeightFogPS > PixelShader(View.ShaderMap);
		
	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);		
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGB, BO_Add, BF_One, BF_SourceAlpha>::GetRHI();// disable alpha writes
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFogVertexDeclaration.VertexDeclarationRHI;
	}
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit); // From now on, RHICmdList.GetBoundVertexShader() or RHICmdList.GetBoundPixelShader() make sence..

	FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundVertexShader(), View.ViewUniformBuffer);
	FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
	SceneTextureParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
	SetTextureParameter(
		RHICmdList, 
		RHICmdList.GetBoundPixelShader(),
		OcclusionTexture, 
		OcclusionSampler,
		TStaticSamplerState<SF_Bilinear,AM_Clamp,AM_Clamp,AM_Clamp>::GetRHI(),
		LightShaftOcclusion->GetRenderTargetItem().ShaderResourceTexture);

	// Draw a quad covering the view.
	RHICmdList.SetViewport(View.ViewRect);
	RHICmdList.SetStreamSource(0, GScreenSpaceVertexBuffer.VertexBufferRHI, 0); // StreamIndex, VertexBuffer, Offset
	RHICmdList.DrawIndexedPrimitive(GTwoTrianglesIndexBuffer.IndexBufferRHI, 0, 0, 4, 0, 2, 1); // IndexBuffer, BaseVertexIndex, FirstInstance, NumVertices, StartIndex, NumPrimitives, NumInstances
}

class FHeightFogVS : public FGlobalShader {
	DECLARE_SHADER_TYPE(FHeightFogVS,Global);
public:
}; IMPLEMENT_SHADER_TYPE(,FHeightFogVS,TEXT("/Engine/Private/HeightFogVertexShader.usf"),TEXT("Main"),SF_Vertex);

class TExponentialHeightFogPS : public FGlobalShader {
	DECLARE_SHADER_TYPE(TExponentialHeightFogPS,Global);
public:
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters)
	LAYOUT_FIELD(FShaderResourceParameter, OcclusionTexture)
	LAYOUT_FIELD(FShaderResourceParameter, OcclusionSampler)
}; IMPLEMENT_SHADER_TYPE(template<>,TExponentialHeightFogPS<EHeightFogFeature::HeightFog>,TEXT("/Engine/Private/HeightFogPixelShader.usf"), TEXT("ExponentialPixelMain"),SF_Pixel)



// DistanceFieldAmbientOcclusion.cpp
void ComputeDistanceFieldNormal(
	FRHICommandListImmediate& RHICmdList, 
	const TArray<FViewInfo>& Views, 
	FSceneRenderTargetItem& DistanceFieldNormal, 
	const FDistanceFieldAOParameters& Parameters)
{
	const FViewInfo& View = Views[0];
	TShaderMapRef<FPostProcessVS> VertexShader(View.ShaderMap);
	TShaderMapRef<FComputeDistanceFieldNormalPS> PixelShader(View.ShaderMap);

	FRHIRenderPassInfo RPInfo(DistanceFieldNormal.TargetableTexture, ERenderTargetActions::Clear_Store);
	TransitionRenderPassTargets(RHICmdList, RPInfo);
	RHICmdList.BeginRenderPass(RPInfo, TEXT("ComputeDistanceFieldNormal"));
	{
		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		{
			RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleList;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		}
		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		FGlobalShader::SetParameters<FViewUniformShaderParameters>(RHICmdList, RHICmdList.GetBoundPixelShader(), View.ViewUniformBuffer);
		SceneTextureParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), View.FeatureLevel, ESceneTextureSetupMode::All);
		AOParameters.Set(RHICmdList, RHICmdList.GetBoundPixelShader(), Parameters);

		float TargetSizeX = View.ViewRect.Width() / GAODownsampleFactor;
		float TargetSizeY = View.ViewRect.Height() / GAODownsampleFactor;
		
		RHICmdList.SetViewport(0.0f, 0.0f, 0.0f, TargetSizeX, TargetSizeY, 1.0f);
		DrawRectangle(RHICmdList,
			0, 0, TargetSizeX, TargetSizeY,
			0, 0, View.ViewRect.Width(), View.ViewRect.Height(),
			FIntPoint(TargetSizeX, TargetSizeY),
			FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
			VertexShader);
	}
	RHICmdList.EndRenderPass();
	RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, DistanceFieldNormal.TargetableTexture);
}

class FComputeDistanceFieldNormalPS : public FGlobalShader {
	DECLARE_SHADER_TYPE(FComputeDistanceFieldNormalPS, Global);
public:
	LAYOUT_FIELD(FSceneTextureShaderParameters, SceneTextureParameters);
	LAYOUT_FIELD(FAOParameters, AOParameters);
} IMPLEMENT_SHADER_TYPE(,FComputeDistanceFieldNormalPS,TEXT("/Engine/Private/DistanceFieldScreenGridLighting.usf"),TEXT("ComputeDistanceFieldNormalPS"),SF_Pixel);



// ShadowDepthRendering.cpp
void FProjectedShadowInfo::RenderDepthInner(FRHICommandListImmediate& RHICmdList, FSceneRenderer* SceneRenderer, FBeginShadowRenderPassFunction BeginShadowRenderPass, )
{
	RHICmdList.SetViewport(
		X + BorderSize, Y + BorderSize, 0.0f,
		X + BorderSize + ResolutionX, Y + BorderSize + ResolutionY, 1.0f );

	BeginShadowRenderPass(RHICmdList, false);
	CopyCachedShadowMap(RHICmdList, , SceneRenderer, *ShadowDepthView);
	RHICmdList.EndRenderPass();
}

void FProjectedShadowInfo::CopyCachedShadowMap(
	FRHICommandList& RHICmdList, , 
	FSceneRenderer* SceneRenderer, 
	const FViewInfo& View)
{
	const FCachedShadowMapData& CachedShadowMapData = SceneRenderer->Scene->CachedShadowMaps.FindChecked(GetLightSceneInfo().Id);
	const IPooledRenderTarget* DepthTarget = CachedShadowMapData.ShadowMap.DepthTarget;
	
	TShaderMapRef<FScreenVS> VertexShader(ShadowDepthView->ShaderMap);
	TShaderMapRef<FCopyShadowMaps2DPS> PixelShader(ShadowDepthView->ShaderMap);

	FGraphicsPipelineStateInitializer GraphicsPSOInit;
	{
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<CW_NONE>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
		GraphicsPSOInit.PrimitiveType = PT_TriangleList;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
	}
	SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

	SetTextureParameter(
		RHICmdList, 
		RHICmdList.GetBoundPixelShader(), 
		ShadowDepthTexture, 
		ShadowDepthSampler, 
		TStaticSamplerState<SF_Point>::GetRHI(), 
		DepthTarget->GetRenderTargetItem().ShaderResourceTexture);

	DrawRectangle(
		RHICmdList,
		0, 0, ResolutionX, ResolutionY,
		BorderSize, BorderSize, ResolutionX, ResolutionY,
		FIntPoint(ResolutionX, ResolutionY),
		DepthTarget->GetDesc().Extent,
		VertexShader);
}

class FCopyShadowMaps2DPS : public FGlobalShader {
	DECLARE_SHADER_TYPE(FCopyShadowMaps2DPS, Global);
public:
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthTexture);
	LAYOUT_FIELD(FShaderResourceParameter, ShadowDepthSampler);
}
IMPLEMENT_SHADER_TYPE(, FCopyShadowMaps2DPS, TEXT("/Engine/Private/CopyShadowMaps.usf"), TEXT("Copy2DDepthPS"), SF_Pixel);

Texture2D ShadowDepthTexture;
SamplerState ShadowDepthSampler;
struct FScreenVertexOutput {
	noperspective MaterialFloat2 UV : TEXCOORD0;
	float4 Position : SV_POSITION;
};
void Copy2DDepthPS(
	FScreenVertexOutput Input,
	out float OutDepth : SV_DEPTH,
	out float4 OutColor : SV_Target0
	)
{
	OutColor = 0;
	OutDepth = Texture2DSampleLevel(ShadowDepthTexture, ShadowDepthSampler, Input.UV, /*Mip*/0).x;
}