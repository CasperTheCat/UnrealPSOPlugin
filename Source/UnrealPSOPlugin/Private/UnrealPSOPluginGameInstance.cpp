// Copyright Chris Anderson, 2022. All Rights Reserved.

#include "PipelineGameInstance.h"

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

// Sets default values for this component's properties
UPipelineCacheGameInstance::UPipelineCacheGameInstance()
{
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
    // GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString("ReturnToMainMenu"));

    // ShutdownInternalPSO();

    // UE_LOG(LogTemp, Warning, TEXT("ReturnToMainMenu"));

    Super::ReturnToMainMenu();
}

void UPipelineCacheGameInstance::Init()
{
    // GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString("Init"));

    // UE_LOG(LogTemp, Warning, TEXT("Init"));

    // ShutdownInternalPSO();
    // FPipelineFileCacheManager::Initialize(100);

    Super::Init();
}

void UPipelineCacheGameInstance::StartGameInstance()
{
    // GEngine->AddOnScreenDebugMessage(-1, 15.f, FColor::Red, FString("StartGameInstance"));

    // UE_LOG(LogTemp, Warning, TEXT("StartGameInstance"));

    // ShutdownInternalPSO();
    // FShaderPipelineCache::Initialize(GMaxRHIShaderPlatform);

    // FShaderPipelineCache::ClosePipelineFileCache();
    // FString Name = FApp::GetProjectName();
    // FShaderPipelineCache::OpenPipelineFileCache(Name, GMaxRHIShaderPlatform)

    Super::StartGameInstance();
}
