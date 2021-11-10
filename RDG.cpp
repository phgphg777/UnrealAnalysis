
class RENDERCORE_API FRDGBuilder {
	TArray<FRDGPass*> Passes;
	TMap< FRDGTexture*, TRefCountPtr<IPooledRenderTarget> > AllocatedTextures;
	TMap< FRDGBuffer*, TRefCountPtr<FPooledRDGBuffer> > AllocatedBuffers;
	TMap< const IPooledRenderTarget*, FRDGTexture* > ExternalTextures;
	TMap< const FPooledRDGBuffer*, FRDGBuffer* > ExternalBuffers;
	
	struct FDeferredInternalTextureQuery {/** Array of all deferred access to internal textures. */
		FRDGTexture* Texture;
		TRefCountPtr<IPooledRenderTarget>* OutTexturePtr;
		bool bTransitionToRead;
	};
	TArray<FDeferredInternalTextureQuery> DeferredInternalTextureQueries;

	struct FDeferredInternalBufferQuery {/** Array of all deferred access to internal buffers. */
		FRDGBuffer* Buffer;
		TRefCountPtr<FPooledRDGBuffer>* OutBufferPtr;
		FRDGResourceState::EAccess DestinationAccess;
		FRDGResourceState::EPipeline DestinationPipeline;
	};
	TArray<FDeferredInternalBufferQuery> DeferredInternalBufferQueries;
}

FRDGTextureRef FRDGBuilder::RegisterExternalTexture(
	const TRefCountPtr<IPooledRenderTarget>& ExternalPooledTexture,
	const TCHAR* Name,
	ERDGResourceFlags Flags)
{
	if (FRDGTextureRef* Texture = ExternalTextures.Find(ExternalPooledTexture.GetReference()))
	{
		return *Texture;
	}

	//FRDGTexture* OutTexture = AllocateForRHILifeTime<FRDGTexture>(Name, ExternalPooledTexture->GetDesc(), Flags);
	FRDGTexture* OutTexture = new (MemStack) FRDGTexture(Name, ExternalPooledTexture->GetDesc(), Flags);
	OutTexture->PooledRenderTarget = ExternalPooledTexture.GetReference();
	OutTexture->ResourceRHI = ExternalPooledTexture->GetRenderTargetItem().ShaderResourceTexture;
	AllocatedTextures.Add(OutTexture, ExternalPooledTexture);
	ExternalTextures.Add(ExternalPooledTexture.GetReference(), OutTexture);

	return OutTexture;
}

FRDGTextureRef FRDGBuilder::CreateTexture(
	const FRDGTextureDesc& Desc,
	const TCHAR* Name,
	ERDGResourceFlags Flags)
{
	FRDGTextureDesc TextureDesc = Desc;
	TextureDesc.DebugName = Name;
	return new (MemStack) FRDGTexture(Name, TextureDesc, Flags);
}

FRDGTextureUAVRef FRDGBuilder::CreateUAV(const FRDGTextureUAVDesc& Desc)
{
	return new (MemStack) FRDGTextureUAV(Desc.Texture->Name, Desc);
}

struct FRHIUniformBufferLayout {
	struct FResourceParameter {/** Data structure to store information about resource parameter in a shader parameter structure. */
		uint16 MemberOffset;/** Byte offset to each resource in the uniform buffer memory. */
		EUniformBufferBaseType MemberType;/** Type of the member that allow (). */
	};
	TArray<FResourceParameter> Resources;/** The list of all resource inlined into the shader parameter structure. */
}

class FRDGPassParameterStruct final {
	uint8* Contents;
	const FRHIUniformBufferLayout* Layout;

	FRDGPassParameterStruct(void* InContents, const FRHIUniformBufferLayout* InLayout)
		: Contents(reinterpret_cast<uint8*>(InContents)), Layout(InLayout) {}

	template <typename FParameterStruct>
	FRDGPassParameterStruct(FParameterStruct* Parameters)
		: FRDGPassParameterStruct(Parameters, &FParameterStruct::FTypeInfo::GetStructMetadata()->GetLayout()) {}

