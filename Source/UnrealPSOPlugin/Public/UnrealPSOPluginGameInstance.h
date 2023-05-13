// Copyright Chris Anderson, 2022. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "PipelineFileCache.h"
#include "ShaderPipelineCache.h"

#include "UnrealPSOPluginGameInstance.generated.h"

// Taken from https://docs.unrealengine.com/5.2/en-US/optimizing-rendering-with-pso-caches-in-unreal-engine/
// You may want to customise this for your title
union BPSOCacheMaskUnion
{
    uint64 Packed;

    struct
    {
        uint64 MaterialQuality : 4;
        uint64 ShadowQuality : 4;
        uint64 LevelIndex : 8;
    };
};

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
enum class E_PSOOrder : uint8
{
    Default = 0, // Whatever order they are already in.
    FirstToLatestUsed =
        1,              // Start with the PSOs with the lowest first-frame used and work toward those with the highest.
    MostToLeastUsed = 2 // Start with the most often used PSOs working toward the least.
};

UENUM(BlueprintType)
enum class E_PSOCompileMode : uint8
{
    Background, // The maximum batch size is defined by r.ShaderPipelineCache.BackgroundBatchSize
    Fast,       // The maximum batch size is defined by r.ShaderPipelineCache.BatchSize
    Precompile  // The maximum batch size is defined by r.ShaderPipelineCache.PrecompileBatchSize
};

UCLASS(ClassGroup = (Custom), BlueprintType, Blueprintable)
class UNREALPSOPLUGIN_API UPipelineCacheGameInstance : public UGameInstance
{
    GENERATED_BODY()

private:
private:
    void DoShutdownRoutine(FString &Data, FString ShaderType, FString SuppliedPlatform = FString(""));
    void LoadShaders();
    void ShutdownInternalPSO();
    FShaderPipelineCache::BatchMode CompileModeHelper(E_PSOCompileMode CompileMode);

    static bool UsageMaskComparisonFunction(uint64 ReferenceMask, uint64 PSOMask);

public:
    // Sets default values for this component's properties
    UPipelineCacheGameInstance();

    ///// ///// ////////// ///// /////
    // PSO Configuration
    //
    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "")
    E_PSOSaveMode WantedPSOMode;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    E_PSOCompileMode AutomaticPSOCompileMode;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "")
    E_PSOOrder PSOOrder;

    /**
     * Whether to set PrecompileMask
     * 
     * Has little effect unless SetBatchingMode(Precompile) is used
     * Can also be used via Async Helpers
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    bool UsePrecompileMask;

    /**
     * Automatically set the usage mask when the level changes
     * 
     * Uses level index from WorldToMaskIndex
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    bool SetUsageMaskAutomatically;

    /**
     * Automatically set the usage mask when the level changes
     * including in the shipping build.
     * 
     * Has no effect is SetUsageMaskAutomatically is false
     *
     * Uses level index from WorldToMaskIndex
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    bool SetUsageMaskAutomaticallyShipping;

    /**
     * Automatically begin compiling PSOs when the level changes
     *
     * Uses AutomaticPSOCompileMode
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    bool BeginCompilationAutomatically;

    /**
     * Mask used by precompilation
     * 
     * When the mode is "Precompile", PSOs matching this mask are
     * compiled using "Fast", while others set in GameUsageMask are
     * compiled using "Background"
     * 
     * You could use this to have some PSOs always compile quickly
     * Such as a main character or the UI
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    int PrecompileMask;

    ///// ///// ////////// ///// /////
    // Remote Logging
    //
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString ProjectUUID;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString MachineUUID;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString VersionString;

    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    FString ServerURL;

    /**
     * Maps UWorld to Integer Index
     *
     * Index specifies
     */
    UPROPERTY(BlueprintReadWrite, EditDefaultsOnly, Category = "")
    TMap<TSoftObjectPtr<UWorld>, int> WorldToMaskIndex;

    virtual void Shutdown() override;

    virtual void ReturnToMainMenu() override;

    virtual void OnStart() override;

    virtual void StartGameInstance() override;

    // Loaded any level
    virtual void LoadComplete(const float LoadTime, const FString &MapName) override;

    virtual void OnWorldChanged(UWorld *OldWorld, UWorld *NewWorld) override;

public:
    UFUNCTION(BlueprintCallable, BlueprintNativeEvent)
    int LevelToIndex(const TSoftObjectPtr<UWorld> &InWorld);
    virtual int LevelToIndex_Implementation(const TSoftObjectPtr<UWorld> &InWorld);

    UFUNCTION(BlueprintCallable)
    void SetUsageMask(TSoftObjectPtr<UWorld> InWorld);

    UFUNCTION(BlueprintCallable)
    void SetUsageMaskAndCompile(TSoftObjectPtr<UWorld> InWorld);

    UFUNCTION(BlueprintCallable)
    void ClearUsageMask();
};
