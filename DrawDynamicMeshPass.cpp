
template<typename LambdaType>
void DrawDynamicMeshPass(const FSceneView& View, FRHICommandList& RHICmdList, const LambdaType& BuildPassProcessorLambda)
{
	FDynamicMeshDrawCommandStorage DynamicMeshDrawCommandStorage;
	FMeshCommandOneFrameArray VisibleMeshDrawCommands;

	FDynamicPassMeshDrawListContext DynamicMeshPassContext(DynamicMeshDrawCommandStorage, VisibleMeshDrawCommands);

	BuildPassProcessorLambda(&DynamicMeshPassContext);

	// We assume all dynamic passes are in stereo if it is enabled in the view, so we apply ISR to them
	const uint32 InstanceFactor = View.IsInstancedStereoPass() ? 2 : 1;
	DrawDynamicMeshPassPrivate(View, RHICmdList, VisibleMeshDrawCommands, DynamicMeshDrawCommandStorage, InstanceFactor);
}


DrawDynamicMeshPass(View, RHICmdList, 
	[&View, CurrentDecalStage, RenderTargetMode](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
	    FMeshDecalMeshProcessor PassMeshProcessor(
	        View.Family->Scene->GetRenderScene(),
	        &View,
	        CurrentDecalStage,
	        RenderTargetMode,
	        DynamicMeshPassContext);

	    for (int32 MeshBatchIndex = 0; MeshBatchIndex < View.MeshDecalBatches.Num(); ++MeshBatchIndex)
	    {
	        const FMeshBatch* Mesh = View.MeshDecalBatches[MeshBatchIndex].Mesh;
	        const FPrimitiveSceneProxy* PrimitiveSceneProxy = View.MeshDecalBatches[MeshBatchIndex].Proxy;

	        PassMeshProcessor.AddMeshBatch(*Mesh, ~0ull, PrimitiveSceneProxy);
	    }
	}
);


DrawDynamicMeshPass(View, RHICmdList,
	[&View, VolumetricFogDistance, &RHICmdList, &VolumetricFogGridSize, &GridZParams](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
	{
		FVoxelizeVolumeMeshProcessor PassMeshProcessor(
			View.Family->Scene->GetRenderScene(),
			&View,
			DynamicMeshPassContext);

		for (int32 MeshBatchIndex = 0; MeshBatchIndex < View.VolumetricMeshBatches.Num(); ++MeshBatchIndex)
		{
			const FMeshBatch* Mesh = View.VolumetricMeshBatches[MeshBatchIndex].Mesh;
			const FPrimitiveSceneProxy* PrimitiveSceneProxy = View.VolumetricMeshBatches[MeshBatchIndex].Proxy;

			VoxelizeVolumePrimitive(PassMeshProcessor, RHICmdList, View, VolumetricFogGridSize, GridZParams, PrimitiveSceneProxy, *Mesh);
		}
	}
);

