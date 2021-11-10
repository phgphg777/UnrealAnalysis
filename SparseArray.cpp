/** The result of a sparse array allocation. */
struct FSparseArrayAllocationInfo
{
	int32 Index;
	void* Pointer;
};

/** Allocated elements are overlapped with free element info in the element list. */
template<typename ElementType>
union TSparseArrayElementOrFreeListLink
{
	ElementType ElementData;

	struct {
		int32 PrevFreeIndex;
		int32 NextFreeIndex;
	};
};

template<typename ElementType,typename Allocator /*= FDefaultSparseArrayAllocator */>
class TSparseArray
{
	typedef TSparseArrayElementOrFreeListLink<TAlignedBytes<sizeof(ElementType), alignof(ElementType)>> FElementOrFreeListLink;
	typedef TArray<FElementOrFreeListLink, Allocator::ElementAllocator> DataType;
	DataType Data;

	typedef TBitArray<Allocator::BitArrayAllocator> AllocationBitArrayType;
	AllocationBitArrayType AllocationFlags;

	/** The index of an unallocated element in the array that currently contains the head of the linked list of free elements. */
	int32 FirstFreeIndex = -1;

	/** The number of elements in the free list. */
	int32 NumFreeIndices = 0;


	int32 Add(const ElementType& Element)
	{
		FSparseArrayAllocationInfo Allocation = AddUninitialized();
		new(Allocation) ElementType(Element);
		return Allocation.Index;
	}

	FSparseArrayAllocationInfo AddUninitialized()
	{
		int32 Index;
		if(NumFreeIndices)
		{
			// Remove and use the first index from the list of free elements.
			Index = FirstFreeIndex;
			FirstFreeIndex = GetData(FirstFreeIndex).NextFreeIndex;
			--NumFreeIndices;
			if(NumFreeIndices)
			{
				GetData(FirstFreeIndex).PrevFreeIndex = -1;
			}
		}
		else
		{
			// Add a new element.
			Index = Data.AddUninitialized(1);
			AllocationFlags.Add(false);
		}

		return AllocateIndex(Index);
	}

}
