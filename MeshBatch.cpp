DRAW PIPELINE


class FDynamicPrimitiveUniformBuffer : public FOneFrameResource
{
	TUniformBuffer<FPrimitiveUniformShaderParameters> UniformBuffer;;
};



class FRHIIndexBuffer : public FRHIResource
{
	uint32 Stride;
	uint32 Size;
	uint32 Usage;
};
class FD3D11IndexBuffer : public FRHIIndexBuffer, public FD3D11BaseShaderResource
{
	TRefCountPtr<ID3D11Buffer> Resource;
};

class FIndexBuffer : public FRenderResource
{	
	TRefCountPtr<FRHIIndexBuffer> IndexBufferRHI;
};

class ENGINE_API FDynamicMeshIndexBuffer32 : public FIndexBuffer
{
	TArray<uint32> Indices;

	virtual void InitRHI() override;
	{
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint32), Indices.Num() * sizeof(uint32), BUF_Static, FRHIResourceCreateInfo());
		{
			return FRHICommandListExecutor::GetImmediateCommandList().CreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
			{
				return GDynamicRHI->CreateIndexBuffer_RenderThread(*this, Stride, Size, InUsage, CreateInfo);
				{
					FScopedRHIThreadStaller StallRHIThread(RHICmdList);
					return RHICreateIndexBuffer(Stride, Size, InUsage, CreateInfo);
					{
						TRefCountPtr<ID3D11Buffer> IndexBufferResource;
						Direct3DDevice->CreateBuffer(&Desc, pInitData, IndexBufferResource.GetInitReference());

						return new FD3D11IndexBuffer(IndexBufferResource, Stride, Size, InUsage);
					}
				}
			}
		}

		void* Buffer = RHILockIndexBuffer(IndexBufferRHI, 0, Indices.Num() * sizeof(uint32), RLM_WriteOnly);
		FMemory::Memcpy(Buffer, Indices.GetData(), Indices.Num() * sizeof(uint32));
		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
};

class FModelWireIndexBuffer : public FIndexBuffer
{
	TArray<FPoly> Polys;
	uint32 NumEdges;

	FModelWireIndexBuffer(UModel* InModel):
		NumEdges(0)
	{
		Polys.Append(InModel->Polys->Element);
		for(int32 PolyIndex = 0;PolyIndex < InModel->Polys->Element.Num();PolyIndex++)
		{
			NumEdges += InModel->Polys->Element[PolyIndex].Vertices.Num();
		}
	}

	virtual void InitRHI() override
	{
		FRHIResourceCreateInfo CreateInfo;
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16),NumEdges * 2 * sizeof(uint16),BUF_Static, CreateInfo);

		uint16* DestIndex = (uint16*)RHILockIndexBuffer(IndexBufferRHI,0,NumEdges * 2 * sizeof(uint16),RLM_WriteOnly);
		uint16 BaseIndex = 0;
		for(int32 PolyIndex = 0;PolyIndex < Polys.Num();PolyIndex++)
		{
			FPoly&	Poly = Polys[PolyIndex];
			for(int32 VertexIndex = 0;VertexIndex < Poly.Vertices.Num();VertexIndex++)
			{
				*DestIndex++ = BaseIndex + VertexIndex;
				*DestIndex++ = BaseIndex + ((VertexIndex + 1) % Poly.Vertices.Num());
			}
			BaseIndex += Poly.Vertices.Num();
		}
		RHIUnlockIndexBuffer(IndexBufferRHI);
	}
};




struct FMeshBatch
{
	TArray<FMeshBatchElement,TInlineAllocator<1> > Elements;
	
	/* Mesh Id in a primitive. Used for stable sorting of draws belonging to the same primitive. **/
	uint16 MeshIdInPrimitive = 0;

	uint32 Type = PT_TriangleList;
	uint32 DepthPriorityGroup = SDPG_World;

	uint32 bUseForMaterial = true;
	uint32 bWireframe = false;
	uint32 bCanApplyViewModeOverrides = false;
	uint32 bUseWireframeSelectionColoring = false;
	uint32 bUseSelectionOutline = true;
	uint32 bSelectable = true;