	uint32 GetParameterCount() const
	{
		return Layout->Resources.Num();
	}
};

template <typename ParameterStructType>
ParameterStructType* FRDGBuilder::AllocParameters()
{
	ParameterStructType* OutParameterPtr = new(MemStack) ParameterStructType;
	FMemory::Memzero(OutParameterPtr, sizeof(ParameterStructType));
	return OutParameterPtr;
}

class RENDERCORE_API FRDGPass {
	const FRDGEventName Name;
	FRDGPassParameterStruct ParameterStruct;
	const ERDGPassFlags PassFlags;
	//const FRDGEventScope* EventScope = nullptr;
	//const FRDGStatScope* StatScope = nullptr;
};

template <typename ParameterStructType, typename LambdaType>
void FRDGBuilder::AddPass(
	FRDGEventName&& Name,
	ParameterStructType* ParameterStruct,
	ERDGPassFlags Flags,
	LambdaType&& Lambda)
{
	auto NewPass = new(MemStack) TRDGLambdaPass<ParameterStructType, LambdaType>(
		MoveTemp(Name),
		FRDGPassParameterStruct(ParameterStruct),
		Flags,
		MoveTemp(Lambda));

	Passes.Emplace(NewPass);

	if (GRDGImmediateMode)
	{
		ExecutePass(NewPass);
	}
}



void FRDGBuilder::Execute()
{
	if (!GRDGImmediateMode)
	{
		WalkGraphDependencies();

		for (const FRDGPass* Pass : Passes)
		{
			ExecutePass(Pass);
		}
	}

	ProcessDeferredInternalResourceQueries();
	DestructPasses();
}


FRDGPassParameter FRDGPassParameterStruct::GetParameter(uint32 ParameterIndex) const
{
	const auto& Resources = Layout->Resources;
	const EUniformBufferBaseType MemberType = Resources[ParameterIndex].MemberType;
	const uint16 MemberOffset = Resources[ParameterIndex].MemberOffset;
	return FRDGPassParameter(MemberType, Contents + MemberOffset);
}

class RENDERCORE_API FRDGResource {}
class RENDERCORE_API FRDGParentResource : public FRDGResource {}/** A render graph resource with an allocation lifetime tracked by the graph. May have child resources which reference it (e.g. views). */
class RENDERCORE_API FRDGTexture final : public FRDGParentResource {}
class 				 FRDGBuffer final : public FRDGParentResource {}
class FRDGChildResource : public FRDGResource {};/** A render graph resource (e.g. a view) which references a single parent resource (e.g. a texture / buffer). Provides an abstract way to access the parent resource. */
class FRDGShaderResourceView : public FRDGChildResource {};
class FRDGUnorderedAccessView : public FRDGChildResource {};
class FRDGTextureSRV final : public FRDGShaderResourceView {}
class FRDGBufferSRV final : public FRDGShaderResourceView {}
class FRDGTextureUAV final : public FRDGUnorderedAccessView {}
class FRDGBufferUAV final : public FRDGUnorderedAccessView {}

class FRDGPassParameter final {
	const EUniformBufferBaseType MemberType = UBMT_INVALID;
	void* MemberPtr = nullptr;

	FRDGParentResource* GetAsParentResource() const
	{
		return * (FRDGParentResource**) MemberPtr;
	}
	FRDGChildResource* GetAsChildResource() const
	{
		return * (FRDGChildResource**) MemberPtr;
	}
	FRDGTexture* GetAsTexture() const
	{
		return * (FRDGTexture**) MemberPtr;
	}
	FRDGTextureUAV* GetAsTextureUAV() const
	{
		return * (FRDGTextureUAV**) MemberPtr;
	}
};


