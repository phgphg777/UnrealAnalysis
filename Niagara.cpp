//-----------------------Tranlation-----------------------------//
const FNiagaraTranslateResults &FHlslNiagaraTranslator::Translate(const FNiagaraCompileRequestData* InCompileData, const FNiagaraCompileOptions& InCompileOptions, FHlslNiagaraTranslatorOptions InTranslateOptions)
{
	TranslationOptions = InTranslateOptions;
	TranslateResults.OutputHLSL = "";

	...

	//Now evaluate all the code chunks to generate the shader code.
	if (TranslateResults.bHLSLGenSucceeded)
	{
		...

		if (TranslationOptions.SimTarget == ENiagaraSimTarget::GPUComputeSim)
		{
			DefineDataInterfaceHLSL(HlslOutput);
			DefineExternalFunctionsHLSL(HlslOutput);
			HlslOutput += StageSetupAndTeardownHLSL;
			DefineMainGPUFunctions(DataSetVariables, DataSetReads, DataSetWrites);
		}
		else
		{
			DefineMain(HlslOutput, DataSetVariables, DataSetReads, DataSetWrites);
		}
	}
}

struct NIAGARASHADER_API FNiagaraDataInterfaceGeneratedFunction
{
	FName DefinitionName;/** Name of the function as defined by the data interface. */
	FString InstanceName;/** Name of the instance. Derived from the definition name but made unique for this DI instance and specifier values. */
	using FunctionSpecifier = TTuple<FName, FName>;
	TArray<FunctionSpecifier> Specifiers;/** Specifier values for this instance. */
};

struct NIAGARASHADER_API FNiagaraDataInterfaceGPUParamInfo {		
	FString DataInterfaceHLSLSymbol; /** Symbol of this DI in the hlsl. Used for binding parameters. */
	FString DIClassName; /** Name of the class for this data interface. Used for constructing the correct parameters struct. */
	TArray<FNiagaraDataInterfaceGeneratedFunction> GeneratedFunctions; /** Information about all the functions generated by the translator for this data interface. */
};

void FHlslNiagaraTranslator::ConvertCompileInfoToParamInfo(const FNiagaraScriptDataInterfaceCompileInfo& Info, FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo)
{
	DIInstanceInfo.DataInterfaceHLSLSymbol = GetSanitizedSymbolName(Info.Name.ToString(), true);
	DIInstanceInfo.DIClassName = Info.Type.GetClass()->GetName();

	for (const FNiagaraFunctionSignature& Sig : Info.RegisteredFunctions)
	{
		FNiagaraDataInterfaceGeneratedFunction& DIFunc = DIInstanceInfo.GeneratedFunctions.AddDefaulted_GetRef();
		DIFunc.DefinitionName = Sig.Name;
		DIFunc.InstanceName = GetFunctionSignatureSymbol(Sig);
		for (const TTuple<FName, FName>& Specifier : Sig.FunctionSpecifiers)
		{
			DIFunc.Specifiers.Add(Specifier);
		}
	}
}

void FHlslNiagaraTranslator::DefineDataInterfaceHLSL(FString& InHlslOutput)
{
	FString InterfaceUniformHLSL;
	FString InterfaceFunctionHLSL;

	for(FNiagaraScriptDataInterfaceCompileInfo& Info : CompilationOutput.ScriptData.DataInterfaceInfo)
	{
		UNiagaraDataInterface* Interface = Cast<UNiagaraDataInterface>(*CompileData->CDOs.Find(Info.Type.GetClass()));
		
		FNiagaraDataInterfaceGPUParamInfo& DIInstanceInfo = DIParamInfo.AddDefaulted_GetRef();
		ConvertCompileInfoToParamInfo(Info, DIInstanceInfo);

		Interface->GetParameterDefinitionHLSL(DIInstanceInfo, InterfaceUniformHLSL);

		int FunctionInstanceIndex = 0;
		for (const FNiagaraDataInterfaceGeneratedFunction& DIFunc : DIInstanceInfo.GeneratedFunctions)
		{
			Interface->GetFunctionHLSL(DIInstanceInfo, DIFunc, FunctionInstanceIndex++, InterfaceFunctionHLSL);
		}
	}

	InHlslOutput += InterfaceUniformHLSL + InterfaceFunctionHLSL;
}
//-----------------------Tranlation-----------------------------//


