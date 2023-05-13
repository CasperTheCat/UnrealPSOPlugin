// Copyright Chris Anderson, 2022. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/StreamableManager.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"

#include "LoadHelpers.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncPSOLoaded, int, remainingShaders, int, totalShaders);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAsyncPSOUpdated, int, remainingShaders, int, totalShaders);
DECLARE_DELEGATE_OneParam(FOnAsyncPSOLoadedInternalCallback, int);

UCLASS()
class PIPELINECACHEDB_API UPSOLevelLoadHelper : public UBlueprintAsyncActionBase
{
    GENERATED_BODY()
public:
    /**
     * Schedule an async save to a specific slot. UGameplayStatics::AsyncSaveGameToSlot is the native version of this.
     * When the save has succeeded or failed, the completed pin is activated with success/failure and the save game
     * object. Keep in mind that some platforms may not support trying to load and save at the same time.
     *
     * @param
     */
    UFUNCTION(BlueprintCallable,
              meta = (BlueprintInternalUseOnly = "true", Category = "PSO", WorldContext = "WorldContextObject"))
    static UPSOLevelLoadHelper *AsyncCompilePSOShaders(UObject *WorldContextObject, const int32 MaxShaders);

    UFUNCTION(BlueprintCallable,
              meta = (BlueprintInternalUseOnly = "true", Category = "PSO", WorldContext = "WorldContextObject"))
    static UPSOLevelLoadHelper *AsyncBackgroundPSOShaders(UObject *WorldContextObject, const int32 MaxShaders);

    UFUNCTION(BlueprintCallable,
              meta = (BlueprintInternalUseOnly = "true", Category = "PSO", WorldContext = "WorldContextObject"))
    static UPSOLevelLoadHelper *AsyncPrecompilePSOShadersWithFeedback(UObject *WorldContextObject);

    UFUNCTION(BlueprintCallable,
              meta = (BlueprintInternalUseOnly = "true", Category = "PSO", WorldContext = "WorldContextObject"))
    static UPSOLevelLoadHelper *AsyncHaltPSOShaders(UObject *WorldContextObject);

    /** Delegate called when the save/load completes */
    UPROPERTY(BlueprintAssignable)
    FOnAsyncPSOLoaded Completed;

    UPROPERTY(BlueprintAssignable)
    FOnAsyncPSOUpdated Updated;

    /** Execute the actual operation */
    virtual void Activate() override;

protected:
    enum class EPSOState : uint8
    {
        Preload,
        Compile,
        Background,
        Halt
    };

    /** Which operation is being run */
    EPSOState Operation;

    UPROPERTY()
    UGameInstance *WorldInstance;

    // UPROPERTY()
    // ARedGameState* GameState;

    int64 MaxShaders;
    int64 LastIterationShaders;
    int64 ReferenceCurrentShaders;
    FTimerHandle StepHandler;

    virtual void WaitForBatch(); // const int MaxShaders, FOnAsyncPSOLoadedInternalCallback Delegate);

    /** Called at completion of save/load to execute delegate */
    virtual void ExecuteCompleted(const int RemainingShaders);
};
