// Copyright 2017 Phyronnaz

#include "VoxelPrivatePCH.h"
#include "VoxelData.h"
#include "VoxelRender.h"
#include "Components/CapsuleComponent.h"
#include "Engine.h"
#include <forward_list>
#include "FlatWorldGenerator.h"
#include "VoxelInvokerComponent.h"
#include "VoxelModifier.h"
#include "VoxelWorldEditorInterface.h"

DEFINE_LOG_CATEGORY(VoxelLog)

AVoxelWorld::AVoxelWorld()
	: VoxelWorldEditorClass(nullptr)
	, NewDepth(9)
	, DeletionDelay(0.1f)
	, bComputeTransitions(true)
	, bIsCreated(false)
	, FoliageFPS(15)
	, LODUpdateFPS(10)
	, NewVoxelSize(100)
	, MeshThreadCount(4)
	, FoliageThreadCount(4)
	, Render(nullptr)
	, Data(nullptr)
	, InstancedWorldGenerator(nullptr)
	, VoxelWorldEditor(nullptr)
	, bComputeCollisions(false)
{
	PrimaryActorTick.bCanEverTick = true;

	auto TouchCapsule = CreateDefaultSubobject<UCapsuleComponent>(FName("Capsule"));
	TouchCapsule->InitCapsuleSize(0.1f, 0.1f);
	TouchCapsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TouchCapsule->SetCollisionResponseToAllChannels(ECR_Ignore);
	RootComponent = TouchCapsule;

	WorldGenerator = TSubclassOf<UVoxelWorldGenerator>(UFlatWorldGenerator::StaticClass());

	bReplicates = true;
}

AVoxelWorld::~AVoxelWorld()
{
	if (Data)
	{
		delete Data;
	}
	if (Render)
	{
		Render->Destroy();
		delete Render;
	}
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();

	if (!IsCreated())
	{
		CreateWorld();
	}

	bComputeCollisions = true;
}

void AVoxelWorld::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (IsCreated())
	{
		Render->Tick(DeltaTime);
	}
}

#if WITH_EDITOR
bool AVoxelWorld::ShouldTickIfViewportsOnly() const
{
	return true;
}

void AVoxelWorld::PostLoad()
{
	Super::PostLoad();

	if (GetWorld())
	{
		CreateInEditor();
	}
}

#endif

float AVoxelWorld::GetValue(FIntVector Position) const
{
	if (IsInWorld(Position))
	{
		FVoxelMaterial Material;
		float Value;

		Data->BeginGet();
		Data->GetValueAndMaterial(Position.X, Position.Y, Position.Z, Value, Material);
		Data->EndGet();

		return Value;
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("Get value: Not in world: (%d, %d, %d)"), Position.X, Position.Y, Position.Z);
		return 0;
	}
}

FVoxelMaterial AVoxelWorld::GetMaterial(FIntVector Position) const
{
	if (IsInWorld(Position))
	{
		FVoxelMaterial Material;
		float Value;

		Data->BeginGet();
		Data->GetValueAndMaterial(Position.X, Position.Y, Position.Z, Value, Material);
		Data->EndGet();

		return Material;
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("Get material: Not in world: (%d, %d, %d)"), Position.X, Position.Y, Position.Z);
		return FVoxelMaterial();
	}
}

void AVoxelWorld::SetValue(FIntVector Position, float Value)
{
	if (IsInWorld(Position))
	{
		Data->BeginSet();
		Data->SetValue(Position.X, Position.Y, Position.Z, Value);
		Data->EndSet();
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("Get material: Not in world: (%d, %d, %d)"), Position.X, Position.Y, Position.Z);
	}
}

void AVoxelWorld::SetMaterial(FIntVector Position, FVoxelMaterial Material)
{
	if (IsInWorld(Position))
	{
		Data->BeginSet();
		Data->SetMaterial(Position.X, Position.Y, Position.Z, Material);
		Data->EndSet();
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("Set material: Not in world: (%d, %d, %d)"), Position.X, Position.Y, Position.Z);
	}
}


void AVoxelWorld::GetSave(FVoxelWorldSave& OutSave) const
{
	Data->GetSave(OutSave);
}

void AVoxelWorld::LoadFromSave(FVoxelWorldSave Save, bool bReset)
{
	if (Save.Depth == Depth)
	{
		std::forward_list<FIntVector> ModifiedPositions;
		Data->LoadFromSaveAndGetModifiedPositions(Save, ModifiedPositions, bReset);
		for (auto Position : ModifiedPositions)
		{
			UpdateChunksAtPosition(Position, true);
		}
		Render->ApplyUpdates();
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("LoadFromSave: Current Depth is %d while Save one is %d"), Depth, Save.Depth);
	}
}



void AVoxelWorld::UpdateVoxelModifiers()
{
	if (IsCreated())
	{
		DestroyWorld();
	}
	CreateWorld(false);

	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), FoundActors);

	for (AActor* Actor : FoundActors)
	{
		AVoxelModifier* Modifier = Cast<AVoxelModifier>(Actor);

		if (Modifier)
		{
			Modifier->ApplyToWorld(this);
		}
	}

	GetSave(WorldSave);

	DestroyWorld();

	CreateInEditor();
}

AVoxelWorldEditorInterface* AVoxelWorld::GetVoxelWorldEditor() const
{
	return VoxelWorldEditor;
}

FVoxelData* AVoxelWorld::GetData() const
{
	return Data;
}

UVoxelWorldGenerator* AVoxelWorld::GetWorldGenerator() const
{
	return InstancedWorldGenerator;
}