//-----------------------Compile-----------------------------//
TMap<const FShaderCompileJob*, 
	TArray<FNiagaraDataInterfaceGPUParamInfo> > FNiagaraShaderType::ExtraParamInfo;

void FNiagaraShaderType::BeginCompileShader(
	uint32 ShaderMapId,
	int32 PermutationId,
	const FNiagaraShaderScript* Script,
	FSharedShaderCompilerEnvironment* CompilationEnvironment,
	EShaderPlatform Platform,
	TArray<FShaderCommonCompileJobPtr>& NewJobs,
	FShaderTarget Target,
	TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo)
{
	...
	ExtraParamInfo.Add(NewJob, InDIParamInfo);
}

FShader* FNiagaraShaderType::FinishCompileShader(
const FSHAHash& ShaderMapHash,
const FShaderCompileJob& CurrentJob,
const FString& InDebugDescription) const
{
	TArray<FNiagaraDataInterfaceGPUParamInfo> DIParamInfo;
	ExtraParamInfo.RemoveAndCopyValue(&CurrentJob, DIParamInfo);
	FShader* Shader = ConstructCompiled(FNiagaraShaderType::CompiledShaderInitializerType(this, CurrentJob.Key.PermutationId, CurrentJob.Output, ShaderMapHash, InDebugDescription, DIParamInfo));
	return Shader;
}

class FNiagaraShaderType : public FShaderType {
	struct CompiledShaderInitializerType : FShaderCompiledShaderInitializerType {
		const FString DebugDescription;
		TArray< FNiagaraDataInterfaceGPUParamInfo > DIParamInfo;
	};
}

// Called by ConstructCompiled()
FNiagaraShader::FNiagaraShader(const FNiagaraShaderType::CompiledShaderInitializerType& Initializer)
: FShader(Initializer), DebugDescription(Initializer.DebugDescription)
{
	BindParams(Initializer.DIParamInfo, Initializer.ParameterMap);
}

void FNiagaraShader::BindParams(const TArray<FNiagaraDataInterfaceGPUParamInfo>& InDIParamInfo, const FShaderParameterMap &ParameterMap)
{
	DataInterfaceParameters.Empty(InDIParamInfo.Num());
	for(const FNiagaraDataInterfaceGPUParamInfo& DIParamInfo : InDIParamInfo)
	{
		FNiagaraDataInterfaceParamRef& ParamRef = DataInterfaceParameters.AddDefaulted_GetRef();
		ParamRef.Bind(DIParamInfo, ParameterMap);
	}
}

struct FNiagaraDataInterfaceParamRef {
	LAYOUT_FIELD(TIndexedPtr<UNiagaraDataInterfaceBase>, DIType);/** Type of Parameters */
	LAYOUT_FIELD_WITH_WRITER(TMemoryImagePtr<FNiagaraDataInterfaceParametersCS>, Parameters, WriteFrozenParameters);/** Pointer to parameters struct for this data interface. */
};

void FNiagaraDataInterfaceParamRef::Bind(const FNiagaraDataInterfaceGPUParamInfo& InParameterInfo, const class FShaderParameterMap& ParameterMap)
{
	INiagaraShaderModule* Module = INiagaraShaderModule::Get();
	UNiagaraDataInterfaceBase* Base = Module->RequestDefaultDataInterface(*InParameterInfo.DIClassName);

	DIType = TIndexedPtr<UNiagaraDataInterfaceBase>(Base); // TODO - clean up TIndexedPtr::operator=()
	Parameters = Base->CreateComputeParameters();
	if (Parameters)
	{
		Parameters->DIType = DIType;
		Base->BindParameters(Parameters, InParameterInfo, ParameterMap);
	}
}

void FNiagaraDataInterfaceParametersCS_Grid3DCollection::Bind(const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap)
{

}
//-----------------------Compile-----------------------------//


//-----------------------InitSystem-----------------------------//
class NIAGARA_API FNiagaraSystemInstance {
	float Age;/** The age of the System instance. */
	float LastRenderTime;/** The last time this system rendered */
	int32 TickCount;/** The tick count of the System instance. */
	int32 RandomSeedOffset;/** A system-wide offset to permute the deterministic random seed (allows for variance among multiple instances while still being deterministic) */
	float LODDistance;/** LODDistance driven by our component. */
	float MaxLODDistance;