void FRDGBuilder::WalkGraphDependencies()
{
	for (const FRDGPass* Pass : Passes)
	{
		FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();

		const uint32 ParameterCount = ParameterStruct.GetParameterCount();

		for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
		{
			FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

			if (Parameter.IsParentResource())
			{
				if (FRDGParentResource* Resource = Parameter.GetAsParentResource())
				{
					Resource->ReferenceCount++;
				}
			}
			else if (Parameter.IsChildResource())
			{
				if (FRDGChildResourceRef Resource = Parameter.GetAsChildResource())
				{
					Resource->GetParent()->ReferenceCount++;
				}
			}
			else if (Parameter.IsRenderTargetBindingSlots())
			{
				...
			}
		}
	} 

	// Add additional dependencies from deferred queries.
	for (const auto& Query : DeferredInternalTextureQueries)
	{
		Query.Texture->ReferenceCount++;
	}
	for (const auto& Query : DeferredInternalBufferQueries)
	{
		Query.Buffer->ReferenceCount++;
	}

	// Release external texture that have ReferenceCount == 0 and yet are already allocated.
	for (auto Pair : AllocatedTextures)
	{
		if (Pair.Key->ReferenceCount == 0)
		{
			Pair.Value = nullptr;
			Pair.Key->PooledRenderTarget = nullptr;
			Pair.Key->ResourceRHI = nullptr;
		}
	}

	// Release external buffers that have ReferenceCount == 0 and yet are already allocated.
	for (auto Pair : AllocatedBuffers)
	{
		if (Pair.Key->ReferenceCount == 0)
		{
			Pair.Value = nullptr;
			Pair.Key->PooledBuffer = nullptr;
			Pair.Key->ResourceRHI = nullptr;
		}
	}
}


void FRDGBuilder::ExecutePass(const FRDGPass* Pass)
{
	FRHIRenderPassInfo RPInfo;
	PrepareResourcesForExecute(Pass, &RPInfo);

	if (Pass->IsRaster())
		RHICmdList.BeginRenderPass(RPInfo, Pass->GetName());
	
	Pass->Execute(RHICmdList); // Lambda(RHICmdList);

	if (Pass->IsRaster())
		RHICmdList.EndRenderPass();
	
	if (!GRDGImmediateMode)
	{
		ReleaseUnreferencedResources(Pass);
	}
}

void FRDGBuilder::PrepareResourcesForExecute(const FRDGPass* Pass, struct FRHIRenderPassInfo* OutRPInfo)
{
	const bool bIsCompute = Pass->IsCompute();

	FRDGBarrierBatcher BarrierBatcher(RHICmdList, Pass);

	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();
	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	TSet<FRDGTexture*> ReadTextures;
	TSet<FRDGTexture*> ModifiedTextures;
	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTexture* Texture = Parameter.GetAsTexture())
			{
				ReadTextures.Add(Texture);
			}
		}
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTexture* Texture = Parameter.GetAsTexture())
			{
				ModifiedTextures.Add(Texture);
			}
		}
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRV* SRV = Parameter.GetAsTextureSRV())
			{
				ReadTextures.Add(SRV->GetParent());
			}
		}
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAV* UAV = Parameter.GetAsTextureUAV())
			{
				ModifiedTextures.Add(UAV->GetParent());
			}
		}
		case UBMT_RENDER_TARGET_BINDING_SLOTS:
		{
			...
		}
		default:
		}
	}

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		{
			if (FRDGTexture* Texture = Parameter.GetAsTexture())
			{
				check(Texture->PooledRenderTarget);
				check(Texture->ResourceRHI);
				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
			}
		}
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTexture* Texture = Parameter.GetAsTexture())
			{
				AllocateRHITextureIfNeeded(Texture);
				BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Write);
			}
		}
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRV* SRV = Parameter.GetAsTextureSRV())
			{
				FRDGTexture* Texture = SRV->Desc.Texture;

				AllocateRHITextureSRVIfNeeded(SRV);

				if (!ModifiedTextures.Contains(Texture))
				{
					BarrierBatcher.QueueTransitionTexture(Texture, FRDGResourceState::EAccess::Read);
				}
			}
		}
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAV* UAV = Parameter.GetAsTextureUAV())
			{
				FRDGTexture* Texture = UAV->Desc.Texture;
				AllocateRHITextureUAVIfNeeded(UAV);
				FRHIUnorderedAccessView* UAVRHI = UAV->GetRHI();
				bool bGeneratingMips = ReadTextures.Contains(Texture);
				BarrierBatcher.QueueTransitionUAV(UAVRHI, Texture, FRDGResourceState::EAccess::Write, bGeneratingMips);
			}
		}
		case UBMT_RDG_BUFFER: 					{}
		case UBMT_RDG_BUFFER_SRV: 				{}
		case UBMT_RDG_BUFFER_UAV: 				{}
		case UBMT_RDG_BUFFER_COPY_DEST:			{}
		case UBMT_RENDER_TARGET_BINDING_SLOTS:	{}
		default:
		}
	}

	OutRPInfo->bGeneratingMips = Pass->IsGenerateMips();
}