int32 AVoxelWorld::GetSeed() const
{
	return Seed;
}

float AVoxelWorld::GetFoliageFPS() const
{
	return FoliageFPS;
}

float AVoxelWorld::GetLODUpdateFPS() const
{
	return LODUpdateFPS;
}

UMaterialInterface* AVoxelWorld::GetVoxelMaterial() const
{
	return VoxelMaterial;
}

bool AVoxelWorld::GetComputeTransitions() const
{
	return bComputeTransitions;
}

bool AVoxelWorld::GetComputeCollisions() const
{
	return bComputeCollisions;
}

float AVoxelWorld::GetDeletionDelay() const
{
	return DeletionDelay;
}

FIntVector AVoxelWorld::GlobalToLocal(FVector Position) const
{
	FVector P = GetTransform().InverseTransformPosition(Position) / GetVoxelSize();
	return FIntVector(FMath::RoundToInt(P.X), FMath::RoundToInt(P.Y), FMath::RoundToInt(P.Z));
}

FVector AVoxelWorld::LocalToGlobal(FIntVector Position) const
{
	return GetTransform().TransformPosition(GetVoxelSize() * (FVector)Position);
}

void AVoxelWorld::UpdateChunksAtPosition(FIntVector Position, bool bAsync)
{
	Render->UpdateChunksAtPosition(Position, bAsync);
}

void AVoxelWorld::UpdateAll(bool bAsync)
{
	Render->UpdateAll(bAsync);
}

void AVoxelWorld::AddInvoker(TWeakObjectPtr<UVoxelInvokerComponent> Invoker)
{
	Render->AddInvoker(Invoker);
}

void AVoxelWorld::CreateWorld(bool bLoadFromSave)
{
	check(!IsCreated());

	UE_LOG(VoxelLog, Warning, TEXT("Loading world"));

	Depth = NewDepth;
	VoxelSize = NewVoxelSize;

	SetActorScale3D(FVector::OneVector);

	check(!Data);
	check(!Render);

	if (!InstancedWorldGenerator || InstancedWorldGenerator->GetClass() != WorldGenerator->GetClass())
	{
		// Create generator
		InstancedWorldGenerator = NewObject<UVoxelWorldGenerator>((UObject*)GetTransientPackage(), WorldGenerator);
		if (InstancedWorldGenerator == nullptr)
		{
			UE_LOG(VoxelLog, Error, TEXT("Invalid world generator"));
			InstancedWorldGenerator = NewObject<UVoxelWorldGenerator>((UObject*)GetTransientPackage(), UFlatWorldGenerator::StaticClass());
		}
	}

	InstancedWorldGenerator->SetVoxelWorld(this);

	// Create Data
	Data = new FVoxelData(Depth, InstancedWorldGenerator);

	// Create Render
	Render = new FVoxelRender(this, this, Data, MeshThreadCount, FoliageThreadCount);

	// Load from save
	if (bLoadFromSave && WorldSave.Depth == Depth)
	{
		std::forward_list<FIntVector> ModifiedPositions;
		Data->LoadFromSaveAndGetModifiedPositions(WorldSave, ModifiedPositions, false);
	}

	bIsCreated = true;
}

void AVoxelWorld::DestroyWorld()
{
	check(IsCreated());

	UE_LOG(VoxelLog, Warning, TEXT("Unloading world"));

	check(Data);
	check(Render);
	delete Data;
	Render->Destroy();
	delete Render;
	Data = nullptr;
	Render = nullptr;

	bIsCreated = false;
}

void AVoxelWorld::CreateInEditor()
{
	if (VoxelWorldEditorClass)
	{
		// Create/Find VoxelWorldEditor
		VoxelWorldEditor = nullptr;

		TArray<AActor*> FoundActors;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), VoxelWorldEditorClass, FoundActors);

		for (auto Actor : FoundActors)
		{
			auto VoxelWorldEditorActor = Cast<AVoxelWorldEditorInterface>(Actor);
			if (VoxelWorldEditorActor)
			{
				VoxelWorldEditor = VoxelWorldEditorActor;
				break;
			}
		}
		if (!VoxelWorldEditor)
		{
			// else spawn
			VoxelWorldEditor = Cast<AVoxelWorldEditorInterface>(GetWorld()->SpawnActor(VoxelWorldEditorClass));
		}

		VoxelWorldEditor->Init(this);


		if (IsCreated())
		{
			DestroyWorld();
		}
		CreateWorld();

		bComputeCollisions = false;

		AddInvoker(VoxelWorldEditor->GetInvoker());

		UpdateAll(true);
	}
}

void AVoxelWorld::DestroyInEditor()
{
	if (IsCreated())
	{
		DestroyWorld();
	}
}

bool AVoxelWorld::IsCreated() const
{
	return bIsCreated;
}

int AVoxelWorld::GetDepthAt(FIntVector Position) const
{
	if (IsInWorld(Position))
	{
		return Render->GetDepthAt(Position);
	}
	else
	{
		UE_LOG(VoxelLog, Error, TEXT("GetDepthAt: Not in world: (%d, %d, %d)"), Position.X, Position.Y, Position.Z);
		return 0;
	}
}

bool AVoxelWorld::IsInWorld(FIntVector Position) const
{
	return Data->IsInWorld(Position.X, Position.Y, Position.Z);
}

float AVoxelWorld::GetVoxelSize() const
{
	return VoxelSize;
}

int AVoxelWorld::Size() const
{
	return Data->Size();
}