	TArray< TSharedRef<FNiagaraEmitterInstance> > Emitters;

	TArray<uint8> DataInterfaceInstanceData;/** Per instance data for any data interfaces requiring it. */
	TArray<int32> PreTickDataInterfaces;
	TArray<int32> PostTickDataInterfaces;

	/** Map of data interfaces to their instance data. */
	TArray<
		TPair<TWeakObjectPtr<UNiagaraDataInterface>, int32>
	> DataInterfaceInstanceDataOffsets;
}

void FNiagaraSystemInstance::Init(bool bInForceSolo)
{
	Reset(EResetMode::ReInit);
}

void FNiagaraSystemInstance::Activate(EResetMode InResetMode)
{
	Reset(InResetMode);
}

bool FNiagaraSystemInstance::FinalizeTick_GameThread(bool bEnqueueGPUTickIfNeeded)
{
	Reset(ResetMode);
}

void FNiagaraSystemInstance::Reset(FNiagaraSystemInstance::EResetMode Mode)
{
	InitDataInterfaces();
}

void FNiagaraSystemInstance::InitDataInterfaces()
{
	int32 InstanceDataSize = 0;
	DataInterfaceInstanceDataOffsets.Empty();

	auto CalcInstDataSize = [&](const FNiagaraParameterStore& ParamStore, bool bIsGPUSimulation, bool bSearchInstanceParams)
	{
		for (const FNiagaraVariableWithOffset& Var : ParamStore.ReadParameterVariables())
		{
			if (Var.IsDataInterface())
			{
				UNiagaraDataInterface* Interface = ParamStore.GetDataInterfaces()[Var.Offset];
				if (Interface)
				{
					if (int32 Size = Interface->PerInstanceDataSize())
					{
						auto& NewPair = DataInterfaceInstanceDataOffsets.AddDefaulted_GetRef();
						NewPair.Key = Interface;
						NewPair.Value = InstanceDataSize;
						InstanceDataSize += Align(Size, 16);
					}
				}
			}
		}
	}

	...

	DataInterfaceInstanceData.SetNumUninitialized(InstanceDataSize);

	PreTickDataInterfaces.Empty();
	PostTickDataInterfaces.Empty();
	GPUDataInterfaceInstanceDataSize = 0;
	GPUDataInterfaces.Empty();

	for(auto& Pair : DataInterfaceInstanceDataOffsets)
	{
		UNiagaraDataInterface* Interface = Pair.Key.Get();
		assert(Interface);

		if (Interface->HasPreSimulateTick())
		{
			PreTickDataInterfaces.Add(i);
		}

		if (Interface->HasPostSimulateTick())
		{
			PostTickDataInterfaces.Add(i);
		}

		if (bHasGPUEmitters)
		{
			const int32 GPUDataSize = Interface->PerInstanceDataPassedToRenderThreadSize();
			if (GPUDataSize > 0)
			{
				GPUDataInterfaces.Emplace(Interface, Pair.Value);
				GPUDataInterfaceInstanceDataSize += GPUDataSize;
			}
		}

		Interface->InitPerInstanceData(&DataInterfaceInstanceData[Pair.Value], this);
	}
}

struct FGrid3DCollectionRWInstanceData_GameThread {
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	int32 TotalNumAttributes = 0;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);
	bool NeedsRealloc = false;

	/** A binding to the user ptr we're reading the RT from (if we are). */
	FNiagaraParameterDirectBinding<UObject*> RTUserParamBinding;

	UTextureRenderTargetVolume* TargetTexture = nullptr;
	TArray<FNiagaraVariableBase> Vars;
	TArray<uint32> Offsets;
};

struct FGrid3DCollectionRWInstanceData_RenderThread {
	FIntVector NumCells = FIntVector::ZeroValue;
	FIntVector NumTiles = FIntVector::ZeroValue;
	int32 TotalNumAttributes = 0;
	FVector CellSize = FVector::ZeroVector;
	FVector WorldBBoxSize = FVector::ZeroVector;
	EPixelFormat PixelFormat = EPixelFormat::PF_R32_FLOAT;
	TArray<int32> AttributeIndices;

