class FSceneViewStateReference {
	FSceneViewStateInterface* Reference;
}

class UNREALED_API FEditorViewportClient : public FCommonViewportClient, public FViewElementDrawer, public FGCObject {
	FSceneViewStateReference ViewState;
}

FEditorViewportClient::FEditorViewportClient(FEditorModeTools* InModeTools, FPreviewScene* InPreviewScene, const TWeakPtr<SEditorViewport>& InEditorViewportWidget)
{
	ViewState.Allocate();
}

void FSceneViewStateReference::Allocate()
{
	Reference = new FSceneViewState();
}

void FEditorViewportClient::Draw(FViewport* InViewport, FCanvas* Canvas)
{
	FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues(...) );

	FSceneView* View = CalcSceneView( &ViewFamily, StereoPass );

	GetRendererModule().BeginRenderingViewFamily(Canvas, &ViewFamily);
}

FSceneView* FEditorViewportClient::CalcSceneView(FSceneViewFamily* ViewFamily, const EStereoscopicPass StereoPass)
{
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewFamily = ViewFamily;
	ViewInitOptions.SceneViewStateInterface = ( (StereoPass < eSSP_RIGHT_EYE) ? ViewState.GetReference() : StereoViewStates[ViewStateIndex].GetReference() );

	FSceneView* View = new FSceneView(ViewInitOptions);
}

class ENGINE_API FSceneView {
	const FSceneViewFamily* Family;
	FSceneViewStateInterface* State;
}

FSceneView::FSceneView(const FSceneViewInitOptions& InitOptions)
	: Family(InitOptions.ViewFamily)
	, State(InitOptions.SceneViewStateInterface)
{

}

void FRendererModule::BeginRenderingViewFamily(FCanvas* Canvas, FSceneViewFamily* ViewFamily)
{
	FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(ViewFamily, Canvas->GetHitProxyConsumer());

	ENQUEUE_RENDER_COMMAND(FDrawSceneCommand)(
	[SceneRenderer, DrawSceneEnqueue](FRHICommandListImmediate& RHICmdList)
	{
		RenderViewFamily_RenderThread(RHICmdList, SceneRenderer);
	});
}

class FSceneRenderer {
	FScene* Scene;
	FSceneViewFamily ViewFamily;
	TArray<FViewInfo> Views;
}

FSceneRenderer::FSceneRenderer(const FSceneViewFamily* InViewFamily,FHitProxyConsumer* HitProxyConsumer)
:	Scene(InViewFamily->Scene ? InViewFamily->Scene->GetRenderScene() : NULL)
,	ViewFamily(*InViewFamily)
{
	FViewInfo* ViewInfo = new(Views) FViewInfo(InViewFamily->Views[ViewIndex]);
	ViewFamily.Views[ViewIndex] = ViewInfo;
	ViewInfo->Family = &ViewFamily;
}

class FViewInfo : public FSceneView {
	FSceneViewState* ViewState;
}

FViewInfo::FViewInfo(const FSceneView* InView)
	:	FSceneView(*InView)
{
	ViewState = (FSceneViewState*) State;
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////
class ENGINE_API FSceneView {
	/** The uniform buffer for the view's parameters. This is only initialized in the rendering thread's copies of the FSceneView. */
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;

	/** Sets up the view rect parameters in the view's uniform shader parameters */
	void SetupViewRectUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters, 
		const FIntPoint& InBufferSize,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrice) const;

	/** 
	 * Populates the uniform buffer prameters common to all scene view use cases
	 * View parameters should be set up in this method if they are required for the view to render properly.
	 * This is to avoid code duplication and uninitialized parameters in other places that create view uniform parameters (e.g Slate) 
	 */
	void SetupCommonViewUniformBufferParameters(FViewUniformShaderParameters& ViewUniformShaderParameters,
		const FIntPoint& InBufferSize,
		int32 NumMSAASamples,
		const FIntRect& InEffectiveViewRect,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices) const;
}

class FViewInfo : public FSceneView {
	/* Final position of the view in the final render target (in pixels), potentially scaled by ScreenPercentage */
	FIntRect ViewRect;

