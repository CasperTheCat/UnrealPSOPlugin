// Copyright Chris Anderson, 2022. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PipelineFileCache.h"

#include "PipelineGameInstance.generated.h"

// Copied BP Types from the engine
// They aren't BP accessible normally

UENUM(BlueprintType)
enum class E_PSOSaveMode : uint8
{
    Incremental = 0,   // Fast(er) approach which saves new entries incrementally at the end of the file, replacing the
                       // table-of-contents, but leaves everything else alone.
    BoundPSOsOnly = 1, // Slower approach which consolidates and saves all PSOs used in this run of the program,
                       // removing any entry that wasn't seen, and sorted by the desired sort-mode.
    SortedBoundPSOs =
        2 // Slow save consolidates all PSOs used on this device that were never part of a cache file delivered in
          // game-content, sorts entries into the desired order and will thus read-back from disk.
};

UENUM(BlueprintType)
enum class E_PSOPSOOrder : uint8
{
    Default = 0, // Whatever order they are already in.
    FirstToLatestUsed =
        1,              // Start with the PSOs with the lowest first-frame used and work toward those with the highest.
    MostToLeastUsed = 2 // Start with the most often used PSOs working toward the least.
};

UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable)
class PIPELINECACHEDB_API UPipelineCacheGameInstance : public UGameInstance
{
    GENERATED_BODY()

private:
    void DoShutdownRoutine(FString &Data, FString ShaderType, FString SuppliedPlatform = FString(""));
    void LoadShaders();
    void ShutdownInternalPSO();

public:
    // Sets default values for this component's properties
    UPipelineCacheGameInstance();

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "")
    E_PSOSaveMode WantedPSOMode;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "")
    E_PSOPSOOrder PSOOrder;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString ProjectUUID;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString MachineUUID;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString VersionString;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString ServerURL;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    bool UsePSOMask;

    virtual void Shutdown() override;

    virtual void ReturnToMainMenu() override;

    virtual void Init() override;

    virtual void StartGameInstance() override;

protected:
    // Called when the game starts
    // virtual void BeginPlay() override;

public:
    // Called every frame
    // virtual void TickComponent(float DeltaTime, ELevelTick TickType,
    //                           FActorComponentTickFunction* ThisTickFunction) override;
};