	TArray<FName> Vars;
	TArray<int32> VarComponents;
	TArray<uint32> Offsets;
	bool bPreviewGrid = false;
	FIntVector4 PreviewAttribute = FIntVector4(INDEX_NONE, INDEX_NONE, INDEX_NONE, INDEX_NONE);

	TArray<TUniquePtr<FGrid3DBuffer>> Buffers;
	FGrid3DBuffer* CurrentData = nullptr;
	FGrid3DBuffer* DestinationData = nullptr;

	FReadBuffer PerAttributeData;

	FTextureRHIRef RenderTargetToCopyTo;
};

class NIAGARA_API UNiagaraDataInterfaceGrid3D : public UNiagaraDataInterfaceRWBase {
	UPROPERTY(EditAnywhere, Category = "Grid")
	FIntVector NumCells;

	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	float CellSize;

	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	int32 NumCellsMaxAxis;

	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	ESetResolutionMethod SetResolutionMethod;
	
	UPROPERTY(EditAnywhere, Category = "Deprecated", AdvancedDisplay)
	FVector WorldBBoxSize;
}

class NIAGARA_API UNiagaraDataInterfaceGrid3DCollection : public UNiagaraDataInterfaceGrid3D {
	UPROPERTY(EditAnywhere, Category = "Grid")
	int32 NumAttributes;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection")
	FNiagaraUserParameterBinding RenderTargetUserParameter;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bOverrideFormat"))
	ENiagaraGpuBufferFormat OverrideBufferFormat;

	UPROPERTY(EditAnywhere, Category = "Grid3DCollection", meta = (EditCondition = "bPreviewGrid", ToolTip = "When enabled allows you to preview the grid in a debug display") )
	FName PreviewAttribute = NAME_None;

	virtual int32 PerInstanceDataSize()const override { return sizeof(FGrid3DCollectionRWInstanceData_GameThread); }
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPostSimulateTick() const override { return true; }

	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_GameThread*> SystemInstancesToProxyData_GT;
};

struct FNiagaraDataInterfaceProxyGrid3DCollectionProxy : public FNiagaraDataInterfaceProxyRW {
	TMap<FNiagaraSystemInstanceID, FGrid3DCollectionRWInstanceData_RenderThread> SystemInstancesToProxyData_RT;
};


bool UNiagaraDataInterfaceGrid3DCollection::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
	FGrid3DCollectionRWInstanceData_GameThread* InstanceData = new (PerInstanceData) 
		FGrid3DCollectionRWInstanceData_GameThread();
	SystemInstancesToProxyData_GT.Emplace(SystemInstance->GetId(), InstanceData);

	int32 NumAttribChannelsFound = 0;
	FindAttributes(InstanceData->Vars, InstanceData->Offsets, NumAttribChannelsFound);
	NumAttribChannelsFound = NumAttributes + NumAttribChannelsFound;
	
	...

	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* RT_Proxy = GetProxyAs<FNiagaraDataInterfaceProxyGrid3DCollectionProxy>();
	ENQUEUE_RENDER_COMMAND(FUpdateData)( 
		[RT_Proxy, 
		RT_Resource=InstanceData->TargetTexture ? InstanceData->TargetTexture->Resource : nullptr, 
		InstanceID = SystemInstance->GetId(), 
		RT_InstanceData=*InstanceData, 
		RT_OutputShaderStages=OutputShaderStages, 
		RT_IterationShaderStages= IterationShaderStages] (FRHICommandListImmediate& RHICmdList)
	{
		FGrid3DCollectionRWInstanceData_RenderThread* TargetData = &RT_Proxy->SystemInstancesToProxyData_RT.Add(InstanceID);
		...
	});
}
//-----------------------InitSystem-----------------------------//


/*
ProxyData->TotalNumAttributes = 6;
ProxyData->NumTiles.X = 4;
ProxyData->NumTiles.Y = 2;
ProxyData->NumTiles.Z = 1;
=>
iAttribute |-> AttributeTileIndex
		 0 |-> 0,0,0
		 1 |-> 1,0,0
		 2 |-> 2,0,0
		 3 |-> 3,0,0
		 4 |-> 0,1,0
		 5 |-> 1,1,0
*/