void FRDGBuilder::AllocateRHITextureUAVIfNeeded(FRDGTextureUAV* UAV)
{
	if (UAV->ResourceRHI)
		return;

	AllocateRHITextureIfNeeded(UAV->Desc.Texture);

	FRDGTexture* Texture = UAV->Desc.Texture;
	FSceneRenderTargetItem& RenderTarget = Texture->PooledRenderTarget->GetRenderTargetItem();

	UAV->ResourceRHI = RenderTarget.MipUAVs[UAV->Desc.MipLevel];
}

void FRDGBuilder::AllocateRHITextureIfNeeded(FRDGTexture* Texture)
{
	if (Texture->PooledRenderTarget)
	{
		return;
	}

	TRefCountPtr<IPooledRenderTarget>& PooledRenderTarget = AllocatedTextures.FindOrAdd(Texture);

	GRenderTargetPool.FindFreeElement(RHICmdList, Texture->Desc, PooledRenderTarget, Texture->Name, false);

	Texture->PooledRenderTarget = PooledRenderTarget;
	Texture->ResourceRHI = PooledRenderTarget->GetRenderTargetItem().ShaderResourceTexture;
}

void FRDGBuilder::ReleaseUnreferencedResources(const FRDGPass* Pass)
{
	FRDGPassParameterStruct ParameterStruct = Pass->GetParameters();
	const uint32 ParameterCount = ParameterStruct.GetParameterCount();

	for (uint32 ParameterIndex = 0; ParameterIndex < ParameterCount; ++ParameterIndex)
	{
		FRDGPassParameter Parameter = ParameterStruct.GetParameter(ParameterIndex);

		switch (Parameter.GetType())
		{
		case UBMT_RDG_TEXTURE:
		case UBMT_RDG_TEXTURE_COPY_DEST:
		{
			if (FRDGTexture* Texture = Parameter.GetAsTexture())
			{
				ReleaseRHITextureIfUnreferenced(Texture);
			}
		}
		case UBMT_RDG_TEXTURE_SRV:
		{
			if (FRDGTextureSRV* SRV = Parameter.GetAsTextureSRV())
			{
				ReleaseRHITextureIfUnreferenced(SRV->GetParent());
			}
		}
		case UBMT_RDG_TEXTURE_UAV:
		{
			if (FRDGTextureUAV* UAV = Parameter.GetAsTextureUAV())
			{
				ReleaseRHITextureIfUnreferenced(UAV->GetParent());
			}
		}
		case UBMT_RDG_BUFFER:
		case UBMT_RDG_BUFFER_COPY_DEST: {}
		case UBMT_RDG_BUFFER_SRV: {}
		case UBMT_RDG_BUFFER_UAV: {}
		case UBMT_RENDER_TARGET_BINDING_SLOTS: {}
		default:
		}
	}
}

void FRDGBuilder::ReleaseRHITextureIfUnreferenced(FRDGTexture* Texture)
{
	check(Texture->ReferenceCount > 0);
	Texture->ReferenceCount--;

	if (Texture->ReferenceCount == 0)
	{
		Texture->PooledRenderTarget = nullptr;
		Texture->ResourceRHI = nullptr;
		AllocatedTextures.FindChecked(Texture) = nullptr;
	}
}
