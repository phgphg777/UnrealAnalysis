// CommonRenderResources.h, cpp
class FScreenRectangleVertexBuffer : public FVertexBuffer {
	void InitRHI()
	{
		TResourceArray<FFilterVertex, 0> Vertices;
		Vertices.SetNumUninitialized(6);
		Vertices[0].Position = FVector4(1, 1, 0, 1); Vertices[0].UV = FVector2D(1, 1);
		Vertices[1].Position = FVector4(0, 1, 0, 1); Vertices[1].UV = FVector2D(0, 1);
		Vertices[2].Position = FVector4(1, 0, 0, 1); Vertices[2].UV = FVector2D(1, 0);
		Vertices[3].Position = FVector4(0, 0, 0, 1); Vertices[3].UV = FVector2D(0, 0);
		//The final two vertices are used for the triangle optimization (a single triangle spans the entire viewport )
		Vertices[4].Position = FVector4(-1, 1, 0, 1); Vertices[4].UV = FVector2D(-1, 1);
		Vertices[5].Position = FVector4(1, -1, 0, 1); Vertices[5].UV = FVector2D(1, -1);

		// Create vertex buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&Vertices);
		VertexBufferRHI = RHICreateVertexBuffer(Vertices.GetResourceDataSize(), BUF_Static, CreateInfo);
	}
};
TGlobalResource<FScreenRectangleVertexBuffer> GScreenRectangleVertexBuffer;

class FScreenRectangleIndexBuffer : public FIndexBuffer {
	void InitRHI()
	{
		// Indices 0 - 5 are used for rendering a quad. Indices 6 - 8 are used for triangle optimization.
		const uint16 Indices[] = { 0, 1, 2, 2, 1, 3, 0, 4, 5 };

		TResourceArray<uint16, 0> IndexBuffer;
		uint32 NumIndices = UE_ARRAY_COUNT(Indices);
		IndexBuffer.AddUninitialized(NumIndices);
		FMemory::Memcpy(IndexBuffer.GetData(), Indices, NumIndices * sizeof(uint16));

		// Create index buffer. Fill buffer with initial data upon creation
		FRHIResourceCreateInfo CreateInfo(&IndexBuffer);
		IndexBufferRHI = RHICreateIndexBuffer(sizeof(uint16), IndexBuffer.GetResourceDataSize(), BUF_Static, CreateInfo);
	}
};
TGlobalResource<FScreenRectangleIndexBuffer> GScreenRectangleIndexBuffer;

class FFilterVertexDeclaration : public FRenderResource {	
	void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		Elements.Add( FVertexElement(0, STRUCT_OFFSET(FFilterVertex, Position), VET_Float4, 0, sizeof(FFilterVertex)) ); // StreamIndex, Offset, Type, AttributeIndex, Stride
		Elements.Add( FVertexElement(0, STRUCT_OFFSET(FFilterVertex, UV), VET_Float2, 1, sizeof(FFilterVertex)) );
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	FVertexDeclarationRHIRef VertexDeclarationRHI;
};
TGlobalResource<FFilterVertexDeclaration> GFilterVertexDeclaration;

//RenderUtils.cpp
class FVector4VertexDeclaration : public FRenderResource {
	void InitRHI()
	{
		FVertexDeclarationElementList Elements;
		Elements.Add(FVertexElement(0, 0, VET_Float4, 0, sizeof(FVector4)));
		VertexDeclarationRHI = PipelineStateCache::GetOrCreateVertexDeclaration(Elements);
	}
	FVertexDeclarationRHIRef VertexDeclarationRHI;
}; TGlobalResource<FVector4VertexDeclaration> GVector4VertexDeclaration;



BEGIN_GLOBAL_SHADER_PARAMETER_STRUCT( FDrawRectangleParameters, RENDERER_API)
	SHADER_PARAMETER( FVector4, PosScaleBias )
	SHADER_PARAMETER( FVector4, UVScaleBias )
	SHADER_PARAMETER( FVector4, InvTargetSizeAndTextureSize )
END_GLOBAL_SHADER_PARAMETER_STRUCT()
IMPLEMENT_GLOBAL_SHADER_PARAMETER_STRUCT(FDrawRectangleParameters, "DRP");

DrawRectangle(RHICmdList,
	0, 0, TargetSizeX, TargetSizeY,
	0, 0, View.ViewRect.Width(), View.ViewRect.Height(),
	FIntPoint(TargetSizeX, TargetSizeY),
	FSceneRenderTargets::Get(RHICmdList).GetBufferSizeXY(),
	VertexShader);