void FNiagaraDataInterfaceProxyGrid3DCollectionProxy::PreStage(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceStageArgs& Context)
{
	FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);

	if (ProxyData->PerAttributeData.NumBytes == 0)
	{
		int32 MaxDim = 2048;
		int32 MaxTilesX = floor(MaxDim / ProxyData->NumCells.X);
		int32 MaxTilesY = floor(MaxDim / ProxyData->NumCells.Y);
		int32 MaxTilesZ = floor(MaxDim / ProxyData->NumCells.Z);
		int32 MaxAttributes = MaxTilesX * MaxTilesY * MaxTilesZ;

		int32 NumTilesX = Min(MaxTilesX, ProxyData->TotalNumAttributes);
		int32 NumTilesY = Min(MaxTilesY, ceil(1.0 * ProxyData->TotalNumAttributes / NumTilesX));
		int32 NumTilesZ = Min(MaxTilesZ, ceil(1.0 * ProxyData->TotalNumAttributes / (NumTilesX * NumTilesY)));

		ProxyData->NumTiles.X = NumTilesX;
		ProxyData->NumTiles.Y = NumTilesY;
		ProxyData->NumTiles.Z = NumTilesZ;

		TResourceArray<FVector4> PerAttributeData;
		PerAttributeData.AddUninitialized((ProxyData->TotalNumAttributes * 2) + 1);
		for (int32 iAttribute = 0; iAttribute < ProxyData->TotalNumAttributes; ++iAttribute)
		{
			const FIntVector AttributeTileIndex(
				iAttribute % NumTiles.X, 
				(iAttribute / NumTiles.X) % NumTiles.Y, 
				iAttribute / (NumTiles.X * NumTiles.Y) );
		
			PerAttributeData[iAttribute] = FVector4(
				AttributeTileIndex.X * ProxyData->NumCells.X,
				AttributeTileIndex.Y * ProxyData->NumCells.Y,
				AttributeTileIndex.Z * ProxyData->NumCells.Z,
				0
			);

			PerAttributeData[iAttribute + ProxyData->TotalNumAttributes] = FVector4(
				(1.0f / ProxyData->NumTiles.X) * float(AttributeTileIndex.X),
				(1.0f / ProxyData->NumTiles.Y) * float(AttributeTileIndex.Y),
				(1.0f / ProxyData->NumTiles.Z) * float(AttributeTileIndex.Z),
				0.0f
			);
		}

		PerAttributeData[ProxyData->TotalNumAttributes * 2] = FVector4(65535, 65535, 65535, 65535);

		ProxyData->PerAttributeData.Initialize(
			TEXT("Grid3D::PerAttributeData"), 
			sizeof(FVector4), 
			PerAttributeData.Num(), 
			EPixelFormat::PF_A32B32G32R32F, 
			BUF_Static, 
			&PerAttributeData );
	}

	if (Context.IsOutputStage)
	{
		...
	}
}


struct FNiagaraDataInterfaceParametersCS_Grid3DCollection : public FNiagaraDataInterfaceParametersCS {
	LAYOUT_FIELD(FShaderParameter, NumAttributesParam);
	LAYOUT_FIELD(FShaderParameter, UnitToUVParam);
	LAYOUT_FIELD(FShaderParameter, NumCellsParam);
	LAYOUT_FIELD(FShaderParameter, NumTilesParam);
	LAYOUT_FIELD(FShaderParameter, OneOverNumTilesParam);
	LAYOUT_FIELD(FShaderParameter, UnitClampMinParam);
	LAYOUT_FIELD(FShaderParameter, UnitClampMaxParam);
	LAYOUT_FIELD(FShaderParameter, CellSizeParam);
	LAYOUT_FIELD(FShaderParameter, WorldBBoxSizeParam);
	LAYOUT_FIELD(FShaderResourceParameter, GridParam);
	LAYOUT_FIELD(FRWShaderParameter, OutputGridParam);
	LAYOUT_FIELD(FShaderParameter, AttributeIndicesParam);
	LAYOUT_FIELD(FShaderResourceParameter, PerAttributeDataParam);
	LAYOUT_FIELD(FShaderResourceParameter, SamplerParam);
	LAYOUT_FIELD(TMemoryImageArray<FName>, AttributeNames);
	LAYOUT_FIELD(TMemoryImageArray<uint32>, AttributeChannelCount);
};

