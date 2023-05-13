// Copyright Chris Anderson, 2022. All Rights Reserved.

#include "UnrealPSOPluginGameInstance.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HttpModule.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PipelineFileCache.h"
#include "Runtime/Core/Public/Containers/EnumAsByte.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderPipelineCache.h"

void UPipelineCacheGameInstance::DoShutdownRoutine(FString &Data, FString ShaderType, FString SuppliedPlatform)
{
    auto HttpRequest = FHttpModule::Get().CreateRequest();

    HttpRequest->SetVerb("POST");
    HttpRequest->SetURL(ServerURL + "/api/pco/new/");
    HttpRequest->SetHeader("Content-Type", "application/json");

    if (SuppliedPlatform.Len() == 0)
    {
        SuppliedPlatform = LexToString(GMaxRHIShaderPlatform);
    }

    const auto GraphicsRHI = FApp::GetGraphicsRHI();

    TSharedPtr<FJsonObject> SendableObjectJSON = MakeShared<FJsonObject>();
    SendableObjectJSON->SetStringField("machine", MachineUUID);
    SendableObjectJSON->SetStringField("project", ProjectUUID);
    SendableObjectJSON->SetStringField("version", VersionString);
    SendableObjectJSON->SetStringField("shadertype", ShaderType);
    SendableObjectJSON->SetStringField("platform", GraphicsRHI);
    SendableObjectJSON->SetStringField("shadermodel", SuppliedPlatform);
    SendableObjectJSON->SetStringField("data", Data);

    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    FJsonSerializer::Serialize(SendableObjectJSON.ToSharedRef(), Writer);

    HttpRequest->SetContentAsString(OutputString);
    HttpRequest->ProcessRequest();

    // Hacky solution to the loop
    float WaitTime = 0.f;
    while (HttpRequest->GetStatus() == EHttpRequestStatus::Processing ||
           HttpRequest->GetStatus() == EHttpRequestStatus::NotStarted)
    {
        // Spinlock
        WaitTime += FApp::GetDeltaTime();

        if (WaitTime > 5.f)
        {
            break;
        }
    }

    // Continue as per normal
}

