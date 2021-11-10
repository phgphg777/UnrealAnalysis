DRAW


FViewport::Draw(bool bShouldPresent)
{
	EnqueueBeginRenderFrame(bShouldPresent);
	ViewportClient->Draw( this, &FCanvas(this,,,,,) );
	{
		FSceneViewFamilyContext ViewFamily( Canvas->GetRenderTarget(), GetScene(), EngineShowFlags );
		FSceneView* View = CalcSceneView( &ViewFamily, StereoPass ); -> FEditorViewportClient::CalcSceneView()
		{
			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.ViewFamily = ViewFamily;
			ViewInitOptions.ViewElementDrawer = this;
			
			FSceneView* View = new FSceneView(ViewInitOptions);
			{
				Family = InitOptions.ViewFamily;
				Drawer = InitOptions.ViewElementDrawer;
			}

			ViewFamily->Views.Add(View);
		}
		SetupViewForRendering(ViewFamily,*View);

		GetRendererModule().BeginRenderingViewFamily(Canvas,&ViewFamily);
		{
			FSceneRenderer* SceneRenderer = FSceneRenderer::CreateSceneRenderer(ViewFamily, Canvas->GetHitProxyConsumer());
			{
				return new FDeferredShadingSceneRenderer(InViewFamily, HitProxyConsumer);
				{
					FSceneRenderer(InViewFamily, HitProxyConsumer)
					{
						Scene = InViewFamily->Scene;
						ViewFamily = *InViewFamily;   // Copy
						MeshCollector = FMeshElementCollector(ERHIFeatureLevel::SM5);

						FViewInfo* ViewInfo = new(Views) FViewInfo(InViewFamily->Views[i]);
						ViewFamily.Views[ViewIndex] = ViewInfo;
						ViewInfo->Family = &ViewFamily;

						// FLevelEditorViewportClient::Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI)
						ViewInfo->Drawer ? ViewInfo->Drawer->Draw( ViewInfo, &FViewElementPDI(ViewInfo,,) ) : 0;
						{
							FEditorViewportClient::Draw(View,PDI);
							{
								Widget->Render( View, PDI, this );
								ModeTools->DrawActiveModes(View, PDI);
								ModeTools->Render(View, Viewport, PDI);
							}
						}
					}
				}
			}

			ENQUEUE_RENDER_COMMAND()
			{
				ViewExtensionPreRender_RenderThread(RHICmdList, SceneRenderer);
				RenderViewFamily_RenderThread(RHICmdList, SceneRenderer);
				FlushPendingDeleteRHIResources_RenderThread();
			}
		}

		DrawCanvas( *Viewport, *View, *Canvas );
		{
			FEditorViewportClient::DrawCanvas(InViewport, View, Canvas);
			{
				ModeTools->DrawHUD(this, &InViewport, &View, &Canvas);
			}
		}

		Widget->DrawHUD( Canvas );
		DrawAxes(Viewport, Canvas);
	}
	EnqueueEndRenderFrame(bLockToVsync, bShouldPresent);
}








class FEditorCommonDrawHelper
{
	virtual void UNREALED_API Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI);
};

class UNREALED_API FEdMode : public FEditorCommonDrawHelper, public TSharedFromThis<FEdMode>, public FGCObject
{
}

class ENGINE_API FViewElementDrawer
{
	virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) {}
};

class FViewport : public FRenderTarget, protected FRenderResource
{
}

class FViewportClient
{
	virtual void Draw(FViewport* Viewport,FCanvas* Canvas) {}
}

class FCommonViewportClient : public FViewportClient
class UScriptViewportClient : public FCommonViewportClient, public UObject

class ENGINE_API UGameViewportClient : public UScriptViewportClient, public FExec
{
	FViewport* Viewport;
	UGameInstance* GameInstance;	
	UWorld* World;

	virtual void Draw(FViewport* Viewport,FCanvas* Canvas) override;
}

class UNREALED_API FEditorViewportClient : public FCommonViewportClient, public FViewElementDrawer, public FGCObject
{
	FViewport* Viewport;
	FEngineShowFlags EngineShowFlags;

	bool bUsesDrawHelper;
	FEditorCommonDrawHelper DrawHelper;

	virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override;
	virtual void Draw(FViewport* Viewport,FCanvas* Canvas) override;
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const EStereoscopicPass StereoPass);
}

class UNREALED_API FLevelEditorViewportClient : public FEditorViewportClient
{
	virtual void Draw(const FSceneView* View,FPrimitiveDrawInterface* PDI) override;
	virtual FSceneView* CalcSceneView(FSceneViewFamily* ViewFamily, const EStereoscopicPass StereoPass) override;
}

class ENGINE_API FSceneViewFamily
{
	TArray<const FSceneView*> Views;	// added by each ULocalPlayer in game mode
	EViewModeIndex ViewMode;
	const FRenderTarget* RenderTarget;	// <- UGameViewportClient::Viewport OR FEditorViewportClient::Viewport
	FSceneInterface* Scene;
	FEngineShowFlags EngineShowFlags;
}

class ENGINE_API FSceneView
{
	const FSceneViewFamily* Family;
	FSceneViewStateInterface* State;
	TUniformBufferRef<FViewUniformShaderParameters> ViewUniformBuffer;
	const AActor* ViewActor;
	int32 PlayerIndex;
	FViewElementDrawer* Drawer;
	FViewMatrices ViewMatrices;
}

class FViewInfo : public FSceneView
{
}

class FSceneRenderer
{
	FScene* Scene;
	FSceneViewFamily ViewFamily;
	TArray<FViewInfo> Views;
	FMeshElementCollector MeshCollector;
}