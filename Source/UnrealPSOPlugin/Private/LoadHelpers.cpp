// Copyright Chris Anderson, 2022. All Rights Reserved.

#include "LoadHelpers.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "ShaderPipelineCache.h"
#include "TimerManager.h"

UPSOLevelLoadHelper *UPSOLevelLoadHelper::AsyncCompilePSOShaders(UObject *WorldContextObject, const int32 MaxShaders)
{
    auto Action = NewObject<UPSOLevelLoadHelper>();
    Action->Operation = EPSOState::Compile;
    Action->MaxShaders = MaxShaders;
    Action->RegisterWithGameInstance(WorldContextObject);
    Action->WorldInstance = Cast<UGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));

    return Action;
}

UPSOLevelLoadHelper *UPSOLevelLoadHelper::AsyncBackgroundPSOShaders(UObject *WorldContextObject, const int32 MaxShaders)
{
    auto Action = NewObject<UPSOLevelLoadHelper>();
    Action->Operation = EPSOState::Background;
    Action->MaxShaders = MaxShaders;
    Action->RegisterWithGameInstance(WorldContextObject);
    Action->WorldInstance = Cast<UGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));

    return Action;
}

UPSOLevelLoadHelper *UPSOLevelLoadHelper::AsyncHaltPSOShaders(UObject *WorldContextObject)
{
    auto Action = NewObject<UPSOLevelLoadHelper>();
    Action->Operation = EPSOState::Halt;
    Action->MaxShaders = -1;
    Action->RegisterWithGameInstance(WorldContextObject);
    Action->WorldInstance = Cast<UGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));

    return Action;
}

UPSOLevelLoadHelper *UPSOLevelLoadHelper::AsyncPrecompilePSOShadersWithFeedback(UObject *WorldContextObject)
{
    auto Action = NewObject<UPSOLevelLoadHelper>();
    Action->Operation = EPSOState::Preload;
    Action->MaxShaders = -1;
    Action->RegisterWithGameInstance(WorldContextObject);
    Action->WorldInstance = Cast<UGameInstance>(UGameplayStatics::GetGameInstance(WorldContextObject));

    return Action;
}

void UPSOLevelLoadHelper::Activate()
{
    auto World = WorldInstance->GetWorld();

    switch (Operation)
    {
    case EPSOState::Preload:
        // Initialise PSO Rate to precompile mode
        FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Precompile);
        if (FShaderPipelineCache::IsBatchingPaused())
        {
            FShaderPipelineCache::ResumeBatching();
        }

        // FShaderPipelineCache::OnPrecompilationComplete.BindUObject(this, &UPSOLevelLoadHelper::WaitForBatch);

        ReferenceCurrentShaders = FShaderPipelineCache::NumPrecompilesRemaining();

        GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Green,
                                         FString::FromInt(ReferenceCurrentShaders) + FString(" shaders remaining"));

        if (0 == ReferenceCurrentShaders)
        {
            // Don't trip update callback
            ExecuteCompleted(0);
            return;
        }

        if (World)
        {
            World->GetTimerManager().SetTimer(StepHandler, this, &UPSOLevelLoadHelper::WaitForBatch, 1.0f, true);
        }
        return;

    case EPSOState::Compile:
        // Wait for X shaders to compile, unless we got a zero
        if ((MaxShaders == -1 || MaxShaders > 0) && World)
        {
            // Initialise PSO Rate
            FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Fast);
            if (FShaderPipelineCache::IsBatchingPaused())
            {
                FShaderPipelineCache::ResumeBatching();
            }

            ReferenceCurrentShaders = FShaderPipelineCache::NumPrecompilesRemaining();

            GEngine->AddOnScreenDebugMessage(-1, 1.f, FColor::Red,
                                             FString::FromInt(ReferenceCurrentShaders) + FString(" shaders ss"));

            if (0 == ReferenceCurrentShaders)
            {
                // Don't trip update callback
                ExecuteCompleted(0);
                return;
            }

            World->GetTimerManager().SetTimer(StepHandler, this, &UPSOLevelLoadHelper::WaitForBatch, 0.35f, true);
            // WaitForBatch(MaxShaders, FOnAsyncPSOLoadedInternalCallback::CreateUObject(this,
            // &UPSOLevelLoadHelper::ExecuteCompleted));
            return;
        }

        // Just Leave. Now. We may have been called to just set batch mode
        ExecuteCompleted(-1);
        return;

    case EPSOState::Background:
        // Wait for X shaders to compile, unless we got a zero
        if ((MaxShaders == -1 || MaxShaders > 0) && World)
        {
            // Initialise PSO Rate
            FShaderPipelineCache::SetBatchMode(FShaderPipelineCache::BatchMode::Background);
            if (FShaderPipelineCache::IsBatchingPaused())
            {
                FShaderPipelineCache::ResumeBatching();
            }

            ReferenceCurrentShaders = FShaderPipelineCache::NumPrecompilesRemaining();

            if (0 == ReferenceCurrentShaders)
            {
                // Don't trip update callback
                ExecuteCompleted(0);
                return;
            }

            World->GetTimerManager().SetTimer(StepHandler, this, &UPSOLevelLoadHelper::WaitForBatch, 0.35f, true, 0.5f);
            // WaitForBatch(MaxShaders, FOnAsyncPSOLoadedInternalCallback::CreateUObject(this,
            // &UPSOLevelLoadHelper::ExecuteCompleted));
            return;
        }

        // Just Leave. Now. We may have been called to just set batch mode
        ExecuteCompleted(-1);
        return;
    case EPSOState::Halt:
        if (!FShaderPipelineCache::IsBatchingPaused())
        {
            FShaderPipelineCache::PauseBatching();
        }

        ExecuteCompleted(-1);
        return;
    default:;
    }

    UE_LOG(LogScript, Error, TEXT("UAsyncActionHandleSaveGame Created with invalid operation!"));

    ExecuteCompleted(-1);
}

void UPSOLevelLoadHelper::WaitForBatch() // const int MaxShaders, FOnAsyncPSOLoadedInternalCallback Delegate)
{
    const auto World = WorldInstance->GetWorld();
    if (!IsValid(World))
    {
        // Big problems!
        // We can't actually turn off this timer!
        checkNoEntry();
    }

    const auto currentShader = FShaderPipelineCache::NumPrecompilesRemaining();

    Updated.Broadcast(currentShader, ReferenceCurrentShaders);

    if (-1 == MaxShaders)
    {
        // Ha...
        // Go to finish
        GEngine->AddOnScreenDebugMessage(657414561, 5.f, FColor::Red,
                                         FString::FromInt(currentShader) + FString(" shaders remaining"));

        if (FShaderPipelineCache::IsBatchingPaused())
        {
            GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Magenta, FString("Why are we paused?"));
            FShaderPipelineCache::ResumeBatching();
        }

        if (0 == currentShader)
        {
            // Execute
            World->GetTimerManager().ClearTimer(StepHandler);
            ExecuteCompleted(ReferenceCurrentShaders - currentShader);
        }
    }
    else if ((ReferenceCurrentShaders - currentShader) >= MaxShaders)
    {
        // Cancel the timer
        World->GetTimerManager().ClearTimer(StepHandler);
        FShaderPipelineCache::PauseBatching();
        ExecuteCompleted(ReferenceCurrentShaders - currentShader);
    }
}

void UPSOLevelLoadHelper::ExecuteCompleted(const int RemainingShaders)
{
    Completed.Broadcast(RemainingShaders, RemainingShaders);
    SetReadyToDestroy();
}