	const FLightCacheInterface* LCI = nullptr;
	const FVertexFactory* VertexFactory;
	const FMaterialRenderProxy* MaterialRenderProxy;
}

struct FMeshBatchElement
{
	FUniformBufferRHIParamRef PrimitiveUniformBuffer = nullptr;
	const TUniformBuffer<FPrimitiveUniformShaderParameters>* PrimitiveUniformBufferResource = nullptr;
	EPrimitiveIdMode PrimitiveIdMode = PrimID_FromPrimitiveSceneInfo;
	uint32 DynamicPrimitiveShaderDataIndex = 0;
	uint32* InstanceRuns = nullptr;
	
	uint32 NumInstances = 1;
	uint32 BaseVertexIndex = 0;

	const FIndexBuffer* IndexBuffer;
	uint32 FirstIndex;
	uint32 NumPrimitives;
	uint32 MinVertexIndex;
	uint32 MaxVertexIndex;
}




{
	in FLocalVertexFactory* VertexFactory;
	in FStaticMeshVertexBuffers* VertexBuffers;
	in ?* IndexBuffer;

	FMeshBatch& Mesh = Collector.AllocateMesh();
	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	
	Mesh.VertexFactory = &VertexFactory;
	Mesh.MaterialRenderProxy = WireframeMaterial;
	// Mesh.ReverseCulling = LocalToWorld.GetDeterminant() < 0.0f ? true : false;

	BatchElement.IndexBuffer = ;
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = ;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffers->PositionVertexBuffer.GetNumVertices() - 1;

	Collector.AddMesh(ViewIndex, Mesh);
}



{
	
}



{
	in FLocalVertexFactory VertexFactory;
	in FStaticMeshVertexBuffers VertexBuffers;
	in FModelWireIndexBuffer WireIndexBuffer;

	FMeshBatch& Mesh = Collector.AllocateMesh();
	FMeshBatchElement& BatchElement = Mesh.Elements[0];
	
	Mesh.VertexFactory = &VertexFactory;  
	Mesh.MaterialRenderProxy = WireframeMaterial; 
	Mesh.Type = PT_LineList;
	Mesh.DepthPriorityGroup = IsSelected() ? SDPG_Foreground : SDPG_World;
	Mesh.CastShadow = false;
	
	BatchElement.IndexBuffer = &WireIndexBuffer; 
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = WireIndexBuffer.GetNumEdges();
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffers.PositionVertexBuffer.GetNumVertices() - 1; 
	
	Collector.AddMesh(ViewIndex, Mesh);
}



{
	in FLocalVertexFactory* CollisionVertexFactory;
	in FStaticMeshVertexBuffers* VertexBuffers;
	in FDynamicMeshIndexBuffer32* IndexBuffer;

	FMeshBatch& Mesh = Collector.AllocateMesh();
	FMeshBatchElement& BatchElement = Mesh.Elements[0];

	Mesh.VertexFactory = CollisionVertexFactory;
	Mesh.MaterialRenderProxy = MatInst;
	// Mesh.Type = PT_TriangleList;
	// Mesh.DepthPriorityGroup = SDPG_World;
	// Mesh.bCanApplyViewModeOverrides = false;
	// Mesh.ReverseCulling = LocalToWorld.GetDeterminant() < 0.0f ? true : false;
	
 	BatchElement.IndexBuffer = IndexBuffer;
	BatchElement.FirstIndex = 0;
	BatchElement.NumPrimitives = IndexBuffer->Indices.Num() / 3;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = VertexBuffers->PositionVertexBuffer.GetNumVertices() - 1;
	
	FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer 
		= Collector.AllocateOneFrameResource<FDynamicPrimitiveUniformBuffer>();
	{
		FBoxSphereBounds WorldBounds, LocalBounds;
		CalcBoxSphereBounds(WorldBounds, LocalToWorld);
		CalcBoxSphereBounds(LocalBounds, FTransform::Identity);
		DynamicPrimitiveUniformBuffer.Set(LocalToWorld.ToMatrixWithScale(), LocalToWorld.ToMatrixWithScale(), WorldBounds, LocalBounds, true, false, bUseEditorDepthTest);
	}
	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

	Collector.AddMesh(ViewIndex, Mesh);
}