// $BEGIN$ : IMPLEMENT_NIAGARA_DI_PARAMETER(UNiagaraDataInterfaceGrid3DCollection, FNiagaraDataInterfaceParametersCS_Grid3DCollection);
FNiagaraDataInterfaceParametersCS* UNiagaraDataInterfaceGrid3DCollection::CreateComputeParameters() const 
{ 
	return new FNiagaraDataInterfaceParametersCS_Grid3DCollection(); 
} 
const FTypeLayoutDesc* UNiagaraDataInterfaceGrid3DCollection::GetComputeParametersTypeDesc() const 
{ 
	return &StaticGetTypeLayoutDesc<FNiagaraDataInterfaceParametersCS_Grid3DCollection>(); 
}
void UNiagaraDataInterfaceGrid3DCollection::BindParameters(FNiagaraDataInterfaceParametersCS* Base, const FNiagaraDataInterfaceGPUParamInfo& ParameterInfo, const class FShaderParameterMap& ParameterMap) 
{ 
	static_cast<FNiagaraDataInterfaceParametersCS_Grid3DCollection*>(Base)->Bind(ParameterInfo, ParameterMap); 
} 
void UNiagaraDataInterfaceGrid3DCollection::SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
{ 
	static_cast<const FNiagaraDataInterfaceParametersCS_Grid3DCollection*>(Base)->Set(RHICmdList, Context); 
}
void UNiagaraDataInterfaceGrid3DCollection::UnsetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
{ 
	static_cast<const FNiagaraDataInterfaceParametersCS_Grid3DCollection*>(Base)->Unset(RHICmdList, Context); 
}
// $END$


void NiagaraEmitterInstanceBatcher::SetDataInterfaceParameters(
	const TArray<FNiagaraDataInterfaceProxy*>& DataInterfaceProxies, 
	const FNiagaraShaderRef& Shader, 
	FRHICommandList& RHICmdList, 
	const FNiagaraComputeInstanceData* Instance, 
	const FNiagaraGPUSystemTick& Tick, 
	uint32 SimulationStageIndex) const
{
	const FNiagaraShaderMapPointerTable& PointerTable = Shader.GetPointerTable();

	uint32 InterfaceIndex = 0;
	for (FNiagaraDataInterfaceProxy* Interface : DataInterfaceProxies)
	{
		const FNiagaraDataInterfaceParamRef& DIParam = Shader->GetDIParameters()[InterfaceIndex++];
		if (DIParam.Parameters.IsValid())
		{
			FNiagaraDataInterfaceSetArgs Context(
				Interface, 
				Tick.SystemInstanceID, 
				this, 
				Shader, 
				Instance, 
				SimulationStageIndex, 
				Instance->IsOutputStage(Interface, SimulationStageIndex), 
				Instance->IsIterationStage(Interface, SimulationStageIndex) );
			DIParam.DIType.Get(PointerTable.DITypes)->SetParameters(DIParam.Parameters.Get(), RHICmdList, Context);
		}
	}
}