void DrawRectangle(FRHICommandList& RHICmdList,
	float X, float Y, float SizeX, float SizeY,
	float U, float V, float SizeU, float SizeV,
	FIntPoint TargetSize,
	FIntPoint TextureSize,
	const TShaderRef<FShader>& VertexShader,
	EDrawRectangleFlags Flags = EDRF_Default)
{
	FDrawRectangleParameters DRP;
	DRP.PosScaleBias = FVector4(SizeX, SizeY, X, Y);
	DRP.UVScaleBias = FVector4(SizeU, SizeV, U, V);
	DRP.InvTargetSizeAndTextureSize = 1.0f / FVector4(TargetSize, TextureSize);
	SetUniformBufferParameterImmediate(
		RHICmdList, 
		VertexShader.GetVertexShader(), 
		VertexShader->GetUniformBufferParameter<FDrawRectangleParameters>(), 
		DRP);

	if (Flags == EDRF_UseTriangleOptimization && X == 0.0f && Y == 0.0f)
	{
		RHICmdList.SetStreamSource(0, GScreenRectangleVertexBuffer.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(GScreenRectangleIndexBuffer.IndexBufferRHI, 0, 0, // IndexBuffer, BaseVertexIndex, FirstInstance, 
			3, 6, 1, 1);// NumVertices, StartIndex, NumPrimitives, NumInstances
	}
	else if(Flags == EDRF_Default)
	{
		RHICmdList.SetStreamSource(0, GScreenRectangleVertexBuffer.VertexBufferRHI, 0);
		RHICmdList.DrawIndexedPrimitive(GScreenRectangleIndexBuffer.IndexBufferRHI, 0, 0, 
			4, 0, 2, 1);
	}
}

// Common.ush
void DrawRectangle(
	in float4 InPosition,
	in float2 InTexCoord,
	out float4 OutPosition,
	out float2 OutTexCoord)
{
	float2 TargetSize = DRP.PosScaleBias.xy;
	float2 TargetOrigin = DRP.PosScaleBias.zw;
	float2 InvTargetBufferSize = DRP.InvTargetSizeAndTextureSize.xy;
	float2 TextureSize = DRP.UVScaleBias.xy;
	float2 TextureOrigin = DRP.UVScaleBias.zw;
	float2 InvTextureBufferSize = DRP.InvTargetSizeAndTextureSize.zw;

	OutPosition = InPosition;
	OutPosition.xy = (TargetOrigin + (InPosition.xy * TargetSize)) * InvTargetBufferSize;
	OutPosition.xy = (OutPosition.xy * 2.0f - 1.0f) * float2( 1, -1 );
	OutTexCoord.xy = (TextureOrigin + (InTexCoord.xy * TextureSize)) * InvTextureBufferSize;
}

// DistanceFieldAmbientOcclusion.cpp
class FPostProcessVS : public FScreenPassVS {}
class FScreenPassVS : public FGlobalShader {
public:
	DECLARE_GLOBAL_SHADER(FScreenPassVS);
}
IMPLEMENT_GLOBAL_SHADER(FScreenPassVS, "/Engine/Private/ScreenPass.usf", "ScreenPassVS", SF_Vertex);

void ScreenPassVS(
	in float4 InPosition : ATTRIBUTE0,
	in float2 InTexCoord : ATTRIBUTE1,
	out noperspective float4 OutUVAndScreenPos : TEXCOORD0,
	out float4 OutPosition : SV_POSITION)
{
	DrawRectangle( InPosition, InTexCoord, OutPosition, OutUVAndScreenPos.xy );
	OutUVAndScreenPos.zw = OutPosition.xy;
}


// ShadowDepthRendering.cpp
class FScreenVS : public FGlobalShader {
	DECLARE_EXPORTED_SHADER_TYPE(FScreenVS,Global,ENGINE_API);
public:
}
IMPLEMENT_SHADER_TYPE(,FScreenVS,TEXT("/Engine/Private/ScreenVertexShader.usf"),TEXT("Main"),SF_Vertex);

struct FScreenVertexOutput {
	noperspective MaterialFloat2 UV : TEXCOORD0;
	float4 Position : SV_POSITION;
};

void Main(
	float2 InPosition : ATTRIBUTE0,
	float2 InUV       : ATTRIBUTE1,
	out FScreenVertexOutput Output)
{
	DrawRectangle( float4(InPosition, 0, 1), InUV, Output.Position, Output.UV );
}