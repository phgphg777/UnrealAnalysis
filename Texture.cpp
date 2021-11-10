//-----------------------------RHI Module-----------------------------//
class RHI_API FRHITexture : public FRHIResource {
	FClearValueBinding ClearValue;
	uint32 NumMips;
	uint32 NumSamples;
	EPixelFormat Format;
	uint32 Flags;
	FLastRenderTimeContainer& LastRenderTime;
	FLastRenderTimeContainer DefaultLastRenderTime;	
	FName TextureName;
}
class RHI_API FRHITexture2D : public FRHITexture {
	uint32 SizeX;
	uint32 SizeY;
}
class RHI_API FRHITexture2DArray : public FRHITexture {}
class RHI_API FRHITexture3D : public FRHITexture {}
class RHI_API FRHITextureCube : public FRHITexture {
	uint32 Size;
}
class RHI_API FRHITextureReference : public FRHITexture {
	FTextureRHIRef ReferencedTexture;
}

typedef TRefCountPtr<FRHISamplerState> FSamplerStateRHIRef;
typedef TRefCountPtr<FRHITexture> FTextureRHIRef;
typedef TRefCountPtr<FRHITexture2D> FTexture2DRHIRef;
typedef TRefCountPtr<FRHITexture2DArray> FTexture2DArrayRHIRef;
typedef TRefCountPtr<FRHITexture3D> FTexture3DRHIRef;
typedef TRefCountPtr<FRHITextureCube> FTextureCubeRHIRef;
typedef TRefCountPtr<FRHITextureReference> FTextureReferenceRHIRef;
//-----------------------------RHI Module-----------------------------//


//-----------------------------RENDERCORE Module-----------------------------//
class FTexture : public FRenderResource { 	/** A textures resource. */
	FTextureRHIRef		TextureRHI;/** The texture's RHI resource. */
	FSamplerStateRHIRef SamplerStateRHI;/** The sampler state to use for the texture. */
}

template <int32 R, int32 G, int32 B, int32 A> class FColoredTexture : public FTexture {} /** A solid-colored 1x1 texture. */
template <EPixelFormat PixelFormat> class FBlackVolumeTexture : public FTexture {} /** A class representing a 1x1x1 black volume texture. */
class FBlackArrayTexture : public FTexture {}
class FMipColorTexture : public FTexture {} /** A texture that has a different solid color in each mip-level */
class FSolidColorTextureCube : public FTexture {} /** A solid color cube texture. */
class FBlackCubeArrayTexture : public FTexture {}

class RENDERCORE_API FTextureReference : public FRenderResource { /** A texture reference resource. */
	FTextureReferenceRHIRef	TextureReferenceRHI;	/** The texture reference's RHI resource. */
}
//-----------------------------RENDERCORE Module-----------------------------//


//-----------------------------ENGINE Module-----------------------------//
struct FTextureSource {
	FByteBulkData BulkData;	/** The bulk source data. */
}

class UTexture : public UObject, public IInterface_AssetUserData {	
	FTextureSource Source;
	FTextureResource*	Resource;		/** The texture's resource, can be NULL */
	FTextureReference TextureReference; /** Stable RHI texture reference that refers to the current RHI texture. Note this is manually refcounted! */
}
class UTexture2D : public UTexture {}
class UTexture2DDynamic : public UTexture {}
class UTextureCube : public UTexture {}
class UVolumeTexture : public UTexture {}
class UTextureRenderTarget : public UTexture {}

class FTextureResource : public FTexture {}; /**  The rendering resource which represents a texture. */
class FTexture2DResource : public FTextureResource { /** FTextureResource implementation for streamable 2D textures. */
	const UTexture2D*	Owner;	/** The UTexture2D which this resource represents. */
	FTexture2DRHIRef	Texture2DRHI;	/** 2D texture version of TextureRHI which is used to lock the 2D texture during mip transitions. */
}
class FTexture2DDynamicResource : public FTextureResource {} /** A dynamic 2D texture resource. */
class FTexture2DArrayResource : public FTextureResource {}
class FTextureRenderTargetResource : public FTextureResource, public FRenderTarget, public FDeferredUpdateResource {}
class FTextureCubeResource : public FTextureResource {}
class FTexture3DResource : public FTextureResource {}
class FSkyTextureCubeResource : public FTexture, private FDeferredCleanupInterface {
	int32 Size;
	int32 NumMips;
	EPixelFormat Format;
	FTextureCubeRHIRef TextureCubeRHI;
	int32 NumRefs;
};
class FReflectionTextureCubeResource : public FTexture {}
class FPlanarReflectionRenderTarget : public FTexture, public FRenderTarget {}
//-----------------------------ENGINE Module-----------------------------//


