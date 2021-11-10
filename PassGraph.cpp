

struct FRenderingCompositePassContext
{
	const FViewInfo& View;
	FRenderingCompositionGraph Graph;
	FRHICommandListImmediate& RHICmdList;
	
	bool bWasProcessed;
	
	FRenderingCompositePass* Pass;
	
	TShaderMap<FGlobalShaderType>* ShaderMap;
}

class FPostprocessContext
{
	const FViewInfo& View;
	FRenderingCompositionGraph& Graph;
	FRHICommandListImmediate& RHICmdList;
	
	FRenderingCompositePass* SceneColor;
	FRenderingCompositePass* SceneDepth;

	FRenderingCompositeOutputRef FinalOutput;
};


class FRenderingCompositionGraph
{
	TArray<FRenderingCompositePass*> Nodes;
};

struct FRenderingCompositeOutputRef
{
	FRenderingCompositePass* Source;
	EPassOutputId PassOutputId;
}

struct FRenderingCompositeOutput
{
	FPooledRenderTargetDesc RenderTargetDesc; 
	TRefCountPtr<IPooledRenderTarget> PooledRenderTarget;
}

struct FRenderingCompositePass
{
	bool bComputeOutputDescWasCalled;
}

template <uint32 InputCount, uint32 OutputCount>
struct TRenderingCompositePassBase :public FRenderingCompositePass
{
	FRenderingCompositeOutputRef PassInputs[InputCount];
	FRenderingCompositeOutput PassOutputs[OutputCount];
}

class FRCPassPostProcessInput : public TRenderingCompositePassBase<0, 1>
{
	TRefCountPtr<IPooledRenderTarget> Data;
}

class FRCPassPostProcessDeferredDecals : public TRenderingCompositePassBase<1, 1>
{
	EDecalRenderStage CurrentStage;
}




GCompositionLighting.ProcessBeforeBasePass(RHICmdList, View, bDBuffer, SSAOLevels);
{
	FRenderingCompositePassContext CompositeContext( , );
	{
		Graph = FRenderingCompositionGraph();
	}
	FRenderingCompositionGraph& Graph = CompositeContext.Graph;

	FPostprocessContext Context( , , );
	{
		SceneColor = nullptr;
		SceneDepth = Graph.RegisterPass(new FRCPassPostProcessInput(SceneContext.SceneDepthZ));
		{
			Data = SceneContext.SceneDepthZ;
		}

		FinalOutput = FRenderingCompositeOutputRef(SceneColor, ePId_Output0);
	}
	FRenderingCompositeOutputRef FinalOutput = Context.FinalOutput;


	FRenderingCompositePass* Pass1 
		= Graph.RegisterPass(new FRCPassPostProcessDeferredDecals(DRS_BeforeBasePass));
	{
		CurrentStage = DRS_BeforeBasePass;
	}

	Pass1->SetInput(ePId_Input0, FinalOutput);

	FinalOutput = FRenderingCompositeOutputRef(Pass1, ePId_Output0);


	CompositeContext.Process(FinalOutput.GetPass(), TEXT("Composition_BeforeBasePass"));
	[
		bWasProcessed = true;

		Graph.RecursivelyGatherDependencies( FinalOutput.GetPass() );
		{
			FRenderingCompositePass* Pass = FinalOutput.GetPass();

			Pass->bComputeOutputDescWasCalled ? return : = true;

			for ( FRenderingCompositeOutputRef& Input : Pass->(PassInput + AdditionalDependencies) )
			{
				FRenderingCompositePass* PrePass = Input.GetPass();

				if(PrePass)
				{
					Input.GetOutput()->AddDependency;
					RecursivelyGatherDependencies( PrePass );
				}
			}
			-------------------------------------------------------------------------------
			-------------------------------------------------------------------------------

			for( FRenderingCompositeOutput& Output : Pass->OutPut)
			{
				Output.RenderTargetDesc = Pass->ComputeOutputDesc(OutputId);
			}
		}

		FRenderingCompositeOutputRef InOutputRef = FRenderingCompositeOutputRef( FinalOutput.GetPass(), ePId_Output0 );
		Graph.RecursivelyProcess( InOutputRef, CompositeContext );
		{
			FRenderingCompositePass* Pass = InOutputRef.GetPass();
			FRenderingCompositeOutput* Output = InOutputRef.GetOutput();

			Pass->bProcessWasCalled ? return : = true;

			for ( FRenderingCompositeOutputRef& Input : Pass->(PassInput + AdditionalDependencies) )
			{
				FRenderingCompositePass* PrePass = Input.GetPass();

				if (PrePass)
				{
					CompositeContext.Pass = Pass;
					RecursivelyProcess( Input, CompositeContext );
				}
			}
			-------------------------------------------------------------------------------
			-------------------------------------------------------------------------------

			Context.Pass = Pass;

			Pass->Process(Context);
		}

	] // end process
}