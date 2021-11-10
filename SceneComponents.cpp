
void UDecalComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();

	if ( ShouldComponentAddToScene() && ShouldRender() )
	{
		GetWorld()->Scene->AddDecal(this);
	}
}
void FScene::AddDecal(UDecalComponent* Component)
{
	if(!Component->SceneProxy)
	{
		Component->SceneProxy = Component->CreateSceneProxy();

		FScene* Scene = this;
		FDeferredDecalProxy* Proxy = Component->SceneProxy;
		ENQUEUE_RENDER_COMMAND(FAddDecalCommand)(
			[Scene, Proxy](FRHICommandListImmediate& RHICmdList)
			{
				Scene->Decals.Add(Proxy);
			});
	}
}


void UExponentialHeightFogComponent::CreateRenderState_Concurrent()
{
	Super::CreateRenderState_Concurrent();
	
	if ( ShouldComponentAddToScene() && ShouldRender() && IsRegistered() 
		&& ((FogDensity + SecondFogData.FogDensity) * 1000) > DELTA 
		&& FogMaxOpacity > DELTA
		&& (GetOuter() == NULL || !GetOuter()->HasAnyFlags(RF_ClassDefaultObject)) )
	{
		GetWorld()->Scene->AddExponentialHeightFog(this);
	}
}
void FScene::AddExponentialHeightFog(UExponentialHeightFogComponent* FogComponent)
{
	FScene* Scene = this;
	FExponentialHeightFogSceneInfo HeightFogSceneInfo = FExponentialHeightFogSceneInfo(FogComponent);
	ENQUEUE_RENDER_COMMAND(FAddFogCommand)(
		[Scene, HeightFogSceneInfo](FRHICommandListImmediate& RHICmdList)
		{
			new(Scene->ExponentialFogs) FExponentialHeightFogSceneInfo(HeightFogSceneInfo);
		});
}