bool TryGet(FString &In, FString &Platform, bool &IsGlobal)
{
    FString PathSplit;
    FString FName;
    In.Split("/", &PathSplit, &FName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

    if (FName.EndsWith(".upipelinecache"))
    {
        FString ValueOfInterest;

        // Okay. We've got the form <Project>_<RHIPlatform>.stable.upipeline
        if (FName.EndsWith(".stable.upipelinecache"))
        {
            IsGlobal = true;

            // Remove project name. Check it's the right project first, though
            if (!FName.StartsWith(FApp::GetProjectName()))
            {
                return false;
            }
            else
            {
                FName.Split("_", &ValueOfInterest, &FName);
            }

            // Split into RHIPlatform <-> stable.upipeline
            FName.Split(".", &ValueOfInterest, &FName);
            Platform = ValueOfInterest;

            // Confirm it's a stable key - U
            // FName.Split(".", &ValueOfInterest, &FName);

            return true;
        }

        // <KEY>.rec.pipeline ->
        else if (FName.EndsWith(".rec.upipelinecache"))
        {
            IsGlobal = false;
            // Remove upipelinecache and rec
            FName.Split(".", &FName, &ValueOfInterest, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            FName.Split(".", &FName, &ValueOfInterest, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            // Remove hash? value or whatever it is
            FName.Split("_", &FName, &ValueOfInterest, ESearchCase::IgnoreCase, ESearchDir::FromEnd);

            // Now, FName is only the start. Find the first _. It should be after the project name
            FName.Split("_", &FName, &ValueOfInterest, ESearchCase::IgnoreCase, ESearchDir::FromStart);
            Platform = ValueOfInterest;

            // Confirm we've parsed vaguely correctly
            FName.Split("-", &FName, &ValueOfInterest, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
            if (ValueOfInterest.Equals(FApp::GetProjectName()))
            {
                return true;
            }

            return false;
        }

        // Warn me of other forms
        else
        {
            checkNoEntry();
        }
    }

    // SHKs
    else if (FName.StartsWith("ShaderStableInfo"))
    {
        // Okay, let's use a key as an example
        // ShaderStableInfo-Global-SF_VULKAN_SM5
        // First loop has VOI set to ShaderStableInfo
        FString ValueOfInterest;
        FName.Split("-", &ValueOfInterest, &FName);

        // Global Check
        FName.Split("-", &ValueOfInterest, &FName);

        if (ValueOfInterest.Equals("Global"))
        {
            IsGlobal = true;
        }
        else if (ValueOfInterest.Equals(FApp::GetProjectName()))
        {
            IsGlobal = false;
        }
        else
        {
            return false;
        }

        // RHI Platform
        FName.Split(".", &ValueOfInterest, &FName);
        Platform = ValueOfInterest;

        return true;

        // // Commented for now
        // while(In.Len() > 0)
        // {
        // 	In.Split("-", &ValueOfInterest, &In);
        //
        // 	// First Iteration
        // 	if(ValueOfInterest.Equals("ShaderStableInfo"))
        // 	{
        // 		continue;
        // 	}
        // }
    }
    else
    {
        // Warn me. It's a case I've missed
        checkNoEntry();
    }
    return false;
}

void GetPipelineNames(TArray<FString> &Strings, const TCHAR *Start, const TCHAR *Type)
{
    TArray<FString> RecordedNames;
    IFileManager::Get().FindFilesRecursive(RecordedNames, Start, Type, true, false);

    // for(auto Name : RecordedNames)
    // {
    // 	auto T = StringToArray<TCHAR, FString>(&Name);
    // 	auto t=*Name;
    // 	UE_LOG(LogTemp, Warning, T);
    // }

    for (auto &Recorded : RecordedNames)
    {
        Strings.Push(Recorded);
    }
}

void UPipelineCacheGameInstance::LoadShaders()
{
    FString RecordingFilename = FPaths::ProjectSavedDir() / TEXT("CollectedPSOs");
    FString Path = FPaths::ProjectSavedDir();
    // FString FilePath = FPaths::ProjectSavedDir() / FString::Printf(TEXT("%s_%s.upipelinecache"), *FileName,
    // *PlatformName.ToString());
    FString GamePathStable =
        FPaths::ProjectContentDir() / TEXT("PipelineCaches") / ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName());
    FString GamePath =
        FPaths::ProjectContentDir() / TEXT("PipelineCaches") / ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName());

    TArray<FString> RecordedNames;
    // GetPipelineNames(RecordedNames, ToCStr(RecordingFilename), TEXT("*.upipelinecache"));
    GetPipelineNames(RecordedNames, ToCStr(GamePath), TEXT("*.upipelinecache"));
    // GetPipelineNames(RecordedNames, ToCStr(GamePathStable), TEXT("*.upipelinecache"));
    GetPipelineNames(RecordedNames, ToCStr(Path), TEXT("*.upipelinecache"));

    for (auto &Recorded : RecordedNames)
    {
        // Load Data
        TArray<uint8> LoadFileData;
        bool bReadOK = FFileHelper::LoadFileToArray(LoadFileData, *Recorded);
        if (bReadOK)
        {
            FString SuppliedPlatform;
            bool Global;
            if (TryGet(Recorded, SuppliedPlatform, Global))
            {
                auto ContentString = FBase64::Encode(LoadFileData);
                DoShutdownRoutine(ContentString, Global ? "stable" : "recorded");
            }
        }
    }

    TArray<FString> KeyInfoNames;
    // GetPipelineNames(KeyInfoNames, ToCStr(RecordingFilename), TEXT("*.shk"));
    GetPipelineNames(KeyInfoNames, ToCStr(GamePath), TEXT("*.shk"));
    // GetPipelineNames(KeyInfoNames, ToCStr(GamePathStable), TEXT("*.shk"));
    GetPipelineNames(KeyInfoNames, ToCStr(Path), TEXT("*.shk"));

    for (auto &KeyInfo : KeyInfoNames)
    {
        // Load Data
        TArray<uint8> LoadFileData;
        bool bReadOK = FFileHelper::LoadFileToArray(LoadFileData, *KeyInfo);
        if (bReadOK)
        {
            FString SuppliedPlatform;
            bool Global;
            if (TryGet(KeyInfo, SuppliedPlatform, Global))
            {
                auto ContentString = FBase64::Encode(LoadFileData);
                DoShutdownRoutine(ContentString, Global ? "globalshaderinfo" : "projectshaderinfo", SuppliedPlatform);
            }
        }
    }
}

void UPipelineCacheGameInstance::ShutdownInternalPSO()
{
    //
    // FPipelineFileCacheManager::SaveMode;
    auto SaveModeAs = static_cast<FPipelineFileCacheManager::SaveMode>(static_cast<uint8>(
        WantedPSOMode)); // TEnumAsByte<FPipelineFileCacheManager::SaveMode>(static_cast<uint8>(WantedPSOMode));

    const FString Name = FApp::GetProjectName();
    auto SaveSuccess = FShaderPipelineCache::SavePipelineFileCache(SaveModeAs);

    // LoadShaders();

    if (SaveSuccess)
    {
        LoadShaders();
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("Could not Save to DB"));
    }
}

bool UPipelineCacheGameInstance::UsageMaskComparisonFunction(uint64 ReferenceMask, uint64 PSOMask)
{
    if (ReferenceMask == UINT64_MAX)
    {
        return true;
    }

    return ReferenceMask == PSOMask;
}

// Sets default values for this component's properties
UPipelineCacheGameInstance::UPipelineCacheGameInstance()
{
    // Default to true as the default isn't harmful
    // I.E. With the UsageMask set, we log PSOs with a usage mask
    // But the shipping build will ignore the mask and just build
    // So we can use them automatic PSO mask for precompile
    SetUsageMaskAutomatically = true;
}

void UPipelineCacheGameInstance::Shutdown()
{
#if !(UE_BUILD_SHIPPING)
    auto InPlaceString = ServerURL;
    while (InPlaceString.EndsWith("/"))
    {
        InPlaceString.Split("/", &InPlaceString, nullptr, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
    }

    ServerURL = InPlaceString;

    ShutdownInternalPSO();

    UE_LOG(LogTemp, Warning, TEXT("UPipelineCacheGameInstance::Shutdown"));
#endif

    Super::Shutdown();
}

void UPipelineCacheGameInstance::ReturnToMainMenu()
{
    Super::ReturnToMainMenu();
}

void UPipelineCacheGameInstance::OnStart()
{
    Super::OnStart();

    // Take the opportunity to nuke the cvar that precompiles.
    // We don't want this to actually run until we set a mask
    static const auto CVarPrecompile = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderPipelineCache.StartupMode"));
    if (CVarPrecompile)
    {
        CVarPrecompile->Set(0);
    }

    // Additionally, set precompile mask for precompile usage
    if (UsePrecompileMask)
    {
        static const auto CVarPrecompileMask =
            IConsoleManager::Get().FindConsoleVariable(TEXT("r.ShaderPipelineCache.PreCompileMask"));
        if (CVarPrecompileMask)
        {
            CVarPrecompileMask->Set(PrecompileMask);
        }
    }
}

void UPipelineCacheGameInstance::StartGameInstance()
{
    Super::StartGameInstance();
}

void UPipelineCacheGameInstance::LoadComplete(const float LoadTime, const FString &MapName)
{
    Super::LoadComplete(LoadTime, MapName);
}

void UPipelineCacheGameInstance::OnWorldChanged(UWorld *OldWorld, UWorld *NewWorld)
{
    Super::OnWorldChanged(OldWorld, NewWorld);

#if (UE_BUILD_SHIPPING)
    if (SetUsageMaskAutomatically && SetUsageMaskAutomaticallyShipping)
#else
    if (SetUsageMaskAutomatically)
#endif
    {
        if (BeginCompilationAutomatically)
        {
            SetUsageMaskAndCompile(NewWorld);
        }
        else
        {
            SetUsageMask(NewWorld);
        }
    }

    GEngine->AddOnScreenDebugMessage(10231, 5.f, FColor::Red, FString::FromInt(LevelToIndex(NewWorld)));
}

int UPipelineCacheGameInstance::LevelToIndex_Implementation(const TSoftObjectPtr<UWorld> &InWorld)
{
    int* UserMaskIndex = WorldToMaskIndex.Find(InWorld);
    if (!UserMaskIndex)
    {
        return INT32_MAX;
    }
    else
    {
        return *UserMaskIndex;
    }
}

FShaderPipelineCache::BatchMode UPipelineCacheGameInstance::CompileModeHelper(E_PSOCompileMode CompileMode)
{
    switch (CompileMode)
    {
    case E_PSOCompileMode::Background:
        return FShaderPipelineCache::BatchMode::Background;
    case E_PSOCompileMode::Fast:
        return FShaderPipelineCache::BatchMode::Fast;
    case E_PSOCompileMode::Precompile:
        return FShaderPipelineCache::BatchMode::Precompile;
    default:
        return FShaderPipelineCache::BatchMode::Background;
    }
}

void UPipelineCacheGameInstance::SetUsageMask(TSoftObjectPtr<UWorld> InWorld)
{
    // Set the PSO Mask
    auto LevelIndex = LevelToIndex(InWorld);

    BPSOCacheMaskUnion Mask{};
    Mask.LevelIndex = LevelIndex;

    // Set Usage Mask
    FShaderPipelineCache::SetGameUsageMaskWithComparison(Mask.Packed,
                                                         &UPipelineCacheGameInstance::UsageMaskComparisonFunction);
}

void UPipelineCacheGameInstance::SetUsageMaskAndCompile(TSoftObjectPtr<UWorld> InWorld)
{
    SetUsageMask(InWorld);

    // Begin a compilation of PSOs based on the current mask
    // TODO: When logging, don't
    FShaderPipelineCache::SetBatchMode(CompileModeHelper(AutomaticPSOCompileMode));

    if (FShaderPipelineCache::IsBatchingPaused())
    {
        FShaderPipelineCache::ResumeBatching();
    }
}

void UPipelineCacheGameInstance::ClearUsageMask()
{
    FShaderPipelineCache::SetGameUsageMaskWithComparison(UINT64_MAX,
                                                         &UPipelineCacheGameInstance::UsageMaskComparisonFunction);
}
