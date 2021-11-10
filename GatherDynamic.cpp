
void ComputeDynamicMeshRelevance(, , 
	const FPrimitiveViewRelevance& ViewRelevance, 
	const FMeshBatchAndRelevance& MeshBatch, 
	FViewInfo& View, 
	FMeshPassMask& PassMask, , )
{
	const int32 NumElements = MeshBatch.Mesh->Elements.Num();
	check(NumElements==1); // If ViewRelevance.bDynamicRelavence==true, it's garanteed.

	if (ViewRelevance.bDrawRelevance)
	{
		PassMask.Set(EMeshPass::EditorSelection);
		View.NumVisibleDynamicMeshElements[EMeshPass::EditorSelection] += NumElements;

		if (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderInDepthPass || ViewRelevance.bRenderCustomDepth)
		{
			PassMask.Set(EMeshPass::DepthPass);
			View.NumVisibleDynamicMeshElements[EMeshPass::DepthPass] += NumElements;

			if (ViewRelevance.bRenderInMainPass || ViewRelevance.bRenderCustomDepth)
			{
				PassMask.Set(EMeshPass::BasePass);
				View.NumVisibleDynamicMeshElements[EMeshPass::BasePass] += NumElements;
				
				if (ViewRelevance.bRenderCustomDepth)
				{
					PassMask.Set(EMeshPass::CustomDepth);
					View.NumVisibleDynamicMeshElements[EMeshPass::CustomDepth] += NumElements;
				}
			}
		}
	}
}


class FSceneRenderer {
	FMeshElementCollector MeshCollector;
}

class FMeshElementCollector {
	FSceneView* View;								 // = &Views[0]
	FSimpleElementCollector* SimpleElementCollector; // = &Views[0].SimpleElementCollector
	
	FGlobalDynamicIndexBuffer* DynamicIndexBuffer;
	FGlobalDynamicVertexBuffer* DynamicVertexBuffer;
	FGlobalDynamicReadBuffer* DynamicReadBuffer;

	int32 NumMeshBatchElements; // Number of elements in gathered meshes.
	TArray<FMeshBatchAndRelevance>* MeshBatches; 	 // = &Views[0].DynamicMeshElements
	TArray<FPrimitiveUniformShaderParameters>* DynamicPrimitiveShaderData; // = &Views[0].DynamicPrimitiveShaderData
	
	const FPrimitiveSceneProxy* PrimitiveSceneProxy; // Current primitive being gathered.
	uint16 MeshIdInPrimitive; // Current Mesh Id in a Primitive
}

FPrimitiveDrawInterface* FMeshElementCollector::GetPDI(int32 ViewIndex = 0)
{
	return SimpleElementCollector;
}

void FMeshElementCollector::AddViewMeshArrays(
	FSceneView* InView, 
	TArray<FMeshBatchAndRelevance>* ViewMeshes,
	FSimpleElementCollector* ViewSimpleElementCollector, 
	TArray<FPrimitiveUniformShaderParameters>* InDynamicPrimitiveShaderData, ,
	FGlobalDynamicIndexBuffer* InDynamicIndexBuffer,
	FGlobalDynamicVertexBuffer* InDynamicVertexBuffer,
	FGlobalDynamicReadBuffer* InDynamicReadBuffer)
{
	View = InView;
	MeshBatches = ViewMeshes;
	SimpleElementCollectors = ViewSimpleElementCollector;
	DynamicPrimitiveShaderData = InDynamicPrimitiveShaderData;

	MeshIdInPrimitive = 0;
	NumMeshBatchElements = 0;
	
	DynamicIndexBuffer = InDynamicIndexBuffer;
	DynamicVertexBuffer = InDynamicVertexBuffer;
	DynamicReadBuffer = InDynamicReadBuffer;
}

void FMeshElementCollector::SetPrimitive(const FPrimitiveSceneProxy* InPrimitiveSceneProxy, FHitProxyId DefaultHitProxyId)
{
	PrimitiveSceneProxy = InPrimitiveSceneProxy;
	MeshIdInPrimitive = 0;
	SimpleElementCollector->PrimitiveMeshId = 0;
	SimpleElementCollector->HitProxyId = DefaultHitProxyId;
}

void FMeshElementCollector::AddMesh(int32 ViewIndex=0, FMeshBatch& MeshBatch)
{
	checkSlow(MeshBatch.VertexFactory);
	checkSlow(MeshBatch.VertexFactory->IsInitialized());
	checkSlow(MeshBatch.MaterialRenderProxy);

	MeshBatch.PreparePrimitiveUniformBuffer(PrimitiveSceneProxy, FeatureLevel);

	// If we are maintaining primitive scene data on the GPU, copy the primitive uniform buffer data to a unified array so it can be uploaded later
	if (UseGPUScene(GMaxRHIShaderPlatform, FeatureLevel) && MeshBatch.VertexFactory->GetPrimitiveIdStreamIndex(EVertexInputStreamType::Default) >= 0)
	{
		for (int32 Index = 0; Index < MeshBatch.Elements.Num(); ++Index)
		{
			const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBufferResource = MeshBatch.Elements[Index].PrimitiveUniformBufferResource;

			if (PrimitiveUniformBufferResource)
			{
				TArray<FPrimitiveUniformShaderParameters>* DynamicPrimitiveShaderData = DynamicPrimitiveShaderData;

				const int32 DataIndex = DynamicPrimitiveShaderData->AddUninitialized(1);
				MeshBatch.Elements[Index].PrimitiveIdMode = PrimID_DynamicPrimitiveShaderData;
				MeshBatch.Elements[Index].DynamicPrimitiveShaderDataIndex = DataIndex;
				FPlatformMemory::Memcpy(&(*DynamicPrimitiveShaderData)[DataIndex], PrimitiveUniformBufferResource->GetContents(), sizeof(FPrimitiveUniformShaderParameters));
			}
		}
	}
	MeshBatch.MaterialRenderProxy->UpdateUniformExpressionCacheIfNeeded(Views->GetFeatureLevel());
	MeshBatch.MeshIdInPrimitive = MeshIdInPrimitive;

	++MeshIdInPrimitive;
	NumMeshBatchElements += MeshBatch.Elements.Num();
	new (*MeshBatches) FMeshBatchAndRelevance(MeshBatch, PrimitiveSceneProxy, FeatureLevel);	
}