	/** Cached view uniform shader parameters, to allow recreating the view uniform buffer without having to fill out the entire struct. */
	TUniquePtr<FViewUniformShaderParameters> CachedViewUniformShaderParameters;

	TUniformBufferRef<FViewUniformShaderParameters> VolumetricRenderTargetViewUniformBuffer;

	/** Informations from the previous frame to use for this view. */
	FPreviousViewInfo PrevViewInfo;

	/** Creates the view's uniform buffers given a set of view transforms. */
	RENDERER_API void SetupUniformBufferParameters(
		FSceneRenderTargets& SceneContext,
		const FViewMatrices& InViewMatrices,
		const FViewMatrices& InPrevViewMatrices,
		FBox* OutTranslucentCascadeBoundsArray, 
		int32 NumTranslucentCascades,
		FViewUniformShaderParameters& ViewUniformShaderParameters) const;
}

bool FDeferredShadingSceneRenderer::InitViews(FRHICommandListImmediate& RHICmdList, FExclusiveDepthStencil::Type BasePassDepthStencilAccess, struct FILCUpdatePrimTaskData& ILCTaskData)
{
	FViewInfo& View = Views[ViewIndex];
	View.InitRHIResources();
}

void FViewInfo::InitRHIResources()
{
	this->CachedViewUniformShaderParameters = MakeUnique<FViewUniformShaderParameters>();

	FSceneRenderTargets& SceneContext = FSceneRenderTargets::Get(FRHICommandListExecutor::GetImmediateCommandList());

	SetupUniformBufferParameters(
		SceneContext,
		this->ViewMatrices,
		this->PrevViewInfo.ViewMatrices,,,
		*this->CachedViewUniformShaderParameters);

	ViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
		*this->CachedViewUniformShaderParameters, UniformBuffer_SingleFrame);
}

void FViewInfo::SetupUniformBufferParameters(
	FSceneRenderTargets& SceneContext,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices,,,
	FViewUniformShaderParameters& ViewUniformShaderParameters) const
{
	SetupCommonViewUniformBufferParameters(
		ViewUniformShaderParameters,
		SceneContext.GetBufferSizeXY(),
		SceneContext.GetMSAACount(),
		this->ViewRect,
		InViewMatrices,
		InPrevViewMatrices);

	...
}

void FSceneView::SetupCommonViewUniformBufferParameters(
	FViewUniformShaderParameters& ViewUniformShaderParameters,
	const FIntPoint& BufferSize,
	int32 NumMSAASamples,
	const FIntRect& EffectiveViewRect,
	const FViewMatrices& InViewMatrices,
	const FViewMatrices& InPrevViewMatrices) const
{
	...

	SetupViewRectUniformBufferParameters(
		ViewUniformShaderParameters, 
		BufferSize, 
		EffectiveViewRect, 
		InViewMatrices, 
		InPrevViewMatrices);
}

void FSceneRenderer::InitVolumetricRenderTargetForViews(FRDGBuilder& GraphBuilder)
{
	FViewInfo& ViewInfo = Views[ViewIndex];

	FIntPoint VolumetricTracingResolution;
	...

	FViewUniformShaderParameters ViewVolumetricCloudRTParameters = *ViewInfo.CachedViewUniformShaderParameters;

	FViewMatrices ViewMatrices = ViewInfo.ViewMatrices;
	ViewMatrices.HackRemoveTemporalAAProjectionJitter();
	...
	ViewMatrices.HackAddTemporalAAProjectionJitter(...);

	ViewInfo.SetupViewRectUniformBufferParameters(
		ViewVolumetricCloudRTParameters,
		VolumetricTracingResolution,
		FIntRect(0, 0, VolumetricTracingResolution.X, VolumetricTracingResolution.Y),
		ViewMatrices,
		ViewInfo.PrevViewInfo.ViewMatrices); // This could also be changed if needed

	ViewInfo.VolumetricRenderTargetViewUniformBuffer = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(
		ViewVolumetricCloudRTParameters, UniformBuffer_SingleFrame);
}