void UNiagaraDataInterfaceGrid3DCollection::SetParameters(const FNiagaraDataInterfaceParametersCS* Base, FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const 
{ 
	static_cast<const FNiagaraDataInterfaceParametersCS_Grid3DCollection*>(Base)->Set(RHICmdList, Context); 
}

struct FNiagaraDataInterfaceArgs {
	FNiagaraDataInterfaceProxy*	DataInterface;
	FNiagaraSystemInstanceID SystemInstanceID;
	const NiagaraEmitterInstanceBatcher* Batcher;
};
struct FNiagaraDataInterfaceSetArgs : public FNiagaraDataInterfaceArgs {
	FShaderReference Shader;
	const FNiagaraComputeInstanceData* ComputeInstanceData;
	uint32 SimulationStageIndex;
	bool IsOutputStage;
	bool IsIterationStage;
};

void FNiagaraDataInterfaceParametersCS_Grid3DCollection::Set(FRHICommandList& RHICmdList, const FNiagaraDataInterfaceSetArgs& Context) const
{
	FRHIComputeShader* ComputeShaderRHI = Context.Shader.GetComputeShader();
	FNiagaraDataInterfaceProxyGrid3DCollectionProxy* VFDI = static_cast<FNiagaraDataInterfaceProxyGrid3DCollectionProxy*>(Context.DataInterface);
	
	FGrid3DCollectionRWInstanceData_RenderThread* ProxyData = VFDI->SystemInstancesToProxyData_RT.Find(Context.SystemInstanceID);
	
	SetShaderValue(RHICmdList, ComputeShaderRHI, NumCellsParam, ProxyData->NumCells);	
	SetShaderValue(RHICmdList, ComputeShaderRHI, NumTilesParam, ProxyData->NumTiles);
	SetShaderValue(RHICmdList, ComputeShaderRHI, OneOverNumTilesParam, FVector(1.0f) / FVector(ProxyData->NumTiles));

	const FVector HalfPixelOffset = FVector(0.5f / ProxyData->NumCells.X, 0.5f / ProxyData->NumCells.Y, 0.5f / ProxyData->NumCells.Z);
	SetShaderValue(RHICmdList, ComputeShaderRHI, UnitClampMinParam, HalfPixelOffset);
	SetShaderValue(RHICmdList, ComputeShaderRHI, UnitClampMaxParam, FVector::OneVector - HalfPixelOffset);

	if (ProxyData->AttributeIndices.Num() == 0 && AttributeNames.Num() > 0)
	{
		int NumAttrIndices = Align(AttributeNames.Num(), 4);
		ProxyData->AttributeIndices.SetNumZeroed(NumAttrIndices);

		// TODO handle mismatched types!
		for (int32 i = 0; i < AttributeNames.Num(); i++)
		{
			int32 FoundIdx = ProxyData->Vars.Find(AttributeNames[i]);
			check(AttributeNames.Num() == AttributeChannelCount.Num());
			check(ProxyData->Offsets.Num() == ProxyData->VarComponents.Num());
			check(ProxyData->Offsets.Num() == ProxyData->Vars.Num());
			if (ProxyData->Offsets.IsValidIndex(FoundIdx) && AttributeChannelCount[i] == ProxyData->VarComponents[FoundIdx])
			{
				ProxyData->AttributeIndices[i] = ProxyData->Offsets[FoundIdx];
			}
			else
			{
				ProxyData->AttributeIndices[i] = -1; // We may need to protect against this in the hlsl as this might underflow an array lookup if used incorrectly.
			}
		}
	}

	SetShaderValue(RHICmdList, ComputeShaderRHI, NumAttributesParam, ProxyData->TotalNumAttributes);
	SetShaderValue(RHICmdList, ComputeShaderRHI, UnitToUVParam, FVector(1.0f) / FVector(ProxyData->NumCells));
	SetShaderValue(RHICmdList, ComputeShaderRHI, CellSizeParam, ProxyData->CellSize);			
	SetShaderValue(RHICmdList, ComputeShaderRHI, WorldBBoxSizeParam, ProxyData->WorldBBoxSize);
	SetShaderValueArray(RHICmdList, ComputeShaderRHI, AttributeIndicesParam, ProxyData->AttributeIndices.GetData(), ProxyData->AttributeIndices.Num());
	SetSRVParameter(RHICmdList, ComputeShaderRHI, PerAttributeDataParam, ProxyData->PerAttributeData.SRV);

	FRHISamplerState *SamplerState = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	SetSamplerParameter(RHICmdList, ComputeShaderRHI, SamplerParam, SamplerState);

	if (GridParam.IsBound())
	{
		FRHIShaderResourceView* InputGridBuffer;
		if (ProxyData->CurrentData != nullptr)
		{
			InputGridBuffer = ProxyData->CurrentData->GridBuffer.SRV;
		}
		else
		{
			InputGridBuffer = FNiagaraRenderer::GetDummyTextureReadBuffer2D();
		}
		SetSRVParameter(RHICmdList, ComputeShaderRHI, GridParam, InputGridBuffer);
	}

	if ( OutputGridParam.IsUAVBound() )
	{
		FRHIUnorderedAccessView* OutputGridUAV;
		if (Context.IsOutputStage && ProxyData->DestinationData != nullptr)
		{
			OutputGridUAV = ProxyData->DestinationData->GridBuffer.UAV;
		}
		else
		{
			OutputGridUAV = Context.Batcher->GetEmptyRWTextureFromPool(RHICmdList, PF_R32_FLOAT);
		}
		RHICmdList.SetUAVParameter(ComputeShaderRHI, OutputGridParam.GetUAVIndex(), OutputGridUAV);
	}
}