class FViewInfo : public FSceneView {
	int32 NumVisibleDynamicPrimitives;// Number of visible dynamic primitives. Only for debuging. Set in FRelevancePacket::RenderThreadFinalize()

	// Gathered meshes via overrided GetDynamicMeshElements() against all Visible Dynamic Primitives
	// DynamicMeshElements.Num() == Total number of calls of FMeshElementCollector::AddMesh()
	TArray<FMeshBatchAndRelevance> DynamicMeshElements;

	TArray<FMeshPassMask> DynamicMeshElementsPassRelevance;// Mesh pass relevance for gathered dynamic mesh elements. ~.Num()== DynamicMeshElements.Num() 
	int32 NumVisibleDynamicMeshElements[EMeshPass::Num];// Number of dynamic mesh elements per each mesh pass.
	TArray<uint32> DynamicMeshEndIndices;
	
	FSimpleElementCollector SimpleElementCollector;	// Gathered points, lines and sprites via overrided GetDynamicMeshElements()
	TArray<FPrimitiveUniformShaderParameters> DynamicPrimitiveShaderData;
}

/*
Out: FViewInfo::DynamicMeshElements 				// via overrided GetDynamicMeshElements()
Out: FViewInfo::DynamicPrimitiveShaderData 			// via overrided GetDynamicMeshElements()
Out: FViewInfo::DynamicMeshEndIndices 				
Out: FViewInfo::DynamicMeshElementsPassRelevance 	// via ComputeDynamicMeshRelevance()
Out: FViewInfo::NumVisibleDynamicMeshElements 		// via ComputeDynamicMeshRelevance()
*/
void FSceneRenderer::GatherDynamicMeshElements(,,, 
	FGlobalDynamicIndexBuffer& DynamicIndexBuffer,
	FGlobalDynamicVertexBuffer& DynamicVertexBuffer,
	FGlobalDynamicReadBuffer& DynamicReadBuffer,
	const FPrimitiveViewMasks& HasDynamicMeshElementsMasks, 
	const FPrimitiveViewMasks& HasDynamicEditorMeshElementsMasks, 
	const FPrimitiveViewMasks& HasViewCustomDataMasks,
	FMeshElementCollector& Collector)
{
	FViewInfo& View = Views[0];
	check(View.DynamicMeshElements.Num()==0);
	check(View.DynamicMeshElementsPassRelevance.Num()==0);

	Collector.ClearViewMeshArrays();
	Collector.AddViewMeshArrays(
		&View, 
		&View.DynamicMeshElements,
		&View.SimpleElementCollector,
		&View.DynamicPrimitiveShaderData,,
		&DynamicIndexBuffer, &DynamicVertexBuffer, &DynamicReadBuffer);

	int32 LastNumDynamicMesh = 0;

	for (FPrimitiveSceneInfo* PrimitiveSceneInfo : Scene->Primitives)
	{
		uint32 PrimId = PrimitiveSceneInfo->GetIndex();
		View.DynamicMeshEndIndices[PrimId] = LastNumDynamicMesh;
		if( HasDynamicMeshElementsMasks[PrimId] == 0)
			continue;

		Collector.SetPrimitive(PrimitiveSceneInfo->Proxy, PrimitiveSceneInfo->DefaultDynamicHitProxyId);
		PrimitiveSceneInfo->Proxy->GetDynamicMeshElements(InViewFamily.Views, InViewFamily, 0x1, Collector);

		const auto& ViewRelevance = View.PrimitiveViewRelevanceMap[PrimId];
		for (int32 j = View.DynamicMeshElementsPassRelevance.Num(); j < View.DynamicMeshElements.Num(); ++j)
		{
			FMeshPassMask& PassRelevance = View.DynamicMeshElementsPassRelevance.AddZeroed();
			ComputeDynamicMeshRelevance(, , 
				ViewRelevance, 
				View.DynamicMeshElements[j], 
				/*Out*/View, 
				/*Out*/PassRelevance, , );
		}

		LastNumDynamicMesh = View.DynamicMeshEndIndices[PrimId] = View.DynamicMeshElements.Num();
	}

	check(View.DynamicMeshElements.Num() 
		== View.DynamicMeshElementsPassRelevance.Num()
		== View.DynamicMeshEndIndices[Scene->Primitives.Num() - 1]
		== Collector.NumMeshBatchElements) // Since MeshBatch.Elements.Num() == 1 for dynamic relevant primitives

	if (GIsEditor)
	{
		...
	}
}

