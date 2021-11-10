


enum class ENiagaraScriptUsage : uint8
{
	Function,
	Module,
	DynamicInput,
	ParticleSpawnScript,
	ParticleSpawnScriptInterpolated UMETA(Hidden),
	ParticleUpdateScript,
	ParticleEventScript ,
	ParticleSimulationStageScript,
	ParticleGPUComputeScript UMETA(Hidden),
	EmitterSpawnScript,
	EmitterUpdateScript ,
	SystemSpawnScript ,
	SystemUpdateScript,
};

class UNiagaraScript : public UNiagaraScriptBase {
	ENiagaraScriptUsage Usage;

	TUniquePtr<FNiagaraShaderScript> ScriptResource;
	FNiagaraVMExecutableData CachedScriptVM;
}

class FNiagaraShaderScript {
	UNiagaraScriptBase* BaseVMScript;
	FString SourceName;
	FString HlslOutput;
}

class NIAGARA_API UNiagaraSystem : public UFXSystemAsset {
	TArray< TObjectPtr<UNiagaraScript> > ScratchPadScripts;
	TObjectPtr<UNiagaraScript> SystemSpawnScript;
	TObjectPtr<UNiagaraScript> SystemUpdateScript;

	TArray<FNiagaraSystemCompileRequest> ActiveCompilations;
}

struct FNiagaraSystemCompileRequest
{
	double StartTime = 0.0;
	TArray<TObjectPtr<UObject>> RootObjects;
	TArray<FEmitterCompiledScriptPair> EmitterCompiledScriptPairs;
	TMap<UNiagaraScript*, TSharedPtr<FNiagaraCompileRequestDataBase> > MappedData;
	bool bIsValid = true;
	bool bForced = false;
};

struct FEmitterCompiledScriptPair
{
	bool bResultsReady;
	UNiagaraEmitter* Emitter;
	UNiagaraScript* CompiledScript;
	uint32 PendingJobID = INDEX_NONE; // this is the ID for any active shader compiler worker job
	FNiagaraVMExecutableDataId CompileId;
	TSharedPtr<FNiagaraVMExecutableData> CompileResults;
	int32 ParentIndex = INDEX_NONE;
};




bool UNiagaraSystem::RequestCompile(bool bForce, FNiagaraSystemUpdateContext* OptionalUpdateContext)
{
	if (ActiveCompilations.Num() > 0)
	{
		QueryCompileComplete(false, true);
	}
}

bool UNiagaraSystem::QueryCompileComplete(bool bWait, bool bDoPost, bool bDoNotApply)
{
	for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[0].EmitterCompiledScriptPairs)
	{
		if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingJobID || EmitterCompiledScriptPair.bResultsReady)
		{
			continue;
		}
		EmitterCompiledScriptPair.bResultsReady = ProcessCompilationResult(EmitterCompiledScriptPair, bWait, bDoNotApply);
	}

	for (FEmitterCompiledScriptPair& EmitterCompiledScriptPair : ActiveCompilations[0].EmitterCompiledScriptPairs)
	{
		if ((uint32)INDEX_NONE == EmitterCompiledScriptPair.PendingJobID)
		{
			continue;
		}
		TSharedPtr<FNiagaraVMExecutableData> ExeData = EmitterCompiledScriptPair.CompileResults;
		TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> PrecompData = ActiveCompilations[ActiveCompileIdx].MappedData.FindChecked(EmitterCompiledScriptPair.CompiledScript);
		EmitterCompiledScriptPair.CompiledScript->SetVMCompilationResults(EmitterCompiledScriptPair.CompileId, *ExeData, PrecompData.Get());
	}

	...
}

bool UNiagaraSystem::ProcessCompilationResult(FEmitterCompiledScriptPair& ScriptPair, bool bWait, bool bDoNotApply)
{
	INiagaraModule& NiagaraModule = FModuleManager::Get().LoadModuleChecked<INiagaraModule>(TEXT("Niagara"));
	TSharedPtr<FNiagaraVMExecutableData> ExeData = NiagaraModule.GetCompileJobResult(ScriptPair.PendingJobID, bWait);
	ScriptPair.CompileResults = ExeData;
}

TSharedPtr<FNiagaraVMExecutableData> FNiagaraEditorModule::GetCompilationResult(int32 JobID, bool bWait)
{
	TSharedPtr<FHlslNiagaraCompiler> Compiler = *ActiveCompilations.Find(JobID);
	FNiagaraCompileResults Results = Compiler->GetCompileResult(JobID, bWait);
	ActiveCompilations.Remove(JobID);
	return Results.Data;
}

FNiagaraCompileResults FHlslNiagaraCompiler::GetCompileResult(int32 JobID, bool bWait /*= false*/)
{
	FNiagaraCompileResults Results = CompilationJob->CompileResults;
	Results.bVMSucceeded = true;
	*Results.Data = CompilationJob->TranslatorOutput.ScriptData;
	Results.Data-> = ;
	CompilationJob.Reset();
	return Results;
}

void UNiagaraScript::SetVMCompilationResults(const FNiagaraVMExecutableDataId& InCompileId, FNiagaraVMExecutableData& InScriptVM, FNiagaraCompileRequestDataBase* InRequestData)
{
	CachedScriptVMId = InCompileId;
	CachedScriptVM = InScriptVM;
	ScriptResource->Invalidate();

	if (Usage == ENiagaraScriptUsage::ParticleGPUComputeScript)
	{
		if (CachedScriptVMId.CompilerVersionID.IsValid() && CachedScriptVMId.BaseScriptCompileHash.IsValid())
		{
			CacheResourceShadersForRendering(false, true);
		}
	